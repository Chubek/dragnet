# 14. The C embedding API

[Contents](README.md) · [Next: Custom dialects](15-custom-dialects.md)

The implemented C ABI is intentionally small: version reporting, context
creation/destruction, and string interning. It does not currently expose C
module handles, pass pipelines, binary objects, or kernel execution.

This complete C program uses `mlvm/c_api.h`:

```c
#include <mlvm/c_api.h>
#include <stdio.h>

int main(void) {
    mlvm_context *context = NULL;
    uint32_t id = 0;
    if (mlvm_context_create(&context) != MLVM_STATUS_OK)
        return 1;
    if (mlvm_context_intern(context, "i32", &id) != MLVM_STATUS_OK) {
        mlvm_context_destroy(context);
        return 1;
    }
    const char *spelling = mlvm_context_spelling(context, id);
    if (spelling == NULL) {
        mlvm_context_destroy(context);
        return 1;
    }
    printf("MLVM %s: %s\n", mlvm_version(), spelling);
    mlvm_context_destroy(context);
    return 0;
}
```

Compile the source as C and link through the CMake `MLVM::Core` target in a
project enabling both C and CXX. For a C-only executable using the static C++
library, explicitly selecting `LINKER_LANGUAGE CXX` is a straightforward way to
include the C++ runtime through the configured toolchain.

## Failure and ownership rules

Successful creation gives the caller an owned context. Creation resets the
output handle on failure; interning resets the output ID on failure. ID zero is
invalid. `mlvm_context_destroy(NULL)` is valid.

Interned strings are borrowed, NUL-terminated, and valid until context
destruction. Invalid spelling IDs return `NULL`; the version string has static
lifetime. C++ exceptions do not cross the ABI. Check status values rather than
assuming allocation or interning always succeeds.

Reference: [C declarations](../include/mlvm/c_api.h), [C regression](../tests/core/c_api.c).
