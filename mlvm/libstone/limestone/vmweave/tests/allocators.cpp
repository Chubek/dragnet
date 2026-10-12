#include "vmweave/adapters/memtkx.hpp"
#include <dommemtk/space/free_list.hpp>
#include "tests/test.hpp"
struct Allocator {
  void* context;
  void* (*allocate)(void*,size_t);
  void* (*reallocate)(void*,void*,size_t);
  void (*release)(void*,void*);
};
int main() {return test_main([] {
  alignas(std::max_align_t) unsigned char bytes[4096];
  DomMEMTk::FreeListAllocator space(reinterpret_cast<uintptr_t>(bytes),reinterpret_cast<uintptr_t>(bytes)+sizeof(bytes));
  limestone::vmweave::adapters::Memtkx adapter(space);
  auto a=adapter.bind<Allocator>();
  void* value=a.allocate(a.context,32); CHECK(value && adapter.live_allocations()==1);
  std::memset(value,42,32);
  void* larger=a.reallocate(a.context,value,64); CHECK(larger && static_cast<unsigned char*>(larger)[31]==42);
  CHECK(adapter.live_allocations()==1);
  CHECK(!a.reallocate(a.context,larger,8192)); CHECK(adapter.live_allocations()==1);
  a.release(a.context,larger); CHECK(adapter.live_allocations()==0 && space.free_bytes()==sizeof(bytes));
});}
