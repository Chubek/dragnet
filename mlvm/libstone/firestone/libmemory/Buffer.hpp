#pragma once
#include "Alloc.hpp"
#include <algorithm>
#include <cstring>

namespace firestone::memory {

class Buffer {
public:
    explicit Buffer(std::size_t size = 0) : storage_(size), size_(size) {
        if (size) std::memset(storage_.data(), 0, size);
    }
    Buffer(const Buffer& other) : Buffer(other.size_) {
        if (size_) std::memcpy(data(), other.data(), size_);
    }
    Buffer& operator=(const Buffer& other) { Buffer copy(other); swap(copy); return *this; }
    Buffer(Buffer&& other) noexcept : storage_(std::move(other.storage_)), size_(std::exchange(other.size_, 0)) {}
    Buffer& operator=(Buffer&& other) noexcept { Buffer moved(std::move(other)); swap(moved); return *this; }
    void swap(Buffer& other) noexcept { storage_.swap(other.storage_); std::swap(size_, other.size_); }
    std::byte* data() noexcept { return static_cast<std::byte*>(storage_.data()); }
    const std::byte* data() const noexcept { return static_cast<const std::byte*>(storage_.data()); }
    std::size_t size() const noexcept { return size_; }
    std::size_t capacity() const noexcept { return storage_.size(); }
    std::byte& at(std::size_t index) { if (index >= size_) throw std::out_of_range("buffer index"); return data()[index]; }
    const std::byte& at(std::size_t index) const { if (index >= size_) throw std::out_of_range("buffer index"); return data()[index]; }
    void reserve(std::size_t capacity) {
        if (capacity <= this->capacity()) return;
        Allocation replacement(capacity);
        if (size_) std::memcpy(replacement.data(), data(), size_);
        storage_.swap(replacement);
    }
    void resize(std::size_t size) {
        reserve(size);
        if (size > size_) std::memset(data() + size_, 0, size - size_);
        size_ = size;
    }
    // Taking a snapshot makes append safe even when source points into this buffer.
    void append(const void* source, std::size_t bytes) {
        if (!bytes) return;
        if (!source) throw std::invalid_argument("null buffer source");
        if (bytes > std::numeric_limits<std::size_t>::max() - size_) throw std::overflow_error("buffer size overflows");
        Allocation snapshot(bytes); std::memcpy(snapshot.data(), source, bytes);
        auto offset = size_; resize(size_ + bytes); std::memcpy(data() + offset, snapshot.data(), bytes);
    }
private:
    Allocation storage_;
    std::size_t size_ = 0;
};

} // namespace firestone::memory
