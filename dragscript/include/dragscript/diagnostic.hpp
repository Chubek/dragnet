#pragma once
#include <dragscript/source.hpp>
#include <vector>

namespace dragscript {
enum class Phase { compile, runtime };
struct TraceFrame { std::string function; SourceSpan span; };
struct Diagnostic {
    std::string code;
    std::string message;
    std::string source_name;
    std::string origin;
    SourceSpan span;
    Phase phase = Phase::compile;
    std::string limit; // Empty for non-resource errors.
    std::vector<TraceFrame> trace;
};
} // namespace dragscript
