#include <string>
#include <vector>

std::vector<std::string> StringSplit(const std::string& s, char delim = ' ', bool reserve_empty_token = false);
std::vector<std::string> SplitFileContent(const std::string& filename, char delim);
