#pragma once
#include "limestone/foundation.hpp"
#include <cstddef>

namespace limestone::syntax {
// Byte offsets and one-based line/column coordinates. End positions are exclusive.
struct SourcePosition {
  size_t offset=0, line=1, column=1;
  bool operator==(const SourcePosition&) const = default;
};
struct SourceSpan {
  std::string file;
  SourcePosition begin, end;
  bool operator==(const SourceSpan&) const = default;
};
struct ParseLimits {
  size_t bytes=16*1024*1024;
  size_t nodes=1000000;
  size_t depth=512;
};
}
