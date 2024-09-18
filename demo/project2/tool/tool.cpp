#include <stdio.h>
#include <array>
#include <stdexcept>

#include "tool.h"

std::string ExecuteCmd(const std::string& cmd, int* ret_code) {
    std::string result;
    std::array<char, 128> buffer;
    FILE* f = popen(cmd.data(), "r");
    if (!f) throw std::runtime_error("popen failed");
    int n = 0, rc = 0;
    while ((n = fread(buffer.data(), 1, buffer.size(), f))) result.append(buffer.data(), n);
    if ((rc = pclose(f)) && ret_code) *ret_code = rc;
    return result;
}
