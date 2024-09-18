DefaultObjectConfig()->SetFlags({"-std=c++17", "-g", "-O2"});

*AccessDefaultCompiler(".cpp") = "g++";

AccessLibrary("common")
  ->AddDepLibs({
    "/string_util",
    "/io",
  });
