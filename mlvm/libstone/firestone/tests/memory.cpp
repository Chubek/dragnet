#include "Test.hpp"
#include <array>
#include <memory>
#include <memory_resource>

using namespace firestone::memory;

struct Counted : RefCounted {
    int& destroyed;
    explicit Counted(int& destroyed) : destroyed(destroyed) {}
    ~Counted() override { ++destroyed; }
};
struct Traced {
    int& destroyed;
    explicit Traced(int& destroyed) : destroyed(destroyed) {}
    ~Traced() { ++destroyed; }
};

int main() { return test_main([] {
    Allocation aligned(100, 256);
    CHECK(reinterpret_cast<std::uintptr_t>(aligned.data()) % 256 == 0);
    Allocation moved(std::move(aligned));
    CHECK(!aligned.data() && moved.size() == 100);
    throws<std::invalid_argument>([] { Allocation invalid(4, 3); });
    throws<std::overflow_error>([] { align_up(std::numeric_limits<std::size_t>::max(), 8); });

    Pool pool(17, 2, 64);
    auto first = pool.allocate(), second = pool.allocate();
    CHECK(pool.owns(first) && pool.available() == 0);
    CHECK(reinterpret_cast<std::uintptr_t>(first) % 64 == 0);
    CHECK(reinterpret_cast<std::uintptr_t>(second) % 64 == 0);
    throws<std::bad_alloc>([&] { pool.allocate(); });
    throws<std::invalid_argument>([&] { pool.deallocate(static_cast<std::byte*>(first) + 1); });
    pool.deallocate(first);
    throws<std::invalid_argument>([&] { pool.deallocate(first); });
    CHECK(pool.allocate() == first);
    PoolResource pooled(pool);
    throws<std::bad_alloc>([&] { (void)pooled.allocate(65); });
    pool.deallocate(second);
    auto storage = pooled.allocate(12, 32); pooled.deallocate(storage, 12, 32);
    throws<std::overflow_error>([] { Pool invalid(64, std::numeric_limits<std::size_t>::max()); });

    Arena arena(32);
    auto bytes = static_cast<std::byte*>(arena.allocate(31)); bytes[0] = std::byte{42};
    auto over_aligned = arena.allocate(100, 128);
    CHECK(reinterpret_cast<std::uintptr_t>(over_aligned) % 128 == 0 && bytes[0] == std::byte{42});
    {
        ArenaResource resource(arena);
        std::pmr::vector<int> values(&resource);
        for (int i = 0; i < 100; ++i) values.push_back(i);
        CHECK(values[42] == 42);
    }
    CHECK(arena.reserved_bytes() >= 131); arena.reset(); CHECK(arena.reserved_bytes() == 0);

    Buffer buffer(3); buffer.at(0) = std::byte{9};
    buffer.append(buffer.data(), buffer.size());
    CHECK(buffer.size() == 6 && buffer.at(3) == std::byte{9});
    Buffer copy = buffer; copy.at(0) = std::byte{1}; CHECK(buffer.at(0) == std::byte{9});
    Buffer transferred = std::move(copy); CHECK(copy.size() == 0 && transferred.size() == 6);
    buffer.resize(8); CHECK(buffer.at(7) == std::byte{0});
    throws<std::out_of_range>([&] { buffer.at(8); });

    int destroyed = 0;
    {
        auto pointer = make_intrusive<Counted>(destroyed);
        auto another = pointer; CHECK(pointer->use_count() == 2);
        another.reset(); pointer.reset(pointer.get()); CHECK(pointer->use_count() == 1);
        auto moved_pointer = std::move(pointer); CHECK(!pointer && moved_pointer);
    }
    CHECK(destroyed == 1);
    destroyed = 0;
    GC<std::unique_ptr<Traced>> heap;
    auto a = heap.allocate(std::make_unique<Traced>(destroyed));
    auto b = heap.allocate(std::make_unique<Traced>(destroyed), {a});
    heap.set_references(a, {b});
    auto garbage = heap.allocate(std::make_unique<Traced>(destroyed));
    heap.pin(a);
    auto collection = heap.collect();
    CHECK(collection.retained == 2 && collection.reclaimed == 1 && destroyed == 1);
    CHECK(!heap.contains(garbage));
    throws<std::out_of_range>([&] { heap.collect({garbage}); });
    CHECK(heap.size() == 2);
    heap.unpin(a); collection = heap.collect();
    CHECK(collection.reclaimed == 2 && destroyed == 3);
    CHECK(heap.allocate(std::make_unique<Traced>(destroyed)) > garbage);
}); }
