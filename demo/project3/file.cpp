#include <fstream>
#include <streambuf>

#include "file.h"

std::string StringFromFile(const std::string& filename) {
    std::ifstream f(filename);
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}
