#pragma once
/* Use the explicit extended API to avoid depending on malloc interposition. */
#include <jemalloc/jemalloc.h>
#define VMWEAVE_JEMALLOC_ADAPTER(NAME) \
  static void *NAME##_vw_je_allocate(void *context, size_t bytes) { (void)context; return bytes ? mallocx(bytes, 0) : NULL; } \
  static void *NAME##_vw_je_resize(void *context, void *value, size_t bytes) { \
    (void)context; if(!bytes) { if(value) dallocx(value, 0); return NULL; } \
    return value ? rallocx(value, bytes, 0) : mallocx(bytes, 0); \
  } \
  static void NAME##_vw_je_free(void *context, void *value) { (void)context; if(value) dallocx(value, 0); } \
  static inline int NAME##_memory_jemalloc(NAME##_vm *vm) { \
    NAME##_allocator adapter = {NULL, NAME##_vw_je_allocate, NAME##_vw_je_resize, NAME##_vw_je_free}; \
    return NAME##_memory_set(vm, adapter); \
  }
