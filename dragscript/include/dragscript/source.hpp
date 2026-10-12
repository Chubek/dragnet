#pragma once
#include <cstddef>
#include <string>

namespace dragscript {
enum class SourceKind { infer, javascript, typescript, tsx, jsx };
struct Source {
    std::string name;
    std::string text; // Valid UTF-8. Owned by compile and its resulting artifact.
    SourceKind kind = SourceKind::infer;
    std::string origin;
};
struct SourcePosition {
    std::size_t byte = 0;
    std::size_t line = 1;
    std::size_t column = 1; // One-based UTF-8 byte column, including at EOF.
};
struct SourceSpan { SourcePosition begin, end; }; // Half-open byte range.
SourcePosition source_position(const Source& source, std::size_t byte);
SourceSpan source_span(const Source& source, std::size_t begin, std::size_t end);
} // namespace dragscript
