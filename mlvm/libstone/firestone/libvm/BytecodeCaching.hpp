#pragma once
#include "BytecodeBinary.hpp"
#include <list>
#include <memory>

namespace firestone::vm {

// Exact bytes and decoding policy are the key: no hash collisions or stale policy hits.
class BytecodeCache {
public:
    explicit BytecodeCache(Index capacity = 64) : capacity_(capacity) {}
    std::shared_ptr<const Module> load(const BytecodeBytes& bytes, const BinaryLimits& limits = {}) {
        for (auto it = entries_.begin(); it != entries_.end(); ++it) if (it->bytes == bytes &&
            it->limits.bytes == limits.bytes && it->limits.items == limits.items && it->limits.string_bytes == limits.string_bytes) {
            auto result = it->module; entries_.splice(entries_.begin(), entries_, it); ++hits_; return result;
        }
        auto decoded = std::make_shared<const Module>(decode(bytes, limits));
        ++misses_;
        if (capacity_) {
            entries_.push_front({bytes, decoded, limits});
            if (entries_.size() > capacity_) entries_.pop_back();
        }
        return decoded;
    }
    Index size() const { return entries_.size(); }
    Index hits() const { return hits_; }
    Index misses() const { return misses_; }
    void clear() { entries_.clear(); hits_ = 0; misses_ = 0; }
private:
    struct Entry { BytecodeBytes bytes; std::shared_ptr<const Module> module; BinaryLimits limits; };
    Index capacity_, hits_ = 0, misses_ = 0;
    std::list<Entry> entries_;
};

} // namespace firestone::vm
