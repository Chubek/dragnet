#include <dragscript/source.hpp>
#include <algorithm>

namespace dragscript {
SourcePosition source_position(const Source& source, std::size_t byte) {
    byte = std::min(byte, source.text.size());
    SourcePosition result{byte, 1, 1};
    for (std::size_t i = 0; i < byte; ++i) {
        if (source.text[i] == '\n') { ++result.line; result.column = 1; }
        else ++result.column;
    }
    return result;
}
SourceSpan source_span(const Source& source, std::size_t begin, std::size_t end) {
    return {source_position(source, begin), source_position(source, end)};
}
} // namespace dragscript
