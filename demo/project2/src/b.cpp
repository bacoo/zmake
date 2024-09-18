#include "b.h"
#include "string_util.h"

void B::test() {
    for (auto x : StringSplit("hello world !", ' ')) {
        printf("word:{%s}, word length: %lu\n", x.data(), x.size());
    }
}
