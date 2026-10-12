#pragma once
#include "tunah.hpp"
#include <set>

namespace limestone::tunah::detail {
inline constexpr size_t max_depth=256;
inline constexpr size_t max_source_size=4*1024*1024;
Error diagnostic(Error::Code, std::string message, const SourceLocation&);
bool symbol(std::string_view);
void validate_term(const Term&, const std::unordered_map<std::string,size_t>&,
                   bool pattern, std::set<std::string>& variables, size_t depth=0);
}
