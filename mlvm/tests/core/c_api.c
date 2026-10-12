#include <mlvm/c_api.h>
#include <string.h>

int main(void) {
    mlvm_context *context = 0;
    uint32_t first = 0, second = 0;
    if (mlvm_context_create(0) != MLVM_STATUS_INVALID_ARGUMENT) return 1;
    if (mlvm_context_create(&context) != MLVM_STATUS_OK || !context) return 2;
    if (mlvm_context_intern(context, "name", &first) != MLVM_STATUS_OK) return 3;
    if (mlvm_context_intern(context, "name", &second) != MLVM_STATUS_OK || first != second) return 4;
    if (strcmp(mlvm_context_spelling(context, first), "name") || !strlen(mlvm_version())) return 5;
    if (mlvm_context_spelling(context, 0) || mlvm_context_spelling(0, first)) return 6;
    if (mlvm_context_intern(context, 0, &first) != MLVM_STATUS_INVALID_ARGUMENT || first) return 7;
    mlvm_context_destroy(context); mlvm_context_destroy(0);
    return 0;
}
