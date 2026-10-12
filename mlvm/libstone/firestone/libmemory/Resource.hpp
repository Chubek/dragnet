#pragma once
#include "Arena.hpp"
#include "Pool.hpp"
#include <memory_resource>

namespace firestone::memory {

class ArenaResource final : public std::pmr::memory_resource {
public:
    explicit ArenaResource(Arena& arena) : arena_(arena) {}
private:
    Arena& arena_;
    void* do_allocate(std::size_t bytes, std::size_t alignment) override { return arena_.allocate(bytes, alignment); }
    void do_deallocate(void*, std::size_t, std::size_t) override {}
    bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override { return this == &other; }
};

// Requests outside the pool's size/alignment contract fail rather than falling back.
class PoolResource final : public std::pmr::memory_resource {
public:
    explicit PoolResource(Pool& pool) : pool_(pool) {}
private:
    Pool& pool_;
    void* do_allocate(std::size_t bytes, std::size_t alignment) override {
        if (bytes > pool_.block_size() || alignment > pool_.alignment()) throw std::bad_alloc();
        return pool_.allocate();
    }
    void do_deallocate(void* pointer, std::size_t, std::size_t) noexcept override {
        try { if (pointer && pool_.owns(pointer)) pool_.deallocate(pointer); } catch (...) {}
    }
    bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override { return this == &other; }
};

} // namespace firestone::memory
