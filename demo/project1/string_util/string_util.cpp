#include <vector>
#include <stdexcept>
#include "string_util.h"
#include "file/file.h" //using "io/file/file.h" is also ok

std::vector<std::string> StringSplit(const std::string& s, char delim, bool reserve_empty_token) {
    size_t sz = s.size();
    if (0 == sz) return {};

    std::vector<std::string> tokens;
    size_t p = std::string::npos, lp = 0; // pos and last_pos
    while (lp < sz && std::string::npos != (p = s.find(delim, lp))) {
        if (reserve_empty_token || p > lp) tokens.emplace_back(s.substr(lp, p - lp));
        lp = p + 1;
    }
    if (lp < sz) tokens.emplace_back(s.substr(lp));
    else if (reserve_empty_token) tokens.emplace_back(""); // " a" will return two parts, so " a " should return three parts
    return tokens;
}

std::vector<std::string> SplitFileContent(const std::string& filename, char delim) {
    auto file_content = StringFromFile(filename);
#ifndef NDEBUG
    printf("file(%s) content length: %lu\n", filename.data(), file_content.size());
#endif
    if (file_content.size() > MAX_FILE_LENGTH) {
        throw std::runtime_error("file is too long");
    }
    return StringSplit(file_content, delim);
}
