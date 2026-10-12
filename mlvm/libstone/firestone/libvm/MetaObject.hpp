#pragma once
#include "Value.hpp"
#include <map>

namespace firestone::vm {

struct MetaObject {
    std::string name;
    std::map<std::string, ValueKind> fields;
    std::map<std::string, std::string> methods; // method name -> native symbol
};
} // namespace firestone::vm
