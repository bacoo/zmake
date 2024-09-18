AccessLibrary("io")
  ->AddObjs(Glob({"**.cpp"}, {"*_test.cpp"}))
  ->SetUsedAsWholeArchive();
