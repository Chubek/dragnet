#pragma once
#include <mimalloc.h>
#define VMWEAVE_MIMALLOC_ADAPTER(NAME) \
  static void *NAME##_vw_mi_allocate(void *context, size_t bytes) { (void)context; return mi_malloc(bytes); } \
  static void *NAME##_vw_mi_resize(void *context, void *value, size_t bytes) { (void)context; return mi_realloc(value, bytes); } \
  static void NAME##_vw_mi_free(void *context, void *value) { (void)context; mi_free(value); } \
  static inline int NAME##_memory_mimalloc(NAME##_vm *vm) { \
    NAME##_allocator adapter = {NULL, NAME##_vw_mi_allocate, NAME##_vw_mi_resize, NAME##_vw_mi_free}; \
    return NAME##_memory_set(vm, adapter); \
  }
