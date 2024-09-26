auto libs = DownloadLibraries("@protobuf", "https://github.com/protocolbuffers/protobuf/releases/download/v3.15.8/protobuf-cpp-3.15.8.tar.gz", true,
    "CXXFLAGS=-fPIE ./configure --prefix=$(readlink -f ..) && make && make install");
if (libs.empty()) throw "download and compile protobuf failed";
auto protoc_path = libs[0]->GetLinkDir() + "/../bin/protoc";
if (!std::filesystem::exists(protoc_path)) throw "no protoc under protobuf lib";
*AccessDefaultCompiler(".proto") = protoc_path;
ImportExternalZmakeProject("project1", "../project1");

DefaultObjectConfig()->SetFlags({"-std=c++17", "-g"});

AccessBinary("demo")
  ->AddObjs({"main.cpp"})
  ->AddDepLibs({
    "/src/foo",
    "/tool",
    "/proto/ps/cc_ps_proto",
  });
