#include <mlvm/c_api.h>
#include <mlvm/core.hpp>
#include <new>

struct mlvm_context { mlvm::Context implementation; };
extern "C" {
const char* mlvm_version(void) { return mlvm::version.data(); }
mlvm_status mlvm_context_create(mlvm_context** output) {
    if (!output) return MLVM_STATUS_INVALID_ARGUMENT;
    *output = nullptr;
    try { *output = new mlvm_context; return MLVM_STATUS_OK; }
    catch (const std::bad_alloc&) { return MLVM_STATUS_OUT_OF_MEMORY; }
    catch (...) { return MLVM_STATUS_INTERNAL_ERROR; }
}
void mlvm_context_destroy(mlvm_context* context) { delete context; }
mlvm_status mlvm_context_intern(mlvm_context* context, const char* text, uint32_t* output) {
    if (output) *output = 0;
    if (!context || !text || !output) return MLVM_STATUS_INVALID_ARGUMENT;
    try { *output = context->implementation.intern(text); return MLVM_STATUS_OK; }
    catch (const std::bad_alloc&) { return MLVM_STATUS_OUT_OF_MEMORY; }
    catch (...) { return MLVM_STATUS_INTERNAL_ERROR; }
}
const char* mlvm_context_spelling(const mlvm_context* context, uint32_t id) {
    if (!context) return nullptr;
    return context->implementation.spelling(id).data();
}
}
