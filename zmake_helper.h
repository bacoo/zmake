/*
 * zmake_helper.h
 *
 *  Created on: 22 Feb 2024
 *      Author: yanbin.zhao
 */

#ifndef ZMAKE_HELPER_H_
#define ZMAKE_HELPER_H_

#include "zmake.h"
#include "zmake_util.h"

namespace zmake {

__attribute__((weak, unused))
void PrintHelpInfo() {
    printf("Usage: %s [OPTION]...\n"
           "  Build your C++ project using the C++ rules you defined, which are implemented\n"
           "  in C++, so you can use std::string/std::vector/std::map these STL containers\n"
           "  and you can define any variable, function or class. Unbelievable, right?\n"
           "\n"
           "  Define your building rules in 'BUILD.inc' or 'BUILD.cpp' under each dir, and\n"
           "  define global common vars/funcs/rules in '${project_root}/WORKSPACE.h';\n"
           "\n"
           "  See all available C++ APIs in '~/bin/zmake_files/zmake.h' and the demo under\n"
           "  '~/bin/zmake_files/demo/', which is a quite good tutorial for you;\n"
           "\n"
           "  Firstly, use `zmake` to build your project(it'll firstly generate 'BUILD.exe'\n"
           "  under your project root dir and then run `./BUILD.exe` to build), and you can\n"
           "  use `./BUILD.exe` to rebuild if you only modify project's source/header files(no\n"
           "  change for the building rules, i.e.: no any change for any BUILD.inc/BUILD.cpp/\n"
           "  WORKSPACE.h);\n"
           "\n"
           "  All generated files locate under '.zmade/', and if you want to clean, just\n"
           "  `rm -rf .zmade/`, and you can find the files that record original compile or\n"
           "  link commands by `find .zmade/ -name '*.cmd'`;\n"
           "\n"
           "Options:\n"
           "  -d \t set the debug level 0/1/2 to print more debug infos, -d0 by default, and\n"
           "     \t if you just use '-d', it means '-d1';\n"
           "  -v \t verbose mode to show full cmd;\n"
           "  -n \t not run ./BUILD.exe after generating it by `zmake`;\n"
           "  -j \t concurrency, -j0 by default, which will use 1/4 CPU cores;\n"
           "  -e \t export itself for being imported by other zmake projects, which\n"
           "     \t will generate the '.zmade/BUILD.libs' file;\n"
           "  -t \t specify only these targets to be built, which is separated by ';'\n"
           "  -A \t analyze the target's dependencies and dump to stdout, using -A <target>\n"
           "  -b \t build targets under a specific dir, using -b dir1/dir2/;\n"
           "  -g \t add -g for all targets' compilation and link;\n"
           "  -O \t set optimization level for all targets' compilation and link forcedly, it\n"
           "     \t will replace targets' optimization level defined in BUILD.inc; it's useful\n"
           "     \t if you want to compile a debug version with -O0;\n"
           "\n"
           "Report bugs to 'bacoo_zh@163.com'\n"
           "\n", CommandArgs::Arg0());
}

struct BuilderBase {
    virtual ~BuilderBase() {};
    virtual void Run() {};
    static auto& GlobalBuilders() {
        static std::map<std::string, std::function<BuilderBase*()>> s_builders;
        return s_builders;
    }
};

template <typename SubType>
struct Builder : public BuilderBase {
    static BuilderBase* Create() {
        return new SubType();
    }
};

#define BUILD()                                                                      \
namespace {                                                                          \
    struct __sub_builder__ : public Builder<__sub_builder__> {                       \
        virtual void Run();                                                          \
    };                                                                               \
}                                                                                    \
__attribute__((unused)) __attribute__((constructor)) static void __zmake_build__() { \
    BuilderBase::GlobalBuilders()[__FILE__] = &__sub_builder__::Create;              \
}                                                                                    \
void __sub_builder__::Run()

} //end of namespace zmake

#endif /* ZMAKE_HELPER_H_ */
