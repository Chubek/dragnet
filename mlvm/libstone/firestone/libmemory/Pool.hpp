#pragma once
#include "Alloc.hpp"
#include <cstdint>
#include <vector>

namespace firestone::memory {

class Pool {
public:
    Pool(std::size_t block_size, std::size_t capacity, std::size_t alignment = alignof(std::max_align_t))
        : stride_(align_up(checked_block(block_size), checked_alignment(alignment))), capacity_(capacity), alignment_(checked_alignment(alignment)),
          block_size_(checked_block(block_size)),
          storage_(storage_size(stride_, capacity), alignment), used_(capacity, false) {
        for (std::size_t i = capacity; i > 0; --i) free_.push_back(i - 1);
    }
    Pool(const Pool&) = delete;
    Pool& operator=(const Pool&) = delete;
    void* allocate() {
        if (free_.empty()) throw std::bad_alloc();
        auto index = free_.back(); free_.pop_back(); used_[index] = true;
        return static_cast<std::byte*>(storage_.data()) + index * stride_;
    }
    void deallocate(void* pointer) {
        const auto index = index_of(pointer);
        if (!used_[index]) throw std::invalid_argument("pool block is already free");
        free_.push_back(index); used_[index] = false;
    }
    bool owns(const void* pointer) const noexcept {
        if (!pointer || !storage_.data()) return false;
        auto base = reinterpret_cast<std::uintptr_t>(storage_.data());
        auto address = reinterpret_cast<std::uintptr_t>(pointer);
        return address >= base && address - base < storage_.size() && (address - base) % stride_ == 0;
    }
    std::size_t capacity() const noexcept { return capacity_; }
    std::size_t available() const noexcept { return free_.size(); }
    std::size_t block_size() const noexcept { return block_size_; }
    std::size_t stride() const noexcept { return stride_; }
    std::size_t alignment() const noexcept { return alignment_; }
private:
    std::size_t stride_, capacity_, alignment_, block_size_;
    Allocation storage_;
    std::vector<bool> used_;
    std::vector<std::size_t> free_;
    static std::size_t checked_block(std::size_t size) {
        if (!size) throw std::invalid_argument("pool blocks must be nonempty");
        return size;
    }
    static std::size_t checked_alignment(std::size_t alignment) {
        if (!valid_alignment(alignment)) throw std::invalid_argument("pool alignment must be a power of two");
        return alignment;
    }
    static std::size_t storage_size(std::size_t stride, std::size_t count) {
        if (stride && count > std::numeric_limits<std::size_t>::max() / stride)
            throw std::overflow_error("pool size overflows");
        return stride * count;
    }
    std::size_t index_of(const void* pointer) const {
        if (!owns(pointer)) throw std::invalid_argument("pointer is not a pool block");
        return (reinterpret_cast<std::uintptr_t>(pointer) - reinterpret_cast<std::uintptr_t>(storage_.data())) / stride_;
    }
};

} // namespace firestone::memory
