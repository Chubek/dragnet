#pragma once

#include <cstddef>
#include <limits>
#include <new>
#include <stdexcept>
#include <utility>

namespace firestone::memory {

inline bool valid_alignment(std::size_t alignment) {
    return alignment != 0 && (alignment & (alignment - 1)) == 0;
}

inline std::size_t align_up(std::size_t size, std::size_t alignment) {
    if (!valid_alignment(alignment)) throw std::invalid_argument("alignment must be a power of two");
    if (size > std::numeric_limits<std::size_t>::max() - (alignment - 1))
        throw std::overflow_error("aligned size overflows");
    return (size + alignment - 1) & ~(alignment - 1);
}

// Owns raw storage; clients are responsible for constructing/destroying objects.
class Allocation {
public:
    explicit Allocation(std::size_t size = 0, std::size_t alignment = alignof(std::max_align_t))
        : size_(size), alignment_(alignment) {
        if (!valid_alignment(alignment)) throw std::invalid_argument("invalid allocation alignment");
        if (size) data_ = ::operator new(size, std::align_val_t(actual_alignment()));
    }
    ~Allocation() { release(); }
    Allocation(const Allocation&) = delete;
    Allocation& operator=(const Allocation&) = delete;
    Allocation(Allocation&& other) noexcept { swap(other); }
    Allocation& operator=(Allocation&& other) noexcept {
        if (this != &other) { Allocation temporary(std::move(other)); swap(temporary); }
        return *this;
    }
    void swap(Allocation& other) noexcept {
        std::swap(data_, other.data_); std::swap(size_, other.size_); std::swap(alignment_, other.alignment_);
    }
    void* data() const noexcept { return data_; }
    std::size_t size() const noexcept { return size_; }
    std::size_t alignment() const noexcept { return alignment_; }
private:
    void* data_ = nullptr;
    std::size_t size_ = 0, alignment_ = alignof(std::max_align_t);
    std::size_t actual_alignment() const noexcept {
        return alignment_ < alignof(std::max_align_t) ? alignof(std::max_align_t) : alignment_;
    }
    void release() noexcept { if (data_) ::operator delete(data_, std::align_val_t(actual_alignment())); }
};

} // namespace firestone::memory
