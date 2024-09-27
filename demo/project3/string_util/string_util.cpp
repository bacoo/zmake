#include <vector>
#include <stdexcept>
#include "string_util.h"
#include "file.h"

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

std::string Trim(const std::string& s, const std::string& trimed_chars) {
    if ("" == s) return s;
    size_t i = -1;
    while (std::string::npos != trimed_chars.find(s.at(++i)));
    size_t j = s.size();
    while (std::string::npos != trimed_chars.find(s.at(--j)));
    return s.substr(i, j - i + 1);
}

std::vector<std::string> SplitFileContent(const std::string& filename, char delim) {
    auto file_content = StringFromFile(filename);
    if (file_content.size() > 1024 * 1024) {
        throw std::runtime_error("file is too long");
    }
    return StringSplit(file_content, delim);
}
