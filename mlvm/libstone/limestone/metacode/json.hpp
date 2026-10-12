#pragma once
#include "metacode.hpp"

namespace limestone::metacode {
// Exact integer/string/Boolean metadata interchange. JSON null and floating
// numbers require a richer metadata model and are rejected explicitly.
Result<Value> parse_json(std::string_view,std::string_view file="<json>");
Result<std::string> print_json(const Value&);
}
