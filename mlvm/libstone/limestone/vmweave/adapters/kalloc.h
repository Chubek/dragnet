#pragma once
/* Include after the generated VM header and klib's kalloc.h. The arena is
   borrowed; initialize/destroy it in the embedding application. */
#include <kalloc.h>
#define VMWEAVE_KALLOC_ADAPTER(NAME) \
  static inline int NAME##_memory_kalloc(NAME##_vm *vm, void *arena) { \
    NAME##_allocator adapter = {arena, kmalloc, krealloc, kfree}; \
    return NAME##_memory_set(vm, adapter); \
  }
