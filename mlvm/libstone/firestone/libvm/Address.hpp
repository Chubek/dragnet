#pragma once
#include <cstddef>
#include <limits>

namespace firestone::vm {

using Index = std::size_t;
inline constexpr Index no_index = std::numeric_limits<Index>::max();

// Heap addresses and code positions are separate domains. Heap slots are never reused.
struct Address {
    Index slot = no_index;
    friend bool operator==(Address a, Address b) { return a.slot == b.slot; }
};
struct ObjectRef {
    Index id = no_index;
    friend bool operator==(ObjectRef a, ObjectRef b) { return a.id == b.id; }
};
struct CodeAddress {
    Index function = no_index, instruction = no_index;
    friend bool operator==(CodeAddress a, CodeAddress b) {
        return a.function == b.function && a.instruction == b.instruction;
    }
};

} // namespace firestone::vm
