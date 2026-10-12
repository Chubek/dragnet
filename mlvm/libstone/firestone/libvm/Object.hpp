#pragma once
#include "MetaObject.hpp"

namespace firestone::vm {

struct Object {
    MetaObject meta;
    std::map<std::string, Value> fields;
};

} // namespace firestone::vm
