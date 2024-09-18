/*
 * BUILD_main.cpp
 *
 *  Created on: 22 Feb 2024
 *      Author: yanbin.zhao
 */

#include "zmake_helper.h"

using namespace zmake;
namespace fs = std::filesystem;

namespace zmake {
    extern std::string GetBuildPath(const std::string& path);
    extern void ProcessDepsRecursively(const std::vector<ZFile*>& deps,
            const std::function<void(ZFile*)>& fn, std::set<ZFile*>* uniq_deps = nullptr);
    struct ZF {
        static void ProcessObjectUsers(ZObject* obj) {
            ProcessDepsRecursively(obj->_users, [&](ZFile* dep) {
                if (FT_LIB_FILE == dep->GetFileType()) AddTarget(dep);
            });
        }
    };
}

int main(int argc, char* argv[]) {
    CommandArgs::Init(argc, argv);
    if (CommandArgs::Has("-h")) {
        PrintHelpInfo();
        return 0;
    }
    if (std::string("./BUILD.exe") != argv[0] && fs::path(fs::current_path().append("BUILD.exe")).string() != argv[0]) {
//    if (!fs::equivalent(fs::path(argv[0]), fs::current_path().append("BUILD.exe"))) {
        fprintf(stderr, "[Error]please run ./BUILD.exe under the directory where the 'BUILD.exe' binary file is\n");
        return 1;
    }

    auto prj_root = *AccessProjectRootDir();
    auto build_root = *AccessBuildRootDir();

    SetVerboseMode(CommandArgs::Has("-v"));
    if (CommandArgs::Has("-d")) {
        int debug_level = 1;
        try { debug_level = CommandArgs::Get<int>("-d"); } catch (...) {}
        SetDebugLevel(debug_level);
    }

    if (CommandArgs::Has("-g")) {
        DefaultObjectConfig()->SetFlag("-g");
        DefaultSharedLibraryConfig()->SetFlag("-g");
        DefaultBinaryConfig()->SetFlag("-g");
    }

    for (auto x : BuilderBase::GlobalBuilders()) {
        auto old_cwd = fs::current_path();
        const std::string prj_inner_path = fs::path(x.first).parent_path().lexically_relative(build_root);
        fs::path p(prj_root + prj_inner_path);
        fs::current_path(p);

        auto builder = (x.second)();
        ColorPrint(StringPrintf("* Start to analyze targets under the directory %s\n", p.string().data()), CT_BRIGHT_CYAN);
        builder->Run();
        delete builder;

        fs::current_path(old_cwd);
    }

    if (CommandArgs::Has("-A")) {
        auto t = CommandArgs::Get<std::string>("-A", "");
        ZFile* f = AccessFile(t);
        if (!f) f = AccessFile(GetBuildPath(t));
        if (!f) ZTHROW("can't find the target '%s'", t.data());
        f->DumpDepsRecursively();
        return 0;
    }

    for (auto t : StringSplit(CommandArgs::Get<std::string>("-t", ""), ';')) {
        ZFile* f = nullptr;
        if (StringEndWith(t, ".o") && '/' != *t.rbegin()) f = AddTarget(GetBuildPath(t));
        else f = AddTarget(t);
        if (StringEndWith(t, C_CPP_SOURCE_SUFFIXES)) {
            f = AddTarget(GetBuildPath(StringReplaceSuffix(t, C_CPP_SOURCE_SUFFIXES, ".o")));
        }
        if (FT_OBJ_FILE == f->GetFileType()) ZF::ProcessObjectUsers((ZObject*)f);
    }

    if (CommandArgs::Has("-b")) {
        for (auto t : ListAllTargets(CommandArgs::Get<std::string>("-b", "."))) AddTarget(t);
    }

    ColorPrint("* Start to build all targets\n", CT_BRIGHT_CYAN);
    BuildAll(CommandArgs::Has("-e"), CommandArgs::Get<int>("-j", -1));
    ColorPrint("* Start to install all targets\n", CT_BRIGHT_CYAN);
    InstallAll();
    return 0;
}


