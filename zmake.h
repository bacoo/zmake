#include <string>
#include <vector>
#include <map>
#include <set>
#include <functional>
#include <filesystem>
#include <unordered_map>

#define C_CPP_SOURCE_SUFFIXES ".cpp|.cc|.c|.cxx|.CPP|.CC|.C|.CXX"
#define C_CPP_HEADER_SUFFIXES ".h|.hh|.hpp|.hxx|.H|.HH|.HPP|.HXX"

namespace zmake {

enum FileType {
    FT_NONE = 0,
    FT_NORMAL_FILE = 1,
    FT_HEADER_FILE = 2,
    FT_SOURCE_FILE = 3,
    FT_PROTO_FILE = 4,
    FT_OBJ_FILE = 5,
    FT_LIB_FILE = 6,
    FT_BINARY_FILE = 7,
};

struct ZConfig;
struct ZFile;
struct ZObject;
struct ZLibrary;
struct ZBinary;
struct ZProto;
struct ZGenerator;

//suffix   default       description
// .cc      g++            src file
// .cpp     g++            src file
// .c       gcc            src file
// .a       ar             static lib's arvhive
// .so      g++            shared lib
// .proto   protoc         proto file
// .cu      nvcc           cuda src file
// ""       g++            binary's link
//you can adjust the default compiler by this API, such as:
//*AccessDefaultCompiler(".cc") = "clang++";
//*AccessDefaultCompiler(".proto") = "/workspace/protobuf-3.6.1
//if you just want to adjust the compiler for one specific file, you can get the 'cmd' by
//GetFullCommand(), modify the compiler in 'cmd', and update it by SetFullCommand(...)
std::string* AccessDefaultCompiler(const std::string& suffix);

ZConfig* DefaultObjectConfig();        //configs for obj's compilation
ZConfig* DefaultStaticLibraryConfig(); //configs for static-lib's archive
ZConfig* DefaultSharedLibraryConfig(); //configs for shared-lib's link
ZConfig* DefaultBinaryConfig();        //configs for binary's link

//path indicates the way to find source files; it can be a specific source file, or
//the filename part(the last part in path) could use glob for matches, such as:
//  1. "dir1/*.cpp": all direct cpp files under "dir1/";
//  2. "dir1/abc_*.cpp": all direct cpp files that start with 'abc_' under "dir1/";
//  3. "dir1/**.cpp": all cpp files under "dir1/" recursively,
//      for '**' usage, we don't support 'abc_**.cpp';
void SetObjsFlags(const std::vector<std::string>& paths, const std::vector<std::string>& flags);
//the usages of 'rule' and 'exclude_rules' are the same as the 'path' of SetObjectConfig
std::vector<std::string> Glob(const std::vector<std::string>& rules,
        const std::vector<std::string>& exclude_rules = {}, const std::string& dir = ".");

//create or get object/library/binary
//the 'src_file', 'lib_name' or 'bin_name' should be unique globally
//such as:
//  src_file: "dir1/dir2/curl.cpp", which must be a real path that we can find this source file;
//            you can also use "dir2/curl.cpp" in "dir1/BUILD.cpp", or use the absolute path
//            anywhere, they all access the same object.
//  obj_file: by default, the obj file will be generated under GetBuildRootDir() and its
//            filename could be deduced by replacing the suffix of 'src_file' with '.o',
//            but sometimes you need to provide the 'obj_file' specifically, for example,
//            two objs compiled from one 'src_file' based on with '-fPIC' flags or not.
//            by the way, you just need to provide its filename(not including '/').
//  lib_name: for example, the file structure is "${project_root}/dir1/dir2/curl.cc", then
//            the lib_name might be:
//              "curl" in dir1/dir2/BUILD.cpp
//              "dir2/curl" in dir1/BUILD.cpp
//              "/dir1/dir2/curl" in any BUILD.cpp
//              what's more, the libcurl.a will be generated under GetBuildRootDir()/dir1/dir2/
//            for accessing imported libraries, the 'lib_name' must start with '@', such as:
//              "@gflags" if there's one lib named "libgflags.a" under this third-party lib
//              "@boost/boost_regex"
//            for compatibility with bazel, you can use bazel rules and they will be converted like:
//              "//service:metric" -> "/service/metric"
//              "//:metric" -> "/metric"
//              ":metric" -> "metric"
//              "@gflags//:gflags" -> "@gflags/gflags"
//  bin_name: "rpc_replay" or "tools/rpc_replay"
ZObject*  AccessObject(const std::string& src_file, const std::string& obj_file = "");
//if this lib already exists, 'is_static_lib' will be ignored, which is only used to create this lib;
//you can also access the imported lib, such as using '@gflags' as the 'lib_name'.
ZLibrary* AccessLibrary(const std::string& lib_name, bool is_static_lib = true);
ZBinary*  AccessBinary(const std::string& bin_name); //such as: "rpc_replay" or "tools/rpc_replay"
//for other cases, you might need create/get just a ZFile*, for example, you can create a file that
//doesn't need to be built, but add some dependencies for it, then any target can rely on it:
//  AccessFile("third_lib_pkg")->AddDepLibs({"@brpc", "@gflags", "@boost"});
//  AccessLibrary("service_core")->AddDep("third_lib_pkg");
//  AccessBinary("dispatcher_service")->AddDep("third_lib_pkg");
ZFile*    AccessFile(const std::string& file, bool need_build = false, FileType ft = FT_NONE);
ZProto*   AccessProto(const std::string& proto_file);
ZLibrary* ImportLibrary(const std::string& lib_name, const std::vector<std::string>& inc_dirs,
        const std::string& lib_file);
//import all libraries under $dir/ for this package; the 'pkg_name' should not include '/', and
//the inc_dir will be "$dir/include/", and all libs(filenames conform to "^lib.*(\.a|\.so)$") under
//"$dir/lib/" will be imported; if there are both libcurl.a and libcurl.so, libcurl.a will be used;
//if there's only one library under "$dir/lib/", e.g. libcurl.a, it will be accessed with "@curl";
//if there are more than one library, such as libcurl_main.a, libcurl_net.a, then they can be accessed
//with "@curl/curl_main" and "@curl/curl_net" respectively.
std::vector<ZLibrary*> ImportLibraries(const std::string& pkg_name, const std::string& dir);
//by default, use following shell script to compile, and you can replace it by 'compile_cmd':
//  ./configure --prefix=$(readlink -f ..) && make -j && make -j install
std::vector<ZLibrary*> DownloadLibraries(const std::string& pkg_name, const std::string& url,
        bool need_compile = false, const std::string& compile_cmd = "");

//if you have another project built by zmake, you can easily import it by this API, such as
//  ImportExternalZmakeProject("common_utils", "/workspace/common_utils/");
//  make sure the file 'BUILD.libs' is under "/workspace/common_utils/", and then you can use it:
//  AccessLibrary("service_core")
//    ->AddDepLibs({"@common_utils/net", "@common_utils/string"});
void ImportExternalZmakeProject(const std::string& ext_prj_name, const std::string& ext_prj_path);
//TODO support BuildExternalZmakeProject(const std::string& ext_prj_path);

//name could be src_file/lib_name/bin_name or a file that could be generated
ZFile* AddTarget(const std::string& name);
void   AddTarget(ZFile* file);

void RegisterTargetInstall(const std::string& name, const std::string& dst_path,
        std::filesystem::copy_options opts = std::filesystem::copy_options::overwrite_existing);
void RegisterTargetInstall(ZFile* file, const std::string& dst_path,
        std::filesystem::copy_options opts = std::filesystem::copy_options::overwrite_existing);

//by default, current directory will be used if dir is ""
std::vector<ZObject*>  ListObjectTargets(const std::string& dir = ".");
std::vector<ZLibrary*> ListLibraryTargets(const std::string& dir = ".");
std::vector<ZBinary*>  ListBinaryTargets(const std::string& dir = ".");
std::vector<ZFile*>    ListAllTargets(const std::string& dir = ".");

//return the reference, so you can modify the root dir
std::string* AccessProjectRootDir(); //the dir where you run ./BUILD
std::string* AccessBuildRootDir(); //by default, it's *AccessProjectRootDir()/.zmade/

//the stdout of zmake can be classified based on the first character:
//  *: the main stage of zmake
//  @: build a target
//  #: the build cmd
//  >: debug info
void SetVerboseMode(bool verbose = true);
void SetDebugLevel(uint32_t level = 1); //current only support 1 or 2
//if no target is added by AddTarget, all targets will be built;
//if 'export_libs' is true, the 'BUILD.libs' file will be generated under build root dir, which
//will be used by other projects when current project plays as an external project.
void BuildAll(bool export_libs = false, int concurrency_num = -1);
void InstallAll();

void RegisterRunnerBeforeBuildAll(std::function<void()> runner);
void RegisterRunnerAfterBuildAll(std::function<void()> runner);

void RegisterDefaultGenerator(const std::string& suffix, const ZGenerator& g);
ZGenerator* GetDefaultGenerator(const std::string& suffix);

//format: key=value, and the order of flags will be kept in terms of the sequence of SetFlag(s)
struct ZConfig {
    const std::unordered_map<std::string, std::string>& GetFlags() const;
    ZConfig* SetFlag(const std::string& flag);
    ZConfig* SetFlags(const std::vector<std::string>& flags);
    bool HasFlag(const std::string& flag_name) const;
    std::string GetFlag(const std::string& flag_name) const;
    std::string ToString(ZConfig* default_conf = nullptr) const;
    void Merge(const ZConfig& other, bool prior_other = false);
    bool Empty() const;

private:
    std::vector<std::string> _flag_names;
    std::unordered_map<std::string, std::string> _flags;
};

struct ZFile {
    virtual ~ZFile();

    ZFile* SetGenerator(const ZGenerator& g);
    ZGenerator* GetGenerator() const;

    ZConfig* GetConfig();
    void SetConfig(const ZConfig& conf);
    ZFile* SetFlag(const std::string& flag);
    ZFile* SetFlags(const std::vector<std::string>& flags);

    //you can also specify the extra dependency explicitly(like XXX.proto), so zmake can
    //watch these files' changes and decide whether recompile or not;
    ZFile* AddDep(ZFile* dep);
    ZFile* AddDep(const std::string& dep);
    const std::vector<ZFile*>& GetDeps() const;
    //dump to stdout if dump_sinker is nullptr
    void DumpDepsRecursively(std::string* dump_sinker = nullptr) const;
    //only use this API to add dependent libraries, and support '*' as the last character
    //to match all libs under path, such as:
    //  "@boost/*", which is equal to "@boost" or "@boost/"
    //  "/service/*" or "/service/"
    ZFile* AddDepLibs(const std::vector<std::string>& dep_libs);

    //normally, you should not set the full cmd for this file directly; if cmd is set, all
    //configs will be ignored, and should do it after all targets have been defined fully,
    //so we suggest you get/set full cmd by adding a runner in RegisterRunnerBeforeBuildAll,
    //such as: RegisterRunnerBeforeBuildAll([&]() { ... });
    void SetFullCommand(const std::string& cmd);
    std::string GetFullCommand(bool print_pretty = false);

    //absolute path for the target file, such as ".o", ".a", ".so" or binary
    std::string GetFilePath() const;
    FileType GetFileType() const;
    std::string GetCwd() const;
    std::string GetName() const { return _name; }

    //build this obj/library/binary, or generate the file based on its generator
    //return whether the build process really occurs or not
    bool Build();

    //add this file as a target, this API equals to: AddTarget(this);
    void BeTarget();

protected:
    ZFile(const std::string& path, FileType ft, bool need_build);
    virtual bool ComposeCommand();

    std::string _file;
    std::string _name;
    std::string _compiler = "";
    FileType _ft = FT_NONE;
    std::string _cmd;
    std::string _cwd;
    ZConfig* _conf = nullptr;
    ZGenerator* _generator = nullptr;
    std::set<std::string> _uniq_deps;
    std::vector<ZFile*> _deps;
    bool _build_done = false;
    bool _has_been_built = false;
    bool _forced_build = false;
    bool _generated_by_dep = false;

    friend class ZF; //Z* Friend
};

struct ZObject : public ZFile {
    ZObject* AddIncludeDir(const std::string& dir);
    std::vector<std::string> GetIncludeDirs() const;

    std::string GetSourceFile() const;

protected:
    ZObject(const std::string& src_file, const std::string& obj_file = "");
    void AddObjectUser(ZFile* file); //file is a library or binary
    virtual bool ComposeCommand();

    std::vector<std::string> _inc_dirs;
    std::set<std::string> _uniq_inc_dirs;
    std::string _src;
    std::vector<ZFile*> _users;

    friend class ZF; //Z* Friend
};

struct ZLibrary : public ZFile {
    //normally, the object filename of "file1.cpp" is "file1.o", but if 'bind_flag' is true, this
    //object file will be bound with this lib and object filename will be adjusted; for example,
    //lib name is "/util/hash_test", then the object filename is "file1-util-hash_test.o"; it's
    //quite useful if you want to compile another version of these source files with some specific
    //macro definitions, such as -DDEBUG for test, use a simple way to do it like this:
    //  AccessLibrary("/util/hash")->AddObjs({"*.cpp"});
    //  AccessLibrary("/util/hash_test")->AddObjs({"*.cpp"}, true)->SetObjsFlags({"-DTEST_MODE"});
    ZLibrary* AddObjs(const std::vector<std::string>& src_files, bool bind_flag = false);
    ZLibrary* AddObj(ZFile* obj);
    ZLibrary* AddProto(const std::string& proto_file);
    ZLibrary* AddProtos(const std::vector<std::string>& proto_files);
    const std::vector<ZObject*>& GetObjs() const;
    //TODO support ZLibrary* SetObjsFlags(const std::vector<std::string>& flags, bool pass_to_deps = false);
    ZLibrary* SetObjsFlags(const std::vector<std::string>& flags);
    ZLibrary* SetLinkFlags(const std::vector<std::string>& flags) { _link_conf.SetFlags(flags); return this; }
    const ZConfig& GetLinkConfig() const { return _link_conf; }

    //this API is just used for shared library, and can only add static library;
    ZLibrary* AddLib(ZFile* lib, bool whole_archive = false);
    std::vector<ZLibrary*> GetLibs() const;

    //if this library is used as a dependence for others, or it's a third-party lib,
    //it should provide the inc_dirs and itself as the referenced lib;
    //if you don't specify it clearly, the current dir will be used.
    const std::set<std::string>& GetIncludeDirs();
    //if 'create_alias_name' is true, create a soft link like this:
    //  ln -s _cwd 'dir'
    //'dir' should provide the alias name as the prefix of include path;
    //it'll be useful to provide a meaningful include path, for example, header files are like 'src/XXX.h',
    //with the help of AddIncludeDir("foobar/include", true), it can be included with "foobar/include/XXX.h";
    ZLibrary* AddIncludeDir(const std::string& dir, bool create_alias_name = false);
    std::string GetLinkDir() const;
    std::string GetLinkLib() const;
    bool IsStaticLibrary() const;

    bool IsUsedAsWholeArchive() const { return _is_whole_archive; }
    ZLibrary* SetUsedAsWholeArchive() { _is_whole_archive = true; return this;}

protected:
    ZLibrary(const std::string& lib_name, bool is_static_lib);
    ZLibrary(const std::string& lib_name, const std::vector<std::string>& inc_dirs, const std::string& lib_file);
    virtual bool ComposeCommand();

    bool _is_static_lib = true;
    bool _is_whole_archive = false;
    bool _added_protobuf_lib_dep = false;
    std::vector<ZObject*> _objs;
    std::vector<std::string> _objs_flags;
    std::vector<ZLibrary*> _libs;
    std::vector<ZLibrary*> _whole_archive_libs;
    std::set<std::string> _inc_dirs;
    ZConfig _link_conf;

    friend class ZF; //Z* Friend
};

struct ZBinary : public ZFile {
    //refer to ZLibrary::AddObjs
    ZBinary* AddObjs(const std::vector<std::string>& src_files, bool bind_flag = false);
    ZBinary* AddObj(ZFile* obj);
    const std::vector<ZObject*>& GetObjs() const;
    ZBinary* SetObjsFlags(const std::vector<std::string>& flags);

    //add static libraries only;
    //for shared libraries, they can be added by ZConfig, such as '-lpthread'
    ZBinary* AddLib(const std::string& lib_name, bool whole_archive = false);
    ZBinary* AddLib(ZFile* lib, bool whole_archive = false);
    std::vector<ZLibrary*> GetLibs() const;

    ZBinary* AddLinkDir(const std::string& dir);
    const std::vector<std::string>& GetLinkDirs() const;

protected:
    ZBinary(const std::string& bin_name);
    virtual bool ComposeCommand();

    std::vector<ZObject*> _objs;
    std::vector<std::string> _objs_flags;
    std::vector<ZFile*> _libs;
    std::vector<ZLibrary*> _whole_archive_libs;
    std::vector<std::string> _link_dirs;

    friend class ZF; //Z* Friend
};

//please notice the weird and disgusting thing for `protoc`. firstly, quote the message from `protoc`:
//  Note that the proto_path must be an exact prefix of the .proto file names -- protoc is too dumb to
//  figure out when two paths (e.g. absolute and relative) are equivalent (it's harder than you think).
//
//if you use 'protoc --cpp_out=. -I/prj_root /prj_root/proto/common/base.proto',
//then 'namespace protobuf_proto_2fcommon_2fbase_2eproto' will be generated in base.pb.h;
//but if you use 'protoc --cpp_out=. -I/prj_root/proto /prj_root/proto/common/base.proto',
//then 'namespace protobuf_common_2fbase_2eproto' will be generated in base.pb.h;
//
//for other proto files that depend on base.proto, for example:
//in '/prj_root/proto/core/server.proto', we use the following way to import base.proto:
//import "proto/common/base.proto";
//then server.pb.cc will need 'namespace protobuf_proto_2fcommon_2fbase_2eproto';
//what's more, if multiple proto files use different ways to import base.proto, such as:
//  import "proto/common/base.proto"; //used in server.proto
//  import "common/base.proto"; //used in client.proto
//this will definitely conflict and make an error.
//
//so the conclusion is:
//you must compile base.proto in the way that conforms to the way it's imported in server.proto.
//for best practice, we suggest all proto files are under /prj_root/proto/, and all imports use the
//full path starting from "proto/...", such as "proto/common/base.proto" or "proto/core/server.proto".

struct ZProto : public ZFile {
    ZObject* SpawnObj();
    void AddProtoImportDir(const std::string& dir);

protected:
    ZProto(const std::string& proto_file);
    virtual bool ComposeCommand();

    friend ZProto* AccessProto(const std::string&);
    std::vector<std::string> _proto_import_dirs;
};

//provide a way to generate some required files,
//such as: .thrift | .proto
struct ZGenerator {
    //use ${1}, ${2}, ... , ${n} as the param placeholder
    //for example, the 'rule' is like this:
    //  protoc -I${1} --cpp_out=. ${2}
    //  then you need to provide 2 input params for 'Generate' function.
    //
    //currently, we only support one param for the input file.
    ZGenerator(const std::string& rule = "");

    void SetRule(const std::string& rule);
    std::string GetRule() const;

    //return the cmd used to generate file
    std::string Generate(const std::vector<std::string>& inputs);

protected:
    std::string _rule;
};

} //end of namespace zmake
