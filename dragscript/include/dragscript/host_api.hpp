#pragma once
#include <dragscript/value.hpp>
#include <cstdint>
#include <functional>
#include <limits>
#include <span>

namespace dragscript {
struct HostResult {
    Value value;
    std::string error; // Nonempty means callback failure; translated to DS_HOST.
};
struct HostContext {
    // A false return exhausts the run, even if a callback ignores it.
    std::function<bool(std::uint64_t)> consume_steps;
    std::function<bool()> cancelled;
};
using HostFunction = std::function<HostResult(std::span<const Value>, HostContext&)>;
struct HostFunctionOptions {
    std::size_t min_arguments = 0;
    std::size_t max_arguments = std::numeric_limits<std::size_t>::max();
};
} // namespace dragscript
