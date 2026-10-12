#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <unordered_map>

namespace limestone::vmweave::adapters {
// Adapter for DomMEMTk nonmoving spaces (e.g. FreeListAllocator), whose free
// operation needs the allocation size. Moving GC plans need a VM-specific root
// and relocation adapter instead. No DomMEMTk implementation type leaks into the
// VM model or generator library.
template<class Space> class Memtkx {
  Space& space_;
  std::unordered_map<void*, size_t> allocations_;
  static void* allocate(void* context,size_t bytes) noexcept {
    auto& self=*static_cast<Memtkx*>(context);
    try {
      auto result=self.space_.allocate(bytes,alignof(std::max_align_t));
      if(!result.is_ok()) return nullptr;
      void* value=reinterpret_cast<void*>(result.unwrap());
      try { self.allocations_.emplace(value,bytes); }
      catch(...) {self.space_.free(reinterpret_cast<uintptr_t>(value),bytes); return nullptr;}
      return value;
    } catch(...) {return nullptr;}
  }
  static void release(void* context,void* value) noexcept {
    auto& self=*static_cast<Memtkx*>(context);
    auto found=self.allocations_.find(value); if(found==self.allocations_.end()) return;
    try {
      if(self.space_.free(reinterpret_cast<uintptr_t>(value),found->second)) self.allocations_.erase(found);
    } catch(...) {}
  }
  static void* resize(void* context,void* value,size_t bytes) noexcept {
    auto& self=*static_cast<Memtkx*>(context);
    if(!value) return allocate(context,bytes);
    auto found=self.allocations_.find(value); if(found==self.allocations_.end()) return nullptr;
    if(!bytes) {release(context,value); return nullptr;}
    const auto old_size=found->second;
    void* replacement=allocate(context,bytes); if(!replacement) return nullptr;
    std::memcpy(replacement,value,std::min(old_size,bytes)); release(context,value); return replacement;
  }
public:
  explicit Memtkx(Space& space):space_(space) {}
  Memtkx(const Memtkx&)=delete;
  Memtkx& operator=(const Memtkx&)=delete;
  ~Memtkx() {
    for(const auto& [value,bytes]:allocations_) {
      try {space_.free(reinterpret_cast<uintptr_t>(value),bytes);} catch(...) {}
    }
  }
  template<class Allocator> Allocator bind() noexcept {return {this,allocate,resize,release};}
  size_t live_allocations() const noexcept {return allocations_.size();}
};
}
