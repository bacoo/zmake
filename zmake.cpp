#include <sys/stat.h>
#include <signal.h>
#include <set>
#include <sstream>
#include <mutex>
#include "zmake.h"

#include "zmake_util.h"

#define BUILD_DIR_NAME ".zmade"
#define FP(f) f->GetFilePath().data()

namespace fs = std::filesystem;
using FSCO = fs::copy_options;
namespace zmake {

//TODO use an uniform way to create global resources, such as:
enum GlobalResourceType {
    GRT_FILE = 1,
    GRT_DEFAULT_COMPILER = 2, GRT_DC = 2,
    GRT_MD5 = 3,
    GRT_RUNNER_BEFORE_BUILD_ALL = 12, GRT_RBB = 12,
    GRT_RUNNER_AFTER_BUILD_ALL = 13, GRT_RAB = 13,
};
template <typename T, GlobalResourceType>
struct GlobalResource {
    static T& Resource() {
        static T s_resource{};
        return s_resource;
    }
    static void InitOnce(std::function<void(T&)> initializer) {
        static std::once_flag s_flag;
        std::call_once(s_flag, [&initializer]() { initializer(Resource()); });
    }
};
auto& GlobalFiles() { return GlobalResource<std::map<std::string, ZFile*>, GRT_FILE>::Resource(); }
constexpr auto GlobalRBB = GlobalResource<std::vector<std::function<void()>>, GRT_RBB>::Resource;
constexpr auto GlobalRAB = GlobalResource<std::vector<std::function<void()>>, GRT_RAB>::Resource;

uint32_t* AccessDebugLevel() {
    static uint32_t s_debug_level = 0;
    return &s_debug_level;
}

std::string* AccessDefaultCompiler(const std::string& suffix) {
    using T = std::map<std::string, std::string>;
    GlobalResource<T, GRT_DC>::InitOnce(
            [](T& compilers) {
        for (auto& x : StringSplit(C_CPP_SOURCE_SUFFIXES, '|')) compilers[x] = "g++";
        compilers[".c"]     = "gcc";
        compilers[".C"]     = "gcc";
        compilers[".a"]     = "ar";
        compilers[".so"]    = "g++";
        compilers[".proto"] = "protoc";
        compilers[".cu"]    = "nvcc";
        compilers[""]       = "g++";
    });
    return &GlobalResource<T, GRT_DC>::Resource()[suffix];
}

//TODO no need Access these root dirs
std::string* AccessProjectRootDir() {
    static std::string s_prj_root_dir = fs::current_path();
    //TODO init once
    if ('/' != *s_prj_root_dir.rbegin()) s_prj_root_dir += "/";
    return &s_prj_root_dir;
}

//TODO change to any other dir and validate it
std::string* AccessBuildRootDir() {
    static std::string s_build_root_dir = *AccessProjectRootDir() + BUILD_DIR_NAME + "/";
    return &s_build_root_dir;
}

bool* AccessVerboseMode() {
    static bool s_verbose = true;
    return &s_verbose;
}
void SetVerboseMode(bool verbose) {
    *AccessVerboseMode() = verbose;
}
void SetDebugLevel(uint32_t level) {
    *AccessDebugLevel() = level;
}

std::string ExecuteCmd(const std::string& cmd, int* ret_code = nullptr) {
    std::string result;
    std::array<char, 128> buffer;
    FILE* f = popen(cmd.data(), "r");
    if (!f) ZTHROW("popen \"%s\" failed", cmd.data());
    int n = 0, rc = 0;
    while ((n = fread(buffer.data(), 1, buffer.size(), f))) result.append(buffer.data(), n);
    if ((rc = pclose(f)) && ret_code) *ret_code = rc;
    return StringRightTrim(result);
}

//wrap all friend functions into this class.
class ZF {
public:
    static void ExecuteBuild(ZFile* f) {
        auto exec_cmd = StringPrintf("(cd %s; %s)", f->_cwd.data(), f->_cmd.data());
        auto tm_start = std::chrono::system_clock::now();
        int ret_code = 0;
        (void)ExecuteCmd(exec_cmd, &ret_code);
        auto spend_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now() - tm_start).count();
        {
            static std::mutex s_mtx;
            std::lock_guard<std::mutex> guard(s_mtx);
            ColorPrint(StringPrintf("@ Build target %s %s, file: %s, spend: %d ms\n",
                    f->_name.data(), ret_code ? "failed" : "OK",
                    f->_file.data(), spend_ms), CT_BRIGHT_YELLOW);
            if (*AccessVerboseMode()) printf("# %s\n", exec_cmd.data());
        }
        if (0 != ret_code) {
            kill(0, SIGKILL);
            _exit(2);
        }
    }
    static void UpdateGeneratedByDep(ZFile* f, bool val) { f->_generated_by_dep = val; }
    static void UpdateCwd(ZFile* f, const std::string& val) { f->_cwd = val; }
    static void AddObjectUser(ZObject* obj, ZFile* user) { obj->AddObjectUser(user); }
    template <typename T, typename... Args>
    static T* Create(Args... args) { return new T(args...); }
};

const std::unordered_map<std::string, std::string>& ZConfig::GetFlags() const {
    return _flags;
}
ZConfig* ZConfig::SetFlag(const std::string& flag) {
    auto parts = StringSplit(flag, '=');
    if (parts.size() > 2) {
        parts.resize(1);
        parts[0] = flag;
    }
    if (!_flags.count(parts[0])) _flag_names.push_back(parts[0]);
    _flags[parts[0]] = (1 == parts.size()) ? "" : parts[1];
    return this;
}
ZConfig* ZConfig::SetFlags(const std::vector<std::string>& flags) {
    for (auto& x : flags) SetFlag(x);
    return this;
}
bool ZConfig::HasFlag(const std::string& flag_name) const {
    return _flags.end() != _flags.find(flag_name);
}
std::string ZConfig::GetFlag(const std::string& flag_name) const {
    auto iter = _flags.find(flag_name);
    return (_flags.end() != iter) ? iter->second : "";
}
void ZConfig::Merge(const ZConfig& other, bool prior_other) {
    for (auto x : other.GetFlags()) {
        if (!HasFlag(x.first)) _flag_names.push_back(x.first);
        else if (!prior_other) continue;
        _flags[x.first] = x.second;
    }
}
std::string ZConfig::ToString(ZConfig* default_conf) const {
    std::ostringstream oss;
    int flag_idx = 0;
    auto process_fn = [&](const ZConfig* cfg, const std::string& name) {
        oss << (flag_idx++ ? " " : "") << name;
        auto iter = cfg->_flags.find(name);
        if (cfg->_flags.end() != iter && "" != iter->second) {
            oss << "=" << iter->second;
        }
    };
    for (auto& n : _flag_names) process_fn(this, n);
    if (default_conf) {
        for (auto& n : default_conf->_flag_names) if (!HasFlag(n)) process_fn(default_conf, n);
    }
    return oss.str();
}

bool ZConfig::Empty() const {
    return _flag_names.empty();
}

ZGenerator::ZGenerator(const std::string& rule): _rule(rule) {}
void ZGenerator::SetRule(const std::string& rule) { _rule = rule; }
std::string ZGenerator::GetRule() const { return _rule; }

std::string ZGenerator::Generate(const std::vector<std::string>& inputs) {
    auto res = _rule;
    for (int idx = 0; ; ++idx) {
        char buf[16];
        int n = snprintf(buf, sizeof(buf), "${%d}", idx + 1);
        auto p = res.find(buf);
        if (std::string::npos == p) break;
        if (idx >= (int)inputs.size()) {
            ZTHROW("no enough inputs(size:%lu) for rule(%s)", inputs.size(), _rule.data());
        }
        res.replace(p, n, inputs[idx]);
    }

    return res;
}
//TODO analyze these macros:
//CPPFLAGS - is the variable name for flags to the C preprocessor.
//CXXFLAGS - is the standard variable name for flags to the C++ compiler.
//CFLAGS - is the standard name for a variable with compilation flags.
//LDFLAGS - should be used for search flags/paths (-L).
//LDLIBS - for linking libraries.
ZConfig* DefaultObjectConfig() {
    static ZConfig s_obj_conf;
    //TODO execute only once
    //TODO modify SetFlag -> Set
    s_obj_conf.SetFlag("-idirafter " + *AccessBuildRootDir());
    return &s_obj_conf;
}
ZConfig* DefaultStaticLibraryConfig() {
    static ZConfig s_static_lib_conf;
    if (!s_static_lib_conf.HasFlag("crs")) {
        s_static_lib_conf.SetFlag("crs");
    }
    return &s_static_lib_conf;
}
ZConfig* DefaultSharedLibraryConfig() {
    static ZConfig s_shared_lib_conf;
    return &s_shared_lib_conf;
}
ZConfig* DefaultBinaryConfig() {
    static ZConfig s_binary_conf;
    return &s_binary_conf;
}

//the scenario is like this:
//  project root dir(running ./BUILD): /workspace/
//  project build root dir: /workspace/${BUILD_DIR_NAME}/
//  cur_dir(including BUILD.cpp): /workspace/core/
//  p(lib_name): curl/net
//  project inner path: /core/curl/net
//  build path: /workspace/${BUILD_DIR_NAME}/core/curl/libnet.a
std::string ConvertToProjectInnerPath(const std::string& p) {
    if ('/' == p.at(0) || '@' == p.at(0)) return p;
    std::string result =
            fs::absolute(p).lexically_relative(*AccessProjectRootDir()).lexically_normal();
#ifdef __MACH__
    result = fs::path(result).lexically_relative(fs::current_path());
#endif
    if ('/' != result.at(0)) result = "/" + result;
    return result;
}
std::string GetBuildPath(const std::string& path) {
    if ("" == path) return path;
    fs::path build_path = path;
    if ('/' != path.at(0) || !StringBeginWith(path, *AccessBuildRootDir())) {
        const std::string prj_inner_path = ConvertToProjectInnerPath(path);
        if (StringBeginWith(prj_inner_path, *AccessProjectRootDir())) {
            build_path = *AccessBuildRootDir() +
                    prj_inner_path.substr(AccessProjectRootDir()->size());
        } else {
            build_path = *AccessBuildRootDir() + prj_inner_path.substr(1);
        }
    }
    build_path = build_path.lexically_normal();

    auto build_dir = build_path.parent_path();
    if (!fs::exists(build_dir)) {
        fs::create_directories(build_dir);
    }

    return build_path;
}

std::string GetBuildRootPath(const std::string& path) {
    std::string root = path;
    if ('/' != *root.rbegin()) root += "/";
    auto p = root.find(StringPrintf("/%s/", BUILD_DIR_NAME));
    if (std::string::npos != p) {
        return root.substr(0, p + strlen(BUILD_DIR_NAME) + 2);
    }
    return path;
}

ZFile*& AccessFileInternal(const std::string& file, bool create_file = false,
        bool need_build = false, FileType ft = FT_NONE) {
    std::string p = file;
    if (FT_SOURCE_FILE == ft || StringEndWith(file, C_CPP_SOURCE_SUFFIXES)) {
        if (FT_NONE == ft) ft = FT_SOURCE_FILE;
        p = fs::absolute(file).lexically_normal();
    } else if (FT_HEADER_FILE == ft || StringEndWith(file, C_CPP_HEADER_SUFFIXES)) {
        if (FT_NONE == ft) ft = FT_HEADER_FILE;
        p = fs::absolute(file).lexically_normal();
    } if (FT_PROTO_FILE == ft || StringEndWith(file, ".proto")) {
        if (FT_NONE == ft) ft = FT_PROTO_FILE;
        p = fs::absolute(file).lexically_normal();
    } else {
        p = ConvertToProjectInnerPath(p);
    }

    auto*& result = GlobalFiles()[p];
    if (!result) {
        if (create_file) {
            if ('@' == file.at(0)) {
                ZTHROW("can't create external library(%s), please import it first", file.data());
            }
            result = ZF::Create<ZFile>(p, ft, need_build);
        }
    }
    return result;
}

std::vector<std::string> Glob(const std::vector<std::string>& rules,
        const std::vector<std::string>& exclude_rules, const std::string& dir) {
    std::vector<std::regex> exclude_regexes = {std::regex("(^|/)BUILD.cpp$")};
    for (auto exclude_rule : exclude_rules) {
        exclude_rule = StringReplaceAll(exclude_rule, ".", "\\.");
        exclude_rule = StringReplaceAll(exclude_rule, "**", "*");
        exclude_rule = StringReplaceAll(exclude_rule, "*", "[^/]*");
        if (std::string::npos == exclude_rule.find("/")) {
            exclude_regexes.emplace_back("(^|/)" + exclude_rule + "$");
        } else {
            exclude_regexes.emplace_back(exclude_rule + "$");
        }
    }
    auto hit_exclude_rule_fn = [&](const std::string& f) -> bool {
        for (const auto& r : exclude_regexes) if (std::regex_search(f, r)) return true;
        return false;
    };
    std::vector<std::string> result;
    std::set<std::string> uniq_results;
    for (auto rule : rules) {
        bool recursive = std::string::npos != rule.find("**");
        recursive = recursive || std::string::npos != rule.find("/"); //detect sub dir
        rule = StringReplaceAll(rule, ".", "\\.");
        rule = StringReplaceAll(rule, "**", "*");
        rule = StringReplaceAll(rule, "*", "[^/]*");
        std::regex r(rule + "$");
        for (auto f : ListFilesUnderDir(dir, "", recursive)) {
            if (uniq_results.count(f)) continue;
            auto rf = f; //rf(reg file): file for matching regex rules
            if (StringBeginWith(rf, dir)) {
                rf = rf.substr(dir.size());
                if ('/' == rf.at(0)) rf = rf.substr(1);
            }
            if (std::regex_search(rf, r) && !hit_exclude_rule_fn(rf)) {
                uniq_results.insert(f);
                result.push_back(f);
            }
        }
    }
    return result;
}

void SetObjsFlags(const std::vector<std::string>& paths, const std::vector<std::string>& flags) {
    for (auto path : paths) {
        if (std::string::npos != GetDirnameFromPath(path).find('*')) {
            ZTHROW("doesn't support '*' glob within dir name(%s)", path.data());
        }
        if (std::string::npos == path.find('*')) {
            for (auto flag : flags) AccessObject(path)->SetFlag(flag);
        } else {
            for (const auto& f :
                    Glob({GetFilenameFromPath(path)}, {"BUILD.cpp"}, GetDirnameFromPath(path))) {
                for (auto flag : flags) AccessObject(f)->SetFlag(flag);
            }
        }
    }
}

ZGenerator*& AccessDefaultGenerator(const std::string& suffix) {
    static std::unordered_map<std::string, ZGenerator*> s_generators;
    return s_generators[suffix];
}
ZGenerator* GetDefaultGenerator(const std::string& suffix) {
    return AccessDefaultGenerator(suffix);
}
void RegisterDefaultGenerator(const std::string& suffix, const ZGenerator& g) {
    auto*& def_g = AccessDefaultGenerator(suffix);
    if (!def_g) {
        def_g = new ZGenerator(g);
    } else {
        *def_g = g;
    }
}

long AcquireFileMTime(const std::string& path) {
    static std::unordered_map<std::string, long> s_file_stats;
    static std::mutex s_mtx;

    long mtime = 0;
    RunWithLock(s_mtx, [&mtime, &path]() {
        if (s_file_stats.count(path)) mtime = s_file_stats[path];
    });
    if (0 == mtime) {
        struct stat result;
        if (0 != stat(path.data(), &result)) return -1;
#ifdef __MACH__
        mtime = result.st_mtimespec.tv_sec * 1000000000UL + result.st_mtimespec.tv_nsec;
#else
        mtime = result.st_mtim.tv_sec * 1000000000UL + result.st_mtim.tv_nsec;
#endif
        RunWithLock(s_mtx, [&mtime, &path]() { s_file_stats[path] = mtime; });
    }
    return mtime;
}

std::string FormalizeLibraryName(const std::string& lib_name, bool is_imported_lib = false) {
    std::string name = lib_name;
    if (is_imported_lib && '@' != name.at(0)) name = "@" + name;
    if (':' == name.at(0)) name = name.substr(1);
    std::string filename = GetFilenameFromPath(name);
    auto p = filename.rfind(':');
    if (std::string::npos != p) {
        filename[p] = '/';
        if (std::string::npos != filename.find(':')) {
            ZTHROW("the filename part of lib_name(%s) should only have one ':' at most",
                    lib_name.data());
        }
        name = GetDirnameFromPath(name) + filename;
    }
    name = ConvertToProjectInnerPath(name);
    if ('@' == name.at(0) && std::string::npos == name.find('/')) name += "/";
    return fs::path(name).lexically_normal();
}

void ProcessDepsRecursively(const std::vector<ZFile*>& deps, const std::function<void(ZFile*)>& fn,
        std::set<ZFile*>* uniq_deps = nullptr) {
    auto valid_uniq_deps = uniq_deps ? uniq_deps : new std::set<ZFile*>();
    for (auto iter = deps.rbegin(); deps.rend() != iter; ++iter) {
        if (!valid_uniq_deps->insert(*iter).second) continue;
        ProcessDepsRecursively((*iter)->GetDeps(), fn, valid_uniq_deps);
        fn(*iter);
    }
    if (!uniq_deps) delete valid_uniq_deps;
}

void UpdateOptimizationLevel(std::string& cmd, size_t pos = 0, bool del_other_opts = false) {
    if (!CommandArgs::Has("-O") || pos >= cmd.size()) return;
    auto o_level = StringPrintf(" -O%d", CommandArgs::Get<int>("-O", 0));
    auto p = cmd.find(" -O", pos);
    if (std::string::npos == p) {
        if (0 != CommandArgs::Get<int>("-O", 0) && !del_other_opts) cmd.append(o_level);
    } else {
        auto p_end = cmd.find(" ", p + 3);
        //it's compatible with std::string::npos == p_end
        auto level = cmd.substr(p + 3, p_end - (p + 3));
        if ("" == level || "0" == level || "1" == level || "2" == level ||
                "3" == level || "g" == level || "s" == level || "fast" == level) {
            cmd.replace(p, p_end - p, del_other_opts ? "" : o_level);
            if (!del_other_opts) del_other_opts = true;
        }
        //process other duplicated -O flags
        UpdateOptimizationLevel(cmd, p + 3, del_other_opts);
    }
}

ZFile::ZFile(const std::string& path, FileType ft, bool need_build):
        _file(path), _ft(ft), _build_done(!need_build) {
    if (need_build) _file = GetBuildPath(path);
    _cwd = fs::current_path();
    _compiler = *AccessDefaultCompiler(fs::path(_file).extension());
}
ZFile::~ZFile() {
    if (_conf) delete _conf;
    if (_generator) delete _generator;
}

ZFile* ZFile::SetGenerator(const ZGenerator& g) {
    if (!_generator) _generator = new ZGenerator();
    *_generator = g;
    return this;
}
ZGenerator* ZFile::GetGenerator() const {
    return _generator;
}

ZConfig* ZFile::GetConfig() {
    if (!_conf) _conf = new ZConfig();
    return _conf;
}
void ZFile::SetConfig(const ZConfig& conf) {
    if (!_conf) {
        _conf = new ZConfig();
    } else if (!_conf->Empty()) {
        fprintf(stderr, "[Warn]substitute the existed config for file '%s'\n", _file.data());
    }
    *_conf = conf;
}
ZFile* ZFile::SetFlag(const std::string& flag) {
    GetConfig()->SetFlag(flag);
    return this;
}
ZFile* ZFile::SetFlags(const std::vector<std::string>& flags) {
    GetConfig()->SetFlags(flags);
    return this;
}

ZFile* ZFile::AddDep(ZFile* dep) {
    if (_uniq_deps.insert(dep->GetFilePath()).second) {
        _deps.push_back(dep);
        ProcessDepsRecursively(_deps, [&](ZFile* dep) {
            if (dep == this) ZTHROW("Detected circular dependency for '%s'", _file.data());
        });
        //libs should be built before objs, considering following scenario:
        //  AccessLibrary("cc_base_proto")->AddProto("base.proto");
        //  AccessLibrary("cc_ps_proto")
        //    ->AddProto("ps.proto")
        //    ->AddDep("cc_base_proto");
        //
        //for libcc_ps_proto.a, libcc_base_proto.a should be compiled first, because
        //compiling ps.pb.cc needs base.pb.h;
        if (FT_OBJ_FILE != dep->GetFileType() && _deps.size() > 1) {
            for (int i = _deps.size() - 2; i >= -1; --i) {
                if (i >= 0 && FT_OBJ_FILE == _deps[i]->GetFileType()) continue;
                std::swap(_deps[i + 1], _deps.back());
                break;
            }
        }
    }
    return this;
}
ZFile* ZFile::AddDep(const std::string& dep) {
    auto f = AccessFileInternal(dep);
    if (!f) ZTHROW("no this dep(%s), please use AccessXXX to create it first", dep.data());
    return AddDep(f);
}

ZFile* ZFile::AddDepLibs(const std::vector<std::string>& dep_libs) {
    for (auto dep : dep_libs) {
        std::string dep_name = FormalizeLibraryName(dep);
        bool is_glob_match = ('/' == *dep_name.rbegin());
        if ('*' == *dep_name.rbegin()) {
            dep_name.pop_back();
            if (std::string::npos != dep_name.find('*')) {
                ZTHROW("contain '*' in the middle of dep name(%s)", dep_name.data());
            }
            is_glob_match = true;
        }
        if ('@' == dep_name.at(0) && !is_glob_match) {
            auto pkg_name = StringSplit(dep_name, '/')[0].substr(1);
            if ("@" + pkg_name + "/" + pkg_name == dep_name) {
                if (auto f = AccessFileInternal(dep_name)) {
                    AddDep(f);
                    continue;
                }
                dep_name = "@" + pkg_name + "/";
                is_glob_match = true;
            }
        }

        auto process_fn = [this](const std::string& dep_name, bool is_glob_match) {
            bool find_libs = false;
            auto& files = GlobalFiles();
            for (auto iter = files.lower_bound(dep_name); files.end() != iter; ++iter) {
                if (!StringBeginWith(iter->first, is_glob_match ? dep_name : dep_name + "/")) break;
                if (iter->second && FT_LIB_FILE == iter->second->GetFileType()) {
                    AddDep(iter->second);
                    find_libs = true;
                    if (!is_glob_match && iter->first == dep_name) break;
                }
            }
            if (!find_libs) {
                if (!is_glob_match) AddDep(AccessLibrary(dep_name));
                else ZTHROW("can't find any lib with the '%s' prefix", dep_name.data());
            }
        };

        if ('@' != dep_name.at(0)) {
            if (is_glob_match || fs::is_directory(*AccessProjectRootDir() + dep_name)) {
                if ('/' != *dep_name.rbegin()) dep_name += "/";
                RegisterRunnerBeforeBuildAll([process_fn, dep_name]() {
                    process_fn(dep_name, true);
                });
                continue;
            }
        }
        process_fn(dep_name, is_glob_match);
    }
    return this;
}
const std::vector<ZFile*>& ZFile::GetDeps() const {
    return _deps;
}

void ZFile::DumpDepsRecursively(std::string* dump_sinker) const {
    std::ostringstream oss;
    std::string indent;
    std::function<void(const ZFile*)> process_dep_fn;
    process_dep_fn = [&](const ZFile* file) {
        auto p = file->GetFilePath();
        if (StringBeginWith(p, "/usr/include/")) return;
        if (FT_HEADER_FILE == file->GetFileType() && StringBeginWith(p, "/usr/")) return;
        oss << indent << (indent.empty() ? "" : " ") << p << std::endl;
        indent += ".";
        for (auto dep : file->GetDeps()) process_dep_fn(dep);
        indent.pop_back();
    };
    process_dep_fn(this);
    if (dump_sinker) *dump_sinker = oss.str();
    else printf("%s", oss.str().data());
}

void ZFile::SetFullCommand(const std::string& cmd) {
    _cmd = cmd;
}
std::string ZFile::GetFullCommand(bool print_pretty) {
    if ("" == _cmd) ComposeCommand();
    if (print_pretty) {
        auto p = _cmd.find(" -o ");
        if (std::string::npos != p) {
            p = _cmd.find(" ", p + 4);
            if (std::string::npos != p) {
                return _cmd.substr(0, p) + StringReplaceAll(_cmd.substr(p), " ", "\n");
            }
        }
        return StringReplaceAll(_cmd, " ", "\n");
    }
    return _cmd;
}

std::string ZFile::GetFilePath() const {
    return _file;
}
FileType ZFile::GetFileType() const {
    return _ft;
}
std::string ZFile::GetCwd() const {
    return _cwd;
}

bool ZFile::ComposeCommand() {
    if ("" == _cmd && !_generated_by_dep) {
        if (_generator) {
            _cmd = _generator->Generate({_file});
        } else {
            fs::path p(_file);
            auto g = GetDefaultGenerator(p.extension());
            if (g) {
                _cmd = g->Generate({_file});
            } else if (StringEndWith(_file, C_CPP_HEADER_SUFFIXES)) {
                _ft = FT_HEADER_FILE;
                _build_done = true;
                return false;
            } else {
                fprintf(stderr, "[Warn]no need to build this file(%s)\n", _file.data());
                _build_done = true;
                return false;
            }
        }
    }
    return "" != _cmd || _generated_by_dep;
}

void ZFile::BeTarget() {
    AddTarget(this);
}

struct Md5Cache {
    static auto& GetAll() {
        using T = std::map<std::string, std::string>;
        GlobalResource<T, GRT_MD5>::InitOnce([](T& file_md5s) {
            for (auto& line : StringSplit(StringFromFile(*AccessBuildRootDir() +
                    "BUILD.md5s"), '\n')) {
                const auto& infos = StringSplit(line, ' ');
                if (2 == infos.size()) file_md5s[infos[0]] = infos[1];
            }
        });
        return GlobalResource<T, GRT_MD5>::Resource();
    }

    static std::string Get(const std::string& file, bool check_change = true) {
        auto& file_md5s = GetAll();
        std::string old_md5;
        static std::mutex s_mtx;
        RunWithLock(s_mtx, [&]() { if (file_md5s.count(file)) old_md5 = file_md5s[file]; });
        //start with '@': checked md5 already, and it changed
        //start with '*': checked md5 already, and it has no change
        if ("" != old_md5 && (!check_change || ('@' == old_md5.at(0) || '*' == old_md5.at(0)))) {
            return old_md5;
        }

        auto new_md5 = ExecuteCmd(StringPrintf("md5sum %s | awk '{print $1}'", file.data()));
        new_md5 = (new_md5 != old_md5 ? "@" : "*") + new_md5;
        RunWithLock(s_mtx, [&]() { file_md5s[file] = new_md5; });
        return new_md5;
    }
};

bool ZFile::Build() {
    bool debug_flag = true;
    if (_build_done && !_forced_build) return _has_been_built;

    bool build_dependencies = false;
    for (auto dep : GetDeps()) {
        bool build_res = dep->Build();
        build_dependencies |= build_res;
        if (*AccessDebugLevel() > 0 && debug_flag && build_dependencies) {
            printf("> build %s since the dependency '%s' has been built\n",
                    _file.data(), dep->GetFilePath().data());
            debug_flag = false;
        }
    }

    if (!ComposeCommand()) return false;

    bool need_build = (build_dependencies || !fs::exists(_file) ||
            fs::is_empty(_file) || _forced_build);
    if (*AccessDebugLevel() > 0 && debug_flag && need_build) {
        if (!fs::exists(_file)) {
            printf("> build %s since it doesn't exist\n", _file.data());
        } else if (_forced_build) {
            printf("> build %s since _forced_build == true\n", _file.data());
        }
        debug_flag = false;
    }
    if (!need_build) need_build = (_cmd != StringFromFile(GetBuildPath(_file) + ".cmd"));
    if (*AccessDebugLevel() > 0 && debug_flag && need_build) {
        printf("> build %s since the cmd '%s' has been changed to '%s'\n", _file.data(),
                StringFromFile(GetBuildPath(_file) + ".cmd").data(), _cmd.data());
        debug_flag = false;
    }
    if (!need_build) {
        auto mtime = AcquireFileMTime(_file);
        for (auto dep : GetDeps()) {
            if (!fs::exists(dep->GetFilePath())) continue;
            if (AcquireFileMTime(dep->GetFilePath()) >= mtime) {
                if ('@' != Md5Cache::Get(dep->GetFilePath()).at(0)) continue; //md5 has no change
                need_build = true;
                if (*AccessDebugLevel() > 0 && debug_flag && need_build) {
                    printf("> build %s since the mtime(%ld) of dependence '%s' is bigger than "
                            "target's mtime(%ld)\n", _file.data(),
                            AcquireFileMTime(dep->GetFilePath()),
                            dep->GetFilePath().data(), mtime);
                    debug_flag = false;
                }
                break;
            }
        }
    }
    if (need_build) {
        _has_been_built = true;
        if (_generated_by_dep) {
            for (auto dep : GetDeps()) {
                if (fs::exists(_file)) break;
                if (*AccessDebugLevel() > 0) {
                    printf("> generate %s by build dep(%s)\n", _file.data(),
                            dep->GetFilePath().data());
                }
                dep->_forced_build = true;
                dep->Build();
            }
        } else {
            StringToFile(_cmd, GetBuildPath(_file) + ".cmd");
            ZF::ExecuteBuild(this);
            if (_forced_build) _forced_build = false;
        }
    }

    _build_done = true;
    return _has_been_built;
}

ZObject::ZObject(const std::string& src_file, const std::string& obj_file):
        ZFile(obj_file, FT_OBJ_FILE, true) {
    _name = src_file;
    _src = fs::absolute(src_file).lexically_normal();
    _compiler = *AccessDefaultCompiler(fs::path(_src).extension());
    _file = GetBuildPath("" == obj_file ?
            StringReplaceSuffix(_src, C_CPP_SOURCE_SUFFIXES, ".o") : obj_file);
    auto dep_file = _file + ".d";
    auto load_dep_file_fn = [this, dep_file]() {
        if (!fs::exists(dep_file)) return;
        auto s = StringFromFile(dep_file);
        auto parts = StringSplit(s, ':');
        if (2 != parts.size()) ZTHROW("can't parse the dependence file(%s)", dep_file.data());
        for (auto dep : StringSplit(StringRightTrim(StringReplaceAll(parts[1], "\\\n", "")), ' ')) {
            //skip check fs::exists(dep), e.g. header file renamed
            AddDep(AccessFile(dep));
        }
    };
    if (fs::exists(dep_file)) load_dep_file_fn();
    else RegisterRunnerAfterBuildAll(load_dep_file_fn);
}

std::string ZObject::GetSourceFile() const {
    return _src;
}

ZObject* ZObject::AddIncludeDir(const std::string& dir) {
    if ("" == dir) return this;
    std::string inc = fs::absolute(dir).lexically_normal();
    if ('/' != *inc.rbegin()) inc += "/";
    if (!_uniq_inc_dirs.count(inc)) {
        _inc_dirs.push_back(inc);
        _uniq_inc_dirs.insert(inc);
    }
    return this;
}
std::vector<std::string> ZObject::GetIncludeDirs() const {
    return _inc_dirs;
}
void ZObject::AddObjectUser(ZFile* file) {
    _users.push_back(file);
}

bool ZObject::ComposeCommand() {
    if ("" == _cmd) {
        _cmd = StringPrintf("%s -c -o %s -MD -MF %s.d", _compiler.data(), _file.data(), _file.data());

        std::set<ZFile*> uniq_deps;
        auto handle_dep_fn = [&](ZFile* dep) {
            if (FT_LIB_FILE == dep->GetFileType()) {
                for (auto inc_dir : ((ZLibrary*)dep)->GetIncludeDirs()) AddIncludeDir(inc_dir);
            }
        };
        //it makes sense to add project root as one include path
        AddIncludeDir(*AccessProjectRootDir());
        ProcessDepsRecursively(GetDeps(), handle_dep_fn, &uniq_deps);
        ProcessDepsRecursively(_users, handle_dep_fn, &uniq_deps);

        for (const auto& inc : _inc_dirs) {
            //avoid hiding system header like <string.h>
            _cmd += StringPrintf(" -idirafter %s", inc.data());
        }
        if (_conf) {
            _cmd += " " + _conf->ToString(DefaultObjectConfig());
        } else {
            _cmd += " " + DefaultObjectConfig()->ToString();
        }
        _cmd += " " + _src;
    }
    UpdateOptimizationLevel(_cmd);
    return true;
}
//TODO support always_link = true
ZLibrary::ZLibrary(const std::string& lib_name, bool is_static_lib): ZFile("", FT_LIB_FILE, true) {
    _name = lib_name;
    std::string lib_file = lib_name;
    if (!StringEndWith(lib_file, ".a|.so")) {
        lib_file += (is_static_lib ? ".a" : ".so");
    }
    if (!StringBeginWith(GetFilenameFromPath(lib_file), "lib")) {
        lib_file = GetDirnameFromPath(lib_file) + "lib" + GetFilenameFromPath(lib_file);
    }
    _file = GetBuildPath(lib_file);
    _compiler = *AccessDefaultCompiler(fs::path(_file).extension());
    _is_static_lib = is_static_lib;
}
ZLibrary::ZLibrary(const std::string& name, const std::vector<std::string>& inc_dirs,
        const std::string& lib_file): ZFile("", FT_LIB_FILE, false) {
    _name = name;
    if ("" != lib_file) _file = fs::absolute(lib_file).lexically_normal();
    for (auto& inc_dir : inc_dirs) {
        _inc_dirs.insert(fs::absolute(inc_dir).lexically_normal());
    }
    _is_static_lib = StringEndWith(_file, ".a");
}

std::string GetObjBindName(const std::string& src, const std::string& bind_name) {
    std::string suffix = StringReplaceAll(bind_name, "/", "-");
    suffix = StringReplaceAll(suffix, ".", "-");
    return StringReplaceSuffix(src, C_CPP_SOURCE_SUFFIXES, suffix + ".o");
}

ZLibrary* ZLibrary::AddObjs(const std::vector<std::string>& src_files, bool bind_flag) {
    for (auto src : src_files) AddObj(AccessObject(src, bind_flag ? GetObjBindName(src, _name) : ""));
    return this;
}
ZLibrary* ZLibrary::AddObj(ZFile* obj) {
    if (FT_OBJ_FILE != obj->GetFileType()) {
        ZTHROW("for lib(%s), '%s' is not an ZObject instance", FP(this), FP(obj));
    }
    if (!_is_static_lib && !obj->GetConfig()->HasFlag("-fPIC")) {
        obj->GetConfig()->SetFlag("-fPIC");
    }
    auto f = (ZObject*)obj;
    f->SetFlags(_objs_flags);
    _objs.push_back(f);
    ZF::AddObjectUser(f, this);
    AddDep(f);
    return this;
}
const std::vector<ZObject*>& ZLibrary::GetObjs() const {
    return _objs;
}
ZLibrary* ZLibrary::SetObjsFlags(const std::vector<std::string>& flags) {
    for (auto f : flags) _objs_flags.push_back(f);
    for (auto obj : _objs) obj->SetFlags(flags);
    return this;
}
ZLibrary* ZLibrary::AddProto(const std::string& proto_file) {
    if (!_added_protobuf_lib_dep) {
        auto& files = GlobalFiles();
        for (auto iter = files.lower_bound("@protobuf/"); files.end() != iter; ++iter) {
            if (!StringBeginWith(iter->first, "@protobuf/")) break;
            if (iter->second && FT_LIB_FILE == iter->second->GetFileType()) {
                AddDep(iter->second);
                _added_protobuf_lib_dep = true;
            }
        }
    }
    return AddObj(AccessProto(proto_file)->SpawnObj());
}
ZLibrary* ZLibrary::AddProtos(const std::vector<std::string>& proto_files) {
    for (auto p : proto_files) AddProto(p);
    return this;
}

const std::set<std::string>& ZLibrary::GetIncludeDirs() {
    if (_inc_dirs.empty()) {
        bool all_srcs_are_pb_cc = !_objs.empty();
        for (auto obj : _objs) {
            if (!StringEndWith(obj->GetSourceFile(), ".pb.cc")) {
                all_srcs_are_pb_cc = false;
                break;
            }
        }
        if (all_srcs_are_pb_cc) {
            _inc_dirs.insert(*AccessBuildRootDir());
        } else {
            _inc_dirs.insert(_cwd);
        }
    }
    return _inc_dirs;
}
ZLibrary* ZLibrary::AddIncludeDir(const std::string& dir, bool create_alias_name) {
    if (!create_alias_name) _inc_dirs.insert(fs::absolute(dir).lexically_normal());
    else {
        _inc_dirs.insert(GetBuildPath(GetCwd()));
        std::string alias = dir;
        if ('/' == *alias.rbegin()) alias.pop_back();
        auto alias_build_path = fs::path(GetBuildPath(GetCwd()) + "/" + alias).lexically_normal();
        if (fs::exists(alias_build_path)) {
            if (fs::is_symlink(alias_build_path) &&
                    fs::equivalent(fs::read_symlink(alias_build_path), GetCwd())) return this;
            ZTHROW("create alias(%s) for lib inc dir failed, since it exists already",
                    alias_build_path.string().data());
        }
        fs::create_directories(alias_build_path.parent_path());
        fs::create_directory_symlink(GetCwd(), alias_build_path);
    }
    return this;
}

std::string ZLibrary::GetLinkDir() const {
    return fs::path(_file).parent_path();
}
std::string ZLibrary::GetLinkLib() const {
    std::string fn = fs::path(_file).filename().string();
    fn = StringReplaceSuffix(fn, ".a|.so", "");
    if (StringBeginWith(fn, "lib")) fn = fn.substr(3);
    return fn;
}
bool ZLibrary::IsStaticLibrary() const {
    return _is_static_lib;
}

ZLibrary* ZLibrary::AddLib(ZFile* lib, bool whole_archive) {
    if (_is_static_lib) {
        ZTHROW("can't add static library(%s) to build a new static library(%s)", FP(lib), FP(this));
    }
    if (FT_LIB_FILE != lib->GetFileType()) {
        ZTHROW("for lib(%s), this file(%s) is not an instance of ZLibrary", FP(this), FP(lib));
    }
    auto f = (ZLibrary*)lib;
    for (auto obj : f->GetObjs()) {
        if (!obj->GetConfig()->HasFlag("-fPIC")) obj->GetConfig()->SetFlag("-fPIC");
    }
    if (whole_archive) _whole_archive_libs.push_back(f);
    else _libs.push_back(f);
    AddDep(f);
    return this;
}
std::vector<ZLibrary*> ZLibrary::GetLibs() const {
    auto result = _whole_archive_libs;
    result.insert(result.end(), _libs.begin(), _libs.end());
    return result;
}

bool ZLibrary::ComposeCommand() {
    if ("" == _cmd) {
        if (_is_static_lib) {
            if (_objs.empty()) {
                if (GetDeps().empty()) ZTHROW("found uninitialized library(%s)", _name.data());
                _build_done = true;
                return false;
            }
            _cmd = StringPrintf("%s", _file.data());
        } else {
            _cmd = StringPrintf("%s -shared -o %s", _compiler.data(), _file.data());
        }
        for (auto obj : _objs) {
            _cmd += StringPrintf(" %s", obj->GetFilePath().data());
        }
        for (auto lib : _libs) {
            if (lib->IsUsedAsWholeArchive()) {
                _cmd += " -Wl,--whole-archive";
                _cmd += StringPrintf(" %s", lib->GetFilePath().data());
                _cmd += " -Wl,--no-whole-archive";
            } else _cmd += StringPrintf(" %s", lib->GetFilePath().data());
        }
        if (!_whole_archive_libs.empty()) {
            _cmd += " -Wl,--whole-archive";
            for (auto lib : _whole_archive_libs) {
                _cmd += StringPrintf(" %s", lib->GetFilePath().data());
            }
            _cmd += " -Wl,--no-whole-archive";
        }
        if (_is_static_lib) {
             if (_conf) {
                 _cmd = _compiler + " " + _conf->ToString(DefaultStaticLibraryConfig()) + " " + _cmd;
             } else {
                 _cmd = _compiler + " " + DefaultStaticLibraryConfig()->ToString() + " " + _cmd;
             }
        } else {
            if (_conf) {
                _cmd += " " + _conf->ToString(DefaultSharedLibraryConfig());
            } else {
                _cmd += " " + DefaultSharedLibraryConfig()->ToString();
            }
        }
    }

    if (!_is_static_lib) {
        UpdateOptimizationLevel(_cmd);
    }
    return true;
}

ZBinary::ZBinary(const std::string& bin_name): ZFile("", FT_BINARY_FILE, true) {
    _name = bin_name;
    _file = GetBuildPath(bin_name);
}
ZBinary* ZBinary::AddObjs(const std::vector<std::string>& src_files, bool bind_flag) {
    for (auto src : src_files) AddObj(AccessObject(src, bind_flag ? GetObjBindName(src, _name) : ""));
    return this;
}
ZBinary* ZBinary::AddObj(ZFile* obj) {
    if (FT_OBJ_FILE != obj->GetFileType()) {
        ZTHROW("for binary(%s), '%s' is not an ZObject instance", FP(this), FP(obj));
    }
    auto f = (ZObject*)obj;
    f->SetFlags(_objs_flags);
    _objs.push_back(f);
    ZF::AddObjectUser(f, this);
    AddDep(f);
    return this;
}
const std::vector<ZObject*>& ZBinary::GetObjs() const {
    return _objs;
}
ZBinary* ZBinary::SetObjsFlags(const std::vector<std::string>& flags) {
    for (auto f : flags) _objs_flags.push_back(f);
    for (auto obj : _objs) obj->SetFlags(flags);
    return this;
}
ZBinary* ZBinary::AddLib(const std::string& lib_name, bool whole_archive) {
    return AddLib(AccessLibrary(lib_name), whole_archive);
}
ZBinary* ZBinary::AddLib(ZFile* lib, bool whole_archive) {
    if (FT_LIB_FILE != lib->GetFileType()) {
        ZTHROW("for binary(%s), this file(%s) is not an instance of ZLibrary", FP(this), FP(lib));
    }
    auto f = (ZLibrary*)lib;
    if (whole_archive) {
        if (!f->IsStaticLibrary()) {
            ZTHROW("for binary(%s), can't add shared lib(%s) in whole-archive way", FP(this), FP(lib));
        }
        _whole_archive_libs.push_back(f);
    } else _libs.push_back(f);
    AddDep(f);
    return this;
}
std::vector<ZLibrary*> ZBinary::GetLibs() const {
    auto result = _whole_archive_libs;
    for (auto f : _libs) result.push_back((ZLibrary*)f);
    return result;
}

ZBinary* ZBinary::AddLinkDir(const std::string& dir) {
    _link_dirs.push_back(fs::absolute(dir).lexically_normal());
    return this;
}
const std::vector<std::string>& ZBinary::GetLinkDirs() const {
    return _link_dirs;
}

bool ZBinary::ComposeCommand() {
    if ("" == _cmd) {
        _cmd = StringPrintf("%s -o %s", _compiler.data(), _file.data());
        for (auto obj : _objs) {
            _cmd += StringPrintf(" %s", obj->GetFilePath().data());
        }

        //TODO move flags like '-lpthread' to the end of _cmd
        std::set<ZFile*> uniq_deps;
        uniq_deps.insert(_whole_archive_libs.begin(), _whole_archive_libs.end());
        if (!_whole_archive_libs.empty()) {
            _cmd += " -Wl,--whole-archive";
            for (auto lib : _whole_archive_libs) {
                GetConfig()->Merge(lib->GetLinkConfig());
                _cmd += StringPrintf(" %s", lib->GetFilePath().data());
            }
            _cmd += " -Wl,--no-whole-archive";
        }

        for (const auto& dir : _link_dirs) _cmd += StringPrintf(" -L%s", dir.data());

        auto adjust_cmd_fn = [&](ZLibrary* lib) {
            GetConfig()->Merge(lib->GetLinkConfig());
            if (lib->IsStaticLibrary()) {
                if (lib->IsUsedAsWholeArchive()) {
                    _cmd += " -Wl,--whole-archive";
                    _cmd += StringPrintf(" %s", lib->GetFilePath().data());
                    _cmd += " -Wl,--no-whole-archive";
                } else {
                    _cmd += StringPrintf(" %s", lib->GetFilePath().data());
                }
            } else {
                _cmd += StringPrintf(" -L%s -l%s", lib->GetLinkDir().data(), lib->GetLinkLib().data());
            }
        };

        std::map<std::string, std::vector<ZLibrary*>> external_libs;
        std::vector<std::string> pkgs;
        std::vector<ZLibrary*> internal_libs;
        auto handle_lib_fn = [&](ZFile* f) {
            if (FT_LIB_FILE == f->GetFileType() && fs::exists(f->GetFilePath())) {
                auto lib = (ZLibrary*)f;
                if ('@' != lib->GetName().at(0)) internal_libs.push_back(lib);
                else {
                    auto pkg_name = StringSplit(lib->GetName(), '/')[0];
                    if (!external_libs.count(pkg_name)) pkgs.push_back(pkg_name);
                    external_libs[pkg_name].push_back(lib);
                }
            }
        };
        ProcessDepsRecursively(_libs, handle_lib_fn, &uniq_deps);
        for (auto lib : _whole_archive_libs) {
            ProcessDepsRecursively(lib->GetDeps(), handle_lib_fn, &uniq_deps);
        }
        ProcessDepsRecursively(GetDeps(), handle_lib_fn, &uniq_deps);

        for (auto iter = internal_libs.rbegin(); internal_libs.rend() != iter; ++iter) {
            adjust_cmd_fn(*iter);
        }
        for (auto iter = pkgs.rbegin(); pkgs.rend() != iter; ++iter) {
            auto& libs = external_libs[*iter];
            if (libs.size() > 1) _cmd += " -Wl,\"-(\"";
            for (auto lib : libs) adjust_cmd_fn(lib);
            if (libs.size() > 1) _cmd += " -Wl,\"-)\"";
        }

        if (_conf) {
            _cmd += " " + _conf->ToString(DefaultBinaryConfig());
        } else {
            _cmd += " " + DefaultBinaryConfig()->ToString();
        }
    }
    UpdateOptimizationLevel(_cmd);
    return true;
}

ZObject* ZProto::SpawnObj() {
    auto src_file_path = GetBuildPath(StringReplaceSuffix(_file, ".proto", ".pb.cc"));
    auto hdr_file_path = GetBuildPath(StringReplaceSuffix(_file, ".proto", ".pb.h"));
    auto obj = AccessObject(src_file_path);

    obj->AddDep(AccessFile(hdr_file_path));
    auto src_file = AccessFile(src_file_path);
    obj->AddDep(src_file);
    obj->AddIncludeDir(GetBuildPath(src_file->GetCwd()));
    //the locations of all generated *.pb.h are based on ${BUILD_ROOT_DIR}
    obj->AddIncludeDir(*AccessBuildRootDir());

    ProcessDepsRecursively(obj->GetDeps(), [&](ZFile* f) {
        //handle AccessProto("ps.proto")->AddDep(AccessProto("base.proto"))
        if (FT_PROTO_FILE == f->GetFileType() && this != f) {
            //using 'XXX.pb.cc' instead of 'XXX.pb.h' to trigger its generation through `protoc`
            //is used to save the opportunity for first invoking AccessFile('XXX.pb.h') so that
            //the 'CWD' can switch to the correct directory where 'XXX.pb.h' belongs.
            auto pb_src_file = AccessFile(GetBuildPath(StringReplaceSuffix(
                    f->GetFilePath(), ".proto", ".pb.cc")));
            obj->AddDep(pb_src_file);
            obj->AddIncludeDir(GetBuildPath(pb_src_file->GetCwd()));
        }
    });
    return obj;
}

ZProto::ZProto(const std::string& proto_file): ZFile(proto_file, FT_PROTO_FILE, true) {
    _file = fs::absolute(proto_file).lexically_normal();

    auto hdr_path = GetBuildPath(StringReplaceSuffix(proto_file, ".proto", ".pb.h"));
    auto hdr_file = AccessFile(hdr_path, true, FT_HEADER_FILE);
    ZF::UpdateGeneratedByDep(hdr_file, true);
    hdr_file->AddDep(this);

    auto src_path = GetBuildPath(StringReplaceSuffix(proto_file, ".proto", ".pb.cc"));
    auto src_file = AccessFile(src_path, true, FT_SOURCE_FILE);
    ZF::UpdateGeneratedByDep(src_file, true);
    src_file->AddDep(this);
}
bool ZProto::ComposeCommand() {
    if ("" == _cmd) {
        _cmd = StringPrintf("%s --cpp_out=%s", _compiler.data(), AccessBuildRootDir()->data());
        std::set<std::string> uniq_import_paths;
        //put -I${PROJECT_ROOT_DIR} at the first place, and the relative locations of all *.proto
        //are based on the ${PROJECT_ROOT_DIR}
        _cmd += " -I" + *AccessProjectRootDir();
        uniq_import_paths.insert(*AccessProjectRootDir());
        if (uniq_import_paths.insert(_cwd).second) _cmd += " -I" + _cwd;
        ProcessDepsRecursively(GetDeps(), [&](ZFile* dep) {
            if (FT_PROTO_FILE == dep->GetFileType() &&
                    uniq_import_paths.insert(dep->GetCwd()).second) {
                _cmd += " -I" + dep->GetCwd();
            }
        });
        for (auto dir : _proto_import_dirs) {
            if (uniq_import_paths.insert(dir).second) _cmd += " -I" + dir;
        }
        _cmd += " " + GetFilePath();
    }
    return true;
}
void ZProto::AddProtoImportDir(const std::string& dir) {
    _proto_import_dirs.push_back(dir);
}

ZObject* AccessObject(const std::string& src_file, const std::string& obj_file) {
    std::string new_obj_file = obj_file;
    if ("" != new_obj_file) new_obj_file = ConvertToProjectInnerPath(new_obj_file);
    std::string p_obj = ("" != new_obj_file) ? GetBuildPath(new_obj_file) :
            GetBuildPath(StringReplaceSuffix(src_file, C_CPP_SOURCE_SUFFIXES, ".o"));

    auto*& f = AccessFileInternal(p_obj);
    if (!f) f = (ZF::Create<ZObject>(src_file, new_obj_file))->AddDep(AccessFile(src_file));
    else if (FT_OBJ_FILE != f->GetFileType()) ZTHROW("'%s' is not an ZObject instance", FP(f));
    return (ZObject*)f;
}

ZLibrary* AccessLibrary(const std::string& lib_name, bool is_static_lib) {
    std::string name = FormalizeLibraryName(lib_name);
    auto*& f = AccessFileInternal(name);
    if (!f) {
        if ('@' == name.at(0)) {
            ZTHROW("the third-lib(%s) must be imported first before use", lib_name.data());
        }
        auto fn = GetFilenameFromPath(name);
        //Let "/prj/inner/path/XXX" and "/prj/inner/path/XXX/XXX" both point to the same ZFile
        if (StringEndWith(name, "/" + fn + "/" + fn)) {
            f = AccessFileInternal(StringReplaceSuffix(name, "/" + fn, ""));
        }
    }
    if (!f) f = ZF::Create<ZLibrary>(name, is_static_lib);
    else {
        if (FT_LIB_FILE != f->GetFileType()) ZTHROW("'%s' is not an ZLibrary instance", FP(f));
        //correct the library's cwd
        if (fs::current_path() != f->GetCwd()) {
            ZLibrary* lib = (ZLibrary*)f;
            if (std::string::npos == lib_name.find('/')) {
                ZF::UpdateCwd(lib, fs::current_path());
            } else {
                auto old_cwd = f->GetCwd();
                auto new_cwd = fs::current_path();
                auto p = fs::path(f->GetFilePath());
                auto rel_oc = p.lexically_relative(old_cwd);
                auto rel_nc = p.lexically_relative(new_cwd);
                if (!StringBeginWith(rel_nc.string(), "../")) {
                    if (StringBeginWith(rel_oc.string(), "../") ||
                            rel_nc.string().size() < rel_oc.string().size()) {
                        ZF::UpdateCwd(lib, new_cwd);
                    }
                }
            }
        }
    }
    return (ZLibrary*)f;
}
ZLibrary* ImportLibrary(const std::string& lib_name,
        const std::vector<std::string>& inc_dirs, const std::string& lib_file) {
    std::string name = FormalizeLibraryName(lib_name, true);
    auto*& f = AccessFileInternal(name);
    if (!f) {
        //don't check lib_file since a virtual lib that only records some deps might be imported
        for (auto inc_dir : inc_dirs) {
            if (!fs::exists(inc_dir)) ZTHROW("the include dir(%s) doesn't exist", inc_dir.data());
        }
        f = ZF::Create<ZLibrary>(name, inc_dirs, lib_file);
        if (*AccessDebugLevel() > 0) {
            printf("> import '%s' library, inc_dir:%s, lib:%s\n", name.data(),
                    StringCompose(inc_dirs).data(), lib_file.data());
        }
    } else {
        if (FT_LIB_FILE != f->GetFileType()) ZTHROW("'%s' is not an ZLibrary instance", FP(f));
        if ("" != lib_file && f->GetFilePath() != fs::absolute(lib_file).lexically_normal()) {
            ZTHROW("imported lib(%s) conflicts, lib_file: prev(%s) vs cur(%s)",
                    name.data(), FP(f), lib_file.data());
        }
    }
    return (ZLibrary*)f;
}
std::vector<ZLibrary*> ImportLibraries(const std::string& pkg_name, const std::string& dir) {
    std::string name = pkg_name;
    if ('@' == name.at(0)) name = name.substr(1);
    if ('/' == *name.rbegin()) name.pop_back();
    if (std::string::npos != name.find('/')) {
        ZTHROW("pkg_name(%s) should not contain '/' in the middle of it", pkg_name.data());
    }
    std::vector<ZLibrary*> result;
    const auto inc_dir = dir + "/include";
    const auto lib_dir = dir + "/lib";
    if (!fs::exists(lib_dir)) {
        ZTHROW("can't find 'lib' dir under %s, and please use ImportLibrary for "
                "header only lib", dir.data());
    }
    for (auto lib_file : ListFilesUnderDir(lib_dir, "^lib.*(\\.a|\\.so)$")) {
        if (StringEndWith(lib_file, ".so") &&
                fs::exists(StringReplaceSuffix(lib_file, ".so", ".a"))) {
            continue;
        }
        std::string lib_name = GetFilenameFromPath(lib_file).substr(3);
        lib_name = StringReplaceSuffix(lib_name, ".a|.so", "");
        result.push_back(ImportLibrary(name + "/" + lib_name, {inc_dir}, lib_file));
    }
    if (0 == result.size()) ZTHROW("there's no any library imported under %s", dir.data());
    else if (1 == result.size()) {
        auto fn = [&](const std::string& lib_name) {
            auto*& lib = AccessFileInternal(FormalizeLibraryName(lib_name, true));
            if (!lib) lib = result[0];
        };
        fn("@" + name + "/" + name);
        fn("@" + name + "/");
    }
    return result;
}
std::vector<ZLibrary*> DownloadLibraries(const std::string& pkg_name,
        const std::string& url, const std::string& compile_cmd, bool header_lib) {
    if ('@' == pkg_name.at(0)) {
        return DownloadLibraries(pkg_name.substr(1), url, compile_cmd, header_lib);
    }
    auto pkg_dir = *AccessBuildRootDir() + ".downloads/" + pkg_name;
    if (!fs::exists(pkg_dir + "/.done")) fs::remove_all(pkg_dir);
    else {
        if (!header_lib) return ImportLibraries(pkg_name, pkg_dir);
        else {
            auto lib = ImportLibrary(pkg_name, {pkg_dir + "/include"}, "");
            if (!lib) ZTHROW("found .done file, but import '%s' header lib from '%s' failed",
                    pkg_name.data(), pkg_dir.data());
            return {lib};
        }
    }
    auto cmd = StringPrintf("mkdir -p %s\n"
            "cd %s\n"
            "wget -q \"%s\"\n"
            "f=$(ls)\n"
            "tar zxf $f --no-same-owner || unzip $f\n"
            "rm -f $f\n"
            "f=$(ls)\n"
            "cd $f\n"
            "%s #compile cmd\n"
            "[ \"$?\" -ne 0 ] && exit $?\n"
            "cd ..\n"
            "rm -rf $f && touch .done", pkg_dir.data(), pkg_dir.data(),
            url.data(), "" != compile_cmd ? compile_cmd.data() :
                    "./configure --prefix=$(readlink -f ..) && make -j2 && make install");
    if (*AccessDebugLevel() > 0) {
        printf("> download '%s' libraries from '%s' using the script \n(%s)\n",
                pkg_name.data(), url.data(), cmd.data());
    }
    int ret_code = 0;
    ExecuteCmd(cmd, &ret_code);
    std::vector<ZLibrary*> libs;
    if (0 == ret_code) {
        if (!header_lib) libs = ImportLibraries(pkg_name, pkg_dir);
        else {
            auto lib = ImportLibrary(pkg_name, {pkg_dir + "/include"}, "");
            if (lib) libs.push_back(lib);
        }
    }
    if (libs.empty() || 0 != ret_code) {
        fs::remove(pkg_dir + "/.done");
        ZTHROW("download '%s' libraries from '%s' failed, ret_code:%d",
                pkg_name.data(), url.data(), ret_code);
    }
    return libs;
}
void ImportExternalZmakeProject(const std::string& ext_prj_name, const std::string& ext_prj_path) {
    std::string name = ext_prj_name;
    if ('@' != name.at(0)) name = "@" + name;
    if ('/' == *name.rbegin()) name.pop_back();
    std::string ext_prj_root = fs::absolute(ext_prj_path).lexically_normal();
    if ('/' == *ext_prj_root.rbegin()) ext_prj_root.pop_back();
    if (!fs::exists(ext_prj_root + "/BUILD.exe")) {
        ZTHROW("there's no BUILD.exe under project root dir(%s).", ext_prj_root.data());
    }
    auto build_libs_file = fs::read_symlink(ext_prj_root + "/BUILD.exe").replace_extension(".libs");
    if (!fs::exists(build_libs_file)) {
        ZTHROW("there's no BUILD.libs under this project(%s)", ext_prj_root.data());
    }
    auto build_libs = StringFromFile(build_libs_file.string());
    std::vector<std::pair<ZFile*, std::string>> dep_infos;
    for (auto& line : StringSplit(build_libs, '\n')) {
        if ('#' == line.at(0)) continue;
        const auto& lib_infos = StringSplit(line, '\t', true);
        if (3 != lib_infos.size() && 4 != lib_infos.size()) {
            fprintf(stderr, "[Warn]invalid line(%s) in %s/BUILD.libs\n",
                    line.data(), ext_prj_root.data());
            continue;
        }

        auto& lib_name = lib_infos[0];
        auto& lib_inc_dirs = lib_infos[1];
        auto& lib_file = lib_infos[2];
        if ('@' == lib_name.at(0)) {
            ImportLibrary(lib_name, StringSplit(lib_inc_dirs, ';'), lib_file);
        } else {
            auto lib = ImportLibrary(name + lib_name, StringSplit(lib_inc_dirs, ';'), lib_file);
            lib->AddIncludeDir(ext_prj_root);
            if (4 == lib_infos.size()) dep_infos.emplace_back(lib, lib_infos[3]);
            if (name == lib_infos[0].substr(1)) { //for "@gflags/gflags" case
                auto*& f = AccessFileInternal(FormalizeLibraryName(ext_prj_name, true));
                if (!f) f = lib;
            }
        }
    }
    for (auto& x : dep_infos) {
        for (auto& dep : StringSplit(x.second, ';')) {
            if ('@' == dep.at(0)) x.first->AddDepLibs({dep});
            else x.first->AddDepLibs({name + dep});
        }
    }
}

ZBinary* AccessBinary(const std::string& bin_name) {
    auto*& f = AccessFileInternal(bin_name);
    if (!f) f = ZF::Create<ZBinary>(ConvertToProjectInnerPath(bin_name));
    else if (FT_BINARY_FILE != f->GetFileType()) ZTHROW("'%s' is not an ZBinary instance", FP(f));
    return (ZBinary*)f;
}

ZFile* AccessFile(const std::string& file, bool need_build, FileType ft) {
    return AccessFileInternal(file, true, need_build, ft);
}

ZProto* AccessProto(const std::string& proto_file) {
    auto*& f = AccessFileInternal(proto_file);
    if (!f) f = new ZProto(proto_file);
    else if (FT_PROTO_FILE != f->GetFileType()) ZTHROW("'%s' is not an ZProto instance", FP(f));
    return (ZProto*)f;
}

auto& GlobalInstallTargets() {
    static std::unordered_map<std::string,
        std::vector<std::pair<std::string, FSCO>>> s_install_targets;
    return s_install_targets;
}

auto& GlobalTargets() {
    static std::set<ZFile*> s_targets;
    return s_targets;
}

void ConcurrentBuild(std::vector<ZFile*> files, int thread_num = -1) {
    TaskRunnerPool thread_pool(thread_num, true);

    std::set<ZFile*> built_ok_deps;
    std::mutex mtx;
    std::function<void(ZFile*, const std::shared_ptr<ZFile>&)> build_func;
    build_func = [&](ZFile* file, const std::shared_ptr<ZFile>& base) {
        {
            std::lock_guard<std::mutex> guard(mtx);
            if (built_ok_deps.count(file)) return;
        }
        std::shared_ptr<ZFile> deps_build_done{file,
            [&thread_pool, base, &built_ok_deps, &mtx](ZFile* file) {
            thread_pool.AddTask([file, base, &built_ok_deps, &mtx](std::string* task_sign) {
                if (task_sign) *task_sign = file->GetFilePath();
                else {
                    file->Build();
                    RunWithLock(mtx, [&]() { built_ok_deps.insert(file); });
                }
            });
        }};
        for (auto dep : file->GetDeps()) build_func(dep, deps_build_done);
    };

    std::packaged_task<void()> ptask([]() {});
    {
        std::shared_ptr<ZFile> done(nullptr, [&ptask](ZFile* file) { ptask(); });
        for (auto f : files) build_func(f, done);
    }
    ptask.get_future().wait();
}

ZFile* AddTarget(const std::string& name) {
    auto f = AccessFileInternal(name);
    if (!f) ZTHROW("can't find a target that has been defined by this name(%s)", name.data());
    AddTarget(f);
    return f;
}

void AddTarget(ZFile* file) {
    auto& targets = GlobalTargets();
    if (!targets.insert(file).second) {
        fprintf(stderr, "[Warn]this target has already been added before.");
    }
}

template <typename T>
std::map<std::string, T*> ListFiles(const std::string& dir) {
    std::string prefix_dir = ConvertToProjectInnerPath(dir);
    if ('/' != *prefix_dir.rbegin()) prefix_dir += "/";
    prefix_dir += "|" + GetBuildPath(prefix_dir); //use build path for obj

    std::map<std::string, T*> result;
    for (const auto& x : GlobalFiles()) {
        auto f = dynamic_cast<T*>(x.second);
        if (!f) continue;
        if (StringBeginWith(x.first, prefix_dir) ||
                StringBeginWith(f->GetFilePath(), prefix_dir)) result[x.first] = f;
    }
    return result;
}

void BuildAll(bool export_libs, int concurrency_num) {
    for (auto runner : GlobalRBB()) runner();
    std::vector<ZFile*> files(GlobalTargets().begin(), GlobalTargets().end());
    if (files.empty()) {
        for (auto x : GlobalFiles()) {
            if (!x.second) continue;
            if (FT_LIB_FILE == x.second->GetFileType() ||
                    FT_BINARY_FILE == x.second->GetFileType()) {
                files.push_back(x.second);
            }
        }
    }
    if (1 == concurrency_num) for (auto f : files) f->Build();
    else ConcurrentBuild(files, concurrency_num);
    for (auto runner : GlobalRAB()) runner();

    ProcessDepsRecursively(files, [](ZFile* f) {
        if (fs::exists(f->GetFilePath())) Md5Cache::Get(f->GetFilePath(), false);
    });
    std::ostringstream md5s_oss;
    for (auto& x : Md5Cache::GetAll()) {
        md5s_oss << x.first << " ";
        if ('@' == x.second.at(0) || '*' == x.second.at(0)) {
            md5s_oss << x.second.substr(1) << std::endl;
        } else md5s_oss << x.second << std::endl;
    }
    StringToFile(md5s_oss.str(), GetBuildPath("BUILD.md5s"));

    if (!export_libs) return;

    const std::string build_root_dir = *AccessBuildRootDir();
    std::ostringstream oss;
    std::set<std::string> uniq_imported_libs;
    std::ostringstream imported_libs_oss;
    oss << "#format: lib_name \t lib_include_dirs \t [lib_file \t [deps]]" << std::endl
        << "#using ';' as the separator for lib_include_dirs and deps"     << std::endl;
    for (auto& x : ListFiles<ZLibrary>("/")) {
        auto p = x.second->GetFilePath();
        if (!StringBeginWith(GetDirnameFromPath(p), build_root_dir)) {
            fprintf(stderr, "[Warn]this lib target(%s) is out of build root dir(%s)\n",
                    p.data(), build_root_dir.data());
            continue;
        }
        oss << x.first << "\t" << StringCompose(x.second->GetIncludeDirs()) << "\t" << p << "\t";
        std::set<std::string> uniq_deps;
        for (auto dep : x.second->GetDeps()) {
            if (FT_LIB_FILE != dep->GetFileType()) continue;
            auto lib = (ZLibrary*)dep;
            std::string dep_name = lib->GetName();
            if ('@' == dep_name.at(0)) {
                if (uniq_imported_libs.insert(dep_name).second) {
                    imported_libs_oss << dep_name << "\t" << StringCompose(lib->GetIncludeDirs())
                            << "\t" << lib->GetFilePath() << std::endl;
                }
                dep_name = StringSplit(dep_name, '/')[0] + "/";
            }
            if (uniq_deps.insert(dep_name).second) {
                oss << (uniq_deps.size() > 1 ? ";" : "") << dep_name;
            }
        }
        oss << std::endl;
    }
    StringToFile(oss.str() + imported_libs_oss.str(), GetBuildPath("BUILD.libs"));
    //TODO analyze files that haven't built done yet from GlobalFiles()
}

void InstallAll() {
    for (const auto& x : GlobalInstallTargets()) {
        for (const auto& dst : x.second) {
            if (FSCO::none != (dst.second & FSCO::create_symlinks)) fs::remove(dst.first);
            fs::copy(x.first, dst.first, dst.second);
        }
    }
}

template <typename T>
std::vector<T*> ListTargets(const std::string& dir) {
    std::vector<T*> result;
    std::set<T*> uniq_res;
    for (auto& x : ListFiles<T>(fs::exists(dir) ? fs::canonical(dir).string() : dir)) {
        if (uniq_res.insert(x.second).second) result.push_back(x.second);
    }
    return result;
}
std::vector<ZObject*> ListObjectTargets(const std::string& dir) {
    return ListTargets<ZObject>(dir);
}
std::vector<ZLibrary*> ListLibraryTargets(const std::string& dir) {
    return ListTargets<ZLibrary>(dir);
}
std::vector<ZBinary*> ListBinaryTargets(const std::string& dir) {
    return ListTargets<ZBinary>(dir);
}
std::vector<ZFile*> ListAllTargets(const std::string& dir) {
    return ListTargets<ZFile>(dir);
}

void RegisterTargetInstall(const std::string& name, const std::string& dst_path, FSCO opts) {
    auto f = AccessFileInternal(name);
    if (!f) ZTHROW("install failed, can't find the target(%s)", name.data());
    RegisterTargetInstall(f, dst_path, opts);
}
void RegisterTargetInstall(ZFile* file, const std::string& dst_path, FSCO opts) {
    GlobalInstallTargets()[file->GetFilePath()].push_back({dst_path, opts});
}

void RegisterRunnerBeforeBuildAll(std::function<void()> runner) {
    GlobalRBB().push_back(std::move(runner));
}
void RegisterRunnerAfterBuildAll(std::function<void()> runner) {
    GlobalRAB().push_back(std::move(runner));
}

} //end of namespace zmake
