#include <bits/stdc++.h>
#include "file.h"
#include "tool/tool.h"
#include "tool/net/net.h"
#include "string_util/string_util.h"
using namespace std;

int main(int argc, char* argv[]) {
    auto f1 = Trim(ExecuteCmd("find -name '*.cpp' | head -1 | sed 's@\\n@@g'"));
    auto lines1 = SplitFileContent(f1, '\n');
    auto lines2 = StringSplit(StringFromFile(f1), '\n');
    if (lines1 != lines2) {
        printf("test failed\n");
        return -1;
    }
    string msg = "validate SplitFileContent == StringSplit(StringFromFile)";
    NetIO ni;
    ni.Write(msg.data(), msg.size());
    printf("test ok\n");
    return 0;
}
