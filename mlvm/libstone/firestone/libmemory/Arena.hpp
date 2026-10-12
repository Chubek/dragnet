#pragma once
#include "Alloc.hpp"
#include <algorithm>
#include <vector>

namespace firestone::memory {

// Monotonic raw storage with stable addresses until reset; no object destructors run.
class Arena {
public:
    explicit Arena(std::size_t block_size = 4096) : block_size_(block_size) {
        if (!block_size) throw std::invalid_argument("arena block size must be positive");
    }
    Arena(const Arena&) = delete;
    Arena& operator=(const Arena&) = delete;
    void* allocate(std::size_t bytes, std::size_t alignment = alignof(std::max_align_t)) {
        if (!valid_alignment(alignment)) throw std::invalid_argument("invalid arena alignment");
        bytes = std::max(bytes, std::size_t{1});
        if (!blocks_.empty()) {
            auto& block = blocks_.back();
            if (alignment <= block.storage.alignment()) {
                auto offset = align_up(block.used, alignment);
                if (offset <= block.storage.size() && bytes <= block.storage.size() - offset) {
                    block.used = offset + bytes;
                    return static_cast<std::byte*>(block.storage.data()) + offset;
                }
            }
        }
        auto capacity = std::max(block_size_, bytes);
        blocks_.push_back({Allocation(capacity, std::max(alignment, alignof(std::max_align_t))), bytes});
        return blocks_.back().storage.data();
    }
    void reset() noexcept { blocks_.clear(); }
    std::size_t reserved_bytes() const {
        std::size_t bytes = 0;
        for (const auto& block : blocks_) {
            if (bytes > std::numeric_limits<std::size_t>::max() - block.storage.size())
                throw std::overflow_error("arena accounting overflows");
            bytes += block.storage.size();
        }
        return bytes;
    }
private:
    struct Block { Allocation storage; std::size_t used; };
    std::size_t block_size_;
    std::vector<Block> blocks_;
};

} // namespace firestone::memory
