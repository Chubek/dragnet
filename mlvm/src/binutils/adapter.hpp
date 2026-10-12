#pragma once
#include <mlvm/binutils.hpp>
#include <bin2bin/object.hpp>

namespace mlvm::detail {
namespace stone = limestone::bin2bin;
inline constexpr std::uint64_t binary_byte_limit = 256 * 1024 * 1024;
stone::ObjectLimits object_limits();
stone::ArchiveLimits archive_limits();
stone::ObjectFile to_stone(const BinaryObject&);
BinaryObject from_stone(const stone::ObjectFile&);
stone::ObjectTarget to_stone(const LinkContract&);
} // namespace mlvm::detail
