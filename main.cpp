/*
 * main.cpp
 *
 *  Created on: 21 Feb 2024
 *      Author: yanbin.zhao
 */

#include "zmake_helper.h"

using namespace zmake;
namespace fs = std::filesystem;

namespace zmake {
    extern std::string GetBuildPath(const std::string& path);
}

int main(int argc, char* argv[]) {
    CommandArgs::Init(argc, argv);
    if (CommandArgs::Has("-h")) {
        PrintHelpInfo();
        return 0;
    }
#ifdef __MACH__
    std::string zmake_dir = "$(dirname $(which zmake))";
#else
    std::string zmake_dir = std::filesystem::read_symlink("/proc/self/exe").parent_path();
#endif
    auto zmake_include_dir = zmake_dir + "/zmake_files/include/";
    auto zmake_lib_dir = zmake_dir + "/zmake_files/lib/";
    DefaultObjectConfig()->SetFlags({
        "-std=c++17",
        "-idirafter " + zmake_include_dir,
        "-g",
        "-D_GLIBCXX_DEBUG",
    });
    DefaultBinaryConfig()->SetFlags({
        "-L" + zmake_lib_dir,
        "-lzmake",
        "-g",
        "-Wl,-no-as-needed -lpthread -Wl,-as-needed",
    });
    if (CommandArgs::Has("-O")) {
        DefaultObjectConfig()->SetFlag(StringPrintf("-O%d", CommandArgs::Get<int>("-O")));
        DefaultBinaryConfig()->SetFlag(StringPrintf("-O%d", CommandArgs::Get<int>("-O")));
    }

    auto build_root = *AccessBuildRootDir();

    //analyze external zmake projects under current dir
    std::vector<std::string> external_prjs;
    for (auto f : ListFilesUnderDir(fs::current_path(), "^BUILD.exe$", true, true)) {
        if (!fs::is_symlink(f) || build_root + "BUILD.exe" == fs::read_symlink(f)) continue;
        auto libs_file = StringReplaceSuffix(fs::read_symlink(f), ".exe", ".libs");
        if (fs::exists(libs_file)) external_prjs.push_back(GetDirnameFromPath(f));
    }

    auto is_external_file_fn = [&](const std::string& f) {
        for (auto x : external_prjs) if (StringBeginWith(f, x)) return true;
        return false;
    };

    //create symbol link under *AccessBuildRootDir() for BUILD.cpp
    for (auto f : ListFilesUnderDir(fs::current_path(), "^BUILD.cpp$", true, true)) {
        if (StringBeginWith(f, build_root) || is_external_file_fn(f)) continue;
        fs::remove(GetBuildPath(f));
        fs::copy(f, GetBuildPath(f), fs::copy_options::create_symlinks);
    }

    //convert BUILD.inc to BUILD.cpp under *AccessBuildRootDir()
    bool has_workspace_header = fs::exists("WORKSPACE.h");
    for (auto f : ListFilesUnderDir(fs::current_path(), "^BUILD.inc$", true, true)) {
        if (StringBeginWith(f, build_root) || is_external_file_fn(f)) continue;
        auto cpp_file = GetBuildPath(StringReplaceSuffix(f, ".inc", ".cpp"));
        if (std::filesystem::exists(cpp_file)) continue;
        auto str = StringPrintf(
"#include \"zmake_helper.h\"\n" \
"using namespace zmake;\n"      \
"%s"                            \
"\n"                            \
"BUILD() {\n"                   \
"#include \"%s\"\n"             \
"}", (has_workspace_header ? "#include \"WORKSPACE.h\"\n" : ""), f.data());
        StringToFile(str, cpp_file);
    }

    auto exec = AccessBinary("BUILD.exe");
    exec->AddDep(AccessFile(zmake_lib_dir + "/libzmake.a"));
    for (auto f : ListFilesUnderDir(build_root, "^BUILD.cpp$", true)) {
        auto obj = AccessObject(f);
        for (auto hdr : Glob({"*.h"}, {}, zmake_include_dir)) obj->AddDep(AccessFile(hdr));
        if (has_workspace_header) obj->AddDep(AccessFile("WORKSPACE.h"));
        exec->AddObj(obj);
    }
    if (exec->GetObjs().empty()) {
        throw std::runtime_error("no BUILD.cpp under current dir and its sub dirs");
    }

    AddTarget(exec);
    RegisterTargetInstall(exec, "./BUILD.exe", fs::copy_options::create_symlinks);

    if (CommandArgs::Has("-d")) {
        int debug_level = 1;
        try { debug_level = CommandArgs::Get<int>("-d"); } catch (...) {}
        SetDebugLevel(debug_level);
    }
    SetVerboseMode(CommandArgs::Has("-v"));
    BuildAll(CommandArgs::Has("-e"), CommandArgs::Get<int>("-j", -1));
    InstallAll();

    if (std::filesystem::exists("./BUILD.exe") && !CommandArgs::Has("-n")) {
        ColorPrint("* =============== execute ./BUILD.exe ===============\n", CT_BRIGHT_GREEN);
        system(StringPrintf("./BUILD.exe %s", CommandArgs::Str().data()).data());
    }

    return 0;
}


