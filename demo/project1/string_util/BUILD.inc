AccessLibrary("string_util")
  ->AddObjs({"string_util.cpp"})
  ->SetObjsFlags({"-DNDEBUG", "-DMAX_FILE_LENGTH=999999"})
  ->AddDepLibs({
    "/io",
  });
