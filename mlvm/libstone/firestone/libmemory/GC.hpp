#pragma once
#include <algorithm>
#include <cstddef>
#include <optional>
#include <set>
#include <stdexcept>
#include <utility>
#include <vector>

namespace firestone::memory {

using Handle = std::size_t;
struct Collection { std::size_t retained = 0, reclaimed = 0; };

// Explicit tracing heap. Handles are append-only, never recycled after collection.
template<class T>
class GC {
public:
    Handle allocate(T value, std::vector<Handle> references = {}) {
        check_references(references);
        slots_.emplace_back(Entry{std::move(value), std::move(references)});
        return slots_.size() - 1;
    }
    bool contains(Handle handle) const { return handle < slots_.size() && slots_[handle].has_value(); }
    T& get(Handle handle) { check(handle); return slots_[handle]->value; }
    const T& get(Handle handle) const { check(handle); return slots_[handle]->value; }
    const std::vector<Handle>& references(Handle handle) const { check(handle); return slots_[handle]->references; }
    void set_references(Handle handle, std::vector<Handle> references) {
        check(handle); check_references(references); slots_[handle]->references = std::move(references);
    }
    void pin(Handle handle) { check(handle); pinned_.insert(handle); }
    void unpin(Handle handle) { check(handle); if (!pinned_.erase(handle)) throw std::logic_error("heap handle is not pinned"); }
    std::size_t size() const {
        return static_cast<std::size_t>(std::count_if(slots_.begin(), slots_.end(), [](const auto& slot) { return slot.has_value(); }));
    }
    Collection collect(const std::vector<Handle>& roots = {}) {
        check_references(roots);
        std::set<Handle> marked = pinned_;
        std::vector<Handle> pending(pinned_.begin(), pinned_.end());
        for (auto root : roots) if (marked.insert(root).second) pending.push_back(root);
        while (!pending.empty()) {
            auto handle = pending.back(); pending.pop_back();
            for (auto child : references(handle)) if (marked.insert(child).second) pending.push_back(child);
        }
        Collection result;
        for (Handle handle = 0; handle < slots_.size(); ++handle) if (contains(handle)) {
            if (marked.count(handle)) ++result.retained;
            else { slots_[handle].reset(); ++result.reclaimed; }
        }
        return result;
    }
private:
    struct Entry { T value; std::vector<Handle> references; };
    std::vector<std::optional<Entry>> slots_;
    std::set<Handle> pinned_;
    void check(Handle handle) const { if (!contains(handle)) throw std::out_of_range("invalid heap handle"); }
    void check_references(const std::vector<Handle>& references) const { for (auto handle : references) check(handle); }
};

} // namespace firestone::memory
