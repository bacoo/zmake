/*
 * zmake_util.hpp
 *
 *  Created on: 8 Feb 2024
 *      Author: yanbin.zhao
 */

#ifndef ZMAKE_UTIL_H_
#define ZMAKE_UTIL_H_

#include <unistd.h>
#include <string>
#include <streambuf>
#include <stdexcept>
#include <fstream>
#include <sstream>
#include <memory>
#include <set>
#include <queue>
#include <mutex>
#include <future>
#include <thread>
#include <regex>
#include <algorithm>
#include <filesystem>

#define ZTHROW(...) throw std::runtime_error(StringPrintf("%s:%d %s %s", __FILE__, __LINE__, \
        __FUNCTION__, StringPrintf(__VA_ARGS__).data()))

namespace zmake {

__attribute__((weak, unused))
void RunWithLock(std::mutex& mtx, std::function<void()> f) {
    mtx.lock();
    f();
    mtx.unlock();
}

struct TaskRunnerPool {
    //the task functor should provide two ways for usage:
    //  1. get the task signature(it won't run the task):
    //       std::string task_sign;
    //       task(&task_sign);
    //  2. run the task:
    //       task(nullptr);
    using Task = std::function<void(std::string*)>;

    TaskRunnerPool(int thread_num = -1, bool start_at_once = false) : _thread_num(thread_num) {
        if (_thread_num <= 0) _thread_num = std::max(std::thread::hardware_concurrency() / 4, 1u);
        if (start_at_once) Start();
    }
    ~TaskRunnerPool() { Stop(); }

    void Start() {
        for (int i = 0; i < _thread_num; ++i) {
            _runners.emplace_back([this] {
                while (!_stop_flag) {
                    std::string task_sign;
                    Task task;
                    {
                        std::unique_lock<std::mutex> lock(_mutex);
                        //loop here in case of a spurious wake-up
                        while (_tasks.empty() && !_stop_flag) _cv.wait(lock);
                        for (size_t i = 0, n = _tasks.size(); i < n && !_stop_flag; ++i) {
                            auto t = std::move(_tasks.front());
                            _tasks.pop();
                            task_sign = "";
                            t(&task_sign);
                            if (_running_tasks.insert(task_sign).second) {
                                task = std::move(t);
                                break;
                            }
                            _tasks.push(std::move(t));
                        }

                        if (!task && !_stop_flag) {
                            _cv.wait(lock);
                            //can't find a valid task, so wait for new task, and trigger next round loop
                            continue;
                        }
                    }
                    if (!_stop_flag) {
                        task(nullptr);
                        RunWithLock(_mutex, [this, &task_sign]() { _running_tasks.erase(task_sign); });
                    }
                }
            });
        }
    }

    void Stop() {
        if (_stop_flag) return;

        RunWithLock(_mutex, [this]() { _stop_flag = true; });
        _cv.notify_all();
        for (auto& r : _runners) if (r.joinable()) r.join();
        std::queue<Task>{}.swap(_tasks);
    }

    void AddTask(const Task& t) {
        RunWithLock(_mutex, [&]() { _tasks.push(std::move(t)); });
        _cv.notify_one();
    }

    size_t GetTasksSize() {
        std::lock_guard<std::mutex> guard(_mutex);
        return _tasks.size();
    }

private:
    std::mutex _mutex;
    std::condition_variable _cv;
    std::queue<Task> _tasks;
    std::set<std::string> _running_tasks;
    int _thread_num = -1;
    volatile bool _stop_flag = false;
    std::vector<std::thread> _runners;
};

enum ColorType {
    CT_RED     = 1,
    CT_GREEN   = 2,
    CT_YELLOW  = 3,
    CT_BLUE    = 4,
    CT_MAGENTA = 5,
    CT_CYAN    = 6,
    CT_WHITE   = 7,

    CT_BRIGHT_RED     = CT_RED     + 7,
    CT_BRIGHT_GREEN   = CT_GREEN   + 7,
    CT_BRIGHT_YELLOW  = CT_YELLOW  + 7,
    CT_BRIGHT_BLUE    = CT_BLUE    + 7,
    CT_BRIGHT_MAGENTA = CT_MAGENTA + 7,
    CT_BRIGHT_CYAN    = CT_CYAN    + 7,
    CT_BRIGHT_WHITE   = CT_WHITE   + 7,

    CT_INVALID = -1,
};

//https://www.codeproject.com/Articles/5329247/How-to-Change-Text-Color-in-a-Linux-Terminal
__attribute__((weak, unused))
std::string ColorText(const std::string& s, ColorType ct) {
    switch (ct) {
    case CT_RED:            return "\033[31m"   + s + "\033[0m";
    case CT_GREEN:          return "\033[32m"   + s + "\033[0m";
    case CT_YELLOW:         return "\033[33m"   + s + "\033[0m";
    case CT_BLUE:           return "\033[34m"   + s + "\033[0m";
    case CT_MAGENTA:        return "\033[35m"   + s + "\033[0m";
    case CT_CYAN:           return "\033[36m"   + s + "\033[0m";
    case CT_WHITE:          return "\033[97m"   + s + "\033[0m";
    case CT_BRIGHT_RED:     return "\033[31;1m" + s + "\033[0m";
    case CT_BRIGHT_GREEN:   return "\033[32;1m" + s + "\033[0m";
    case CT_BRIGHT_YELLOW:  return "\033[33;1m" + s + "\033[0m";
    case CT_BRIGHT_BLUE:    return "\033[34;1m" + s + "\033[0m";
    case CT_BRIGHT_MAGENTA: return "\033[35;1m" + s + "\033[0m";
    case CT_BRIGHT_CYAN:    return "\033[36;1m" + s + "\033[0m";
    case CT_BRIGHT_WHITE:   return "\033[97;1m" + s + "\033[0m";
    default: break;
    }
    return s;
}

__attribute__((weak, unused))
void ColorPrint(const std::string& s, ColorType ct, FILE* fp = stdout) {
    if ('\n' == *s.rbegin()) {
        fprintf(fp, "%s\n", ColorText(s.substr(0, s.size() - 1),
                isatty(fileno(fp)) ? ct : CT_INVALID).data());
    } else {
        fprintf(fp, "%s", ColorText(s, isatty(fileno(fp)) ? ct : CT_INVALID).data());
    }
}

template <typename... Args>
std::string StringPrintf(const std::string& fmt, Args... args) {
    size_t size = std::snprintf(nullptr, 0, fmt.data(), args...) + 1;  // include '\0'
    auto buf = std::make_unique<char[]>(size);
    std::snprintf(buf.get(), size, fmt.data(), args...);
    return std::string(buf.get(), size - 1);  // remove '\0'
}

__attribute__((weak, unused))
std::vector<std::string> StringSplit(const std::string& s, char delim = ' ', bool reserve_empty_token = false) {
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
template <typename T>
std::string StringCompose(const T& container, char delim = ';') {
    if (container.empty()) return "";
    std::ostringstream oss;
    int idx = 0;
    for (auto& x : container) {
        if (idx++) oss << delim;
        oss << x;
    }
    return oss.str();
}

//suffix support multiple matches split by '|', such as: ".cc|.cpp"
__attribute__((weak, unused))
bool StringEndWith(const std::string& str, const std::string& suffix) {
    for (const auto& x : StringSplit(suffix, '|')) {
        if (x.size() > str.size()) continue;
        if (std::equal(x.rbegin(), x.rend(), str.rbegin())) return true;
    }
    return false;
}

//prefix support multiple matches split by '|', such as: "lib|Lib|LIB"
__attribute__((weak, unused))
bool StringBeginWith(const std::string& str, const std::string& prefix) {
    for (const auto& x : StringSplit(prefix, '|')) {
        if (x.size() > str.size()) continue;
        if (std::equal(x.begin(), x.end(), str.begin())) return true;
    }
    return false;
}

//old_suffix support multiple matches split by '|', such as: ".cc|.cpp"
__attribute__((weak, unused))
std::string StringReplaceSuffix(const std::string& str, const std::string& old_suffix, const std::string& new_suffix) {
    for (const auto& x : StringSplit(old_suffix, '|')) {
        auto p = str.rfind(x);
        if (std::string::npos == p || p + x.size() != str.size()) continue;
        return str.substr(0, p) + new_suffix;
    }
    return str;
}

__attribute__((weak, unused))
std::string GetFilenameFromPath(const std::string& path) {
    auto p = path.rfind('/');
    return std::string::npos == p ? path : path.substr(p + 1);
}

__attribute__((weak, unused))
std::string GetDirnameFromPath(const std::string& path) {
    auto p = path.rfind('/');
    return std::string::npos == p ? "./" : path.substr(0, p + 1);
}

__attribute__((weak, unused))
bool StringToFile(const std::string& str, const std::string& filename) {
    std::ofstream of(filename);
    if (!of.is_open()) return false;
    of << str;
    return true;
}

__attribute__((weak, unused))
std::string StringFromFile(const std::string& filename) {
    std::ifstream f(filename);
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

__attribute__((weak, unused))
std::string StringReplaceAll(std::string str, const std::string& from, const std::string& to) {
    size_t start_pos = 0;
    while(std::string::npos != (start_pos = str.find(from, start_pos))) {
        str.replace(start_pos, from.length(), to);
        start_pos += to.length();
    }
    return str;
}

__attribute__((weak, unused))
std::string StringRightTrim(std::string str) {
    auto iter = std::find_if(str.rbegin(), str.rend(),
            [](char c) { return !std::isspace<char>(c, std::locale::classic()); });
    str.erase(iter.base() , str.end());
    return str;
}

__attribute__((weak, unused))
std::vector<std::string> ListFilesUnderDir(const std::string& path = ".",
        const std::string& filename_regex_filter = "", bool recursive = false,
        bool skip_hidden_entries = false) {
    if (!std::filesystem::is_directory(path)) return {path};

    std::vector<std::string> ret;
    std::regex r(filename_regex_filter);
    static std::regex s_hidden_reg("/\\.[^.]");
#define LF_IMPLEMENTATION(FUNC)                                                            \
    for (const auto& e : std::filesystem::FUNC(path)) {                                    \
        if (!e.is_regular_file()) continue;                                                \
        auto ep = e.path().lexically_normal();                                             \
        if (skip_hidden_entries && std::regex_search(ep.string(), s_hidden_reg)) continue; \
        if (std::regex_search(ep.filename().string(), r)) ret.emplace_back(ep);            \
    }
    if (recursive) {
        LF_IMPLEMENTATION(recursive_directory_iterator);
    } else {
        LF_IMPLEMENTATION(directory_iterator);
    }

    return ret;
}

struct CommandArgs {
    static void Init(int argc, char* argv[]) { Argc() = argc; Argv() = argv; }
    static const char* Arg0() { return Argv()[0]; }
    static bool Has(const std::string& name) { return ParseVal<bool>(name); }
    template <typename T>
    static T Get(const std::string& name, T default_res = {}) {
        return ParseVal<T>(name, &default_res), default_res;
    }
    template <typename T>
    static std::vector<T> Gets(const std::string& name) { return ParseVals<T>(name); }
    static std::string Str() {
        std::ostringstream oss;
        for (int i = 0; i < Argc(); ++i) oss << (i ? " " : "") << Argv()[i];
        return oss.str();
    }

private:
    static int& Argc() { static int s_argc = 0; return s_argc; }
    static char**& Argv() { static char** s_argv = nullptr; return s_argv; }
    template <typename T>
    static bool ParseVal(const std::string& name, T* val = nullptr) {
        auto vals = ParseVals<T>(name, nullptr != val);
        if (val && !vals.empty()) *val = vals[0];
        return !vals.empty();
    }
    template <typename T>
    static std::vector<T> ParseVals(const std::string& name, int expected_val_num = 0) {
        if ("" == name || '-' != name.at(0)) ZTHROW("invalid command line argument(%s)", name.data());
        std::vector<T> vals;
        for (int i = 1; i < Argc(); ++i) {
            if (!StringBeginWith(Argv()[i], name)) continue;
            if (0 == expected_val_num) {
                vals.resize(1); //add one empty val as a placeholder
                break;
            }
            auto parse_val_fn = [&vals](const char* p) {
                T val = {};
                std::istringstream(p) >> val;
                vals.push_back(val);
            };
            if (Argv()[i] == name) {
                if (i < Argc() - 1 && '-' != Argv()[i + 1][0]) parse_val_fn(Argv()[++i]);
                else ZTHROW("no value for this argument(%s)", name.data());
            } else {
                const char* p = Argv()[i] + name.size();
                parse_val_fn(('=' == *p) ? p + 1 : p);
            }
            if ((int)vals.size() == expected_val_num) break;
        }
        return vals;
    }
};

} //end of namespace zmake

#endif /* ZMAKE_UTIL_H_ */
