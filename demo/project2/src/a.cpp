#include "a.h"
#include "io/file.h"

std::string A::read(const std::string& filename) {
    return StringFromFile(filename);
}
