DownloadLibraries("@protobuf", "https://github.com/protocolbuffers/protobuf/releases/download/v3.15.8/protobuf-cpp-3.15.8.tar.gz", true,
    "CXXFLAGS=-fPIE ./configure --prefix=$(readlink -f ..) && make -j && make -j install");
ImportExternalZmakeProject("project1", "../project1");

DefaultObjectConfig()->SetFlags({"-std=c++17", "-g"});

AccessBinary("demo")
  ->AddObjs({"main.cpp"})
  ->AddDepLibs({
    "/src/foo",
    "/tool",
    "/proto/ps/cc_ps_proto",
  });
