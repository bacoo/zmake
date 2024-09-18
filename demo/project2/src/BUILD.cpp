#include "zmake_helper.h"

using namespace zmake;

BUILD() {
    auto lib = AccessLibrary("foo");

    auto a_obj = AccessObject("a.cpp");
    a_obj->GetConfig()->SetFlag("-O2");

    auto b_obj = AccessObject("b.cpp");
    b_obj->GetConfig()->SetFlag("-O0");

    lib->AddObj(a_obj);
    lib->AddObj(b_obj);

    //with the help of it, we can use '#include "foo/a.h"'
    lib->AddIncludeDir("foo", true);

    lib->AddDepLibs({"@project1/string_util"});
}
