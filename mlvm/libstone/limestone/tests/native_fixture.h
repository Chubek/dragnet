#ifndef LIMESTONE_NATIVE_FIXTURE_H
#define LIMESTONE_NATIVE_FIXTURE_H
#include "exolayer.h"
struct native_fixture_state {
  exl_context_t *context;
  exl_library_t *library;
  int (*close)(exl_context_t *,exl_library_t *);
};
#endif
