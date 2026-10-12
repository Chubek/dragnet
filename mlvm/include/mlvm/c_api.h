#ifndef MLVM_C_API_H
#define MLVM_C_API_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct mlvm_context mlvm_context;
typedef enum mlvm_status {
    MLVM_STATUS_OK = 0,
    MLVM_STATUS_INVALID_ARGUMENT = 1,
    MLVM_STATUS_OUT_OF_MEMORY = 2,
    MLVM_STATUS_PARSE_ERROR = 3,
    MLVM_STATUS_VERIFICATION_ERROR = 4,
    MLVM_STATUS_UNSUPPORTED = 5,
    MLVM_STATUS_INTERNAL_ERROR = 6
} mlvm_status;

/* The version string has static lifetime. Create sets *out_context to NULL on
   failure. A successful context is caller-owned; destroy accepts NULL. No C++
   exceptions cross this API. Contexts are not internally synchronized. */
const char *mlvm_version(void);
mlvm_status mlvm_context_create(mlvm_context **out_context);
void mlvm_context_destroy(mlvm_context *context);
/* Interned strings are borrowed, valid until destroy. ID zero is invalid. */
mlvm_status mlvm_context_intern(mlvm_context *context, const char *text, uint32_t *out_id);
const char *mlvm_context_spelling(const mlvm_context *context, uint32_t id);

#ifdef __cplusplus
}
#endif
#endif
