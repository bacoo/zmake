#include <string>
#include <vector>

std::string Trim(const std::string& s, const std::string& trimed_chars = " \t\n\r");
std::vector<std::string> StringSplit(const std::string& s, char delim = ' ', bool reserve_empty_token = false);
std::vector<std::string> SplitFileContent(const std::string& filename, char delim);
