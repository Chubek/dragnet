#pragma once
#include "limestone/foundation.hpp"

namespace limestone::syntax {
// The resolver owns its returned text. Name is a stable document identity used
// for source locations, relative resolution, and include-cycle detection.
struct ResolvedSource { std::string name, text; };
using IncludeResolver=std::function<Result<ResolvedSource>(std::string_view including_file,std::string_view requested_file)>;
struct IncludeOptions {
  IncludeResolver resolver;
  // Cumulative limits include the root document and every include occurrence.
  size_t bytes=16*1024*1024, documents=128, depth=32;
};
// Resolve relative to the including file, canonicalize identities (including
// symlinks), and read bounded text. No implicit filesystem access in text loaders.
IncludeResolver filesystem_resolver(size_t byte_limit=16*1024*1024);
}
