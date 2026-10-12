// Persistent mode fuzzer for Dragscript - uses AFL++ persistent mode for speed
// Compile with: afl-clang-fast++ -std=c++20 -Iinclude -Isrc -Isrc/mlvm -Isrc/runtime -Isrc/semantic -Isrc/frontend -O2 -fsanitize=address,undefined -o fuzz_persistent fuzz_persistent.cc -L/path/to/build -ldragscript -lmlvm_ir -lmlvm_core -ltree-sitter -ltree-sitter-typescript -ltree-sitter-tsx

#include <dragscript/engine.hpp>
#include <cstdint>
#include <cstdlib>
#include <cstring>

static dragscript::Engine* g_engine = nullptr;
static dragscript::EngineOptions g_options;

extern "C" int LLVMFuzzerInitialize(int* argc, char*** argv) {
    g_options.max_string_units = 1024 * 1024;
    g_options.max_array_length = 65536;
    g_options.max_properties_per_object = 4096;
    g_options.max_property_name_units = 256;
    g_engine = new dragscript::Engine(g_options);
    return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size == 0 || size > 1024 * 1024) return 0;
    
    char* source = new char[size + 1];
    std::memcpy(source, data, size);
    source[size] = '\0';
    
    dragscript::Source src{"fuzz.ts", source, dragscript::SourceKind::typescript, "fuzz://input"};
    
    dragscript::CompileOptions compile_opts;
    auto compiled = g_engine->compile(src, dragscript::CompileOptions{});
    delete[] source;
    
    if (!compiled.ok()) return 0;
    
    dragscript::RunOptions run_opts;
    run_opts.max_steps = 10000;
    run_opts.max_call_depth = 32;
    run_opts.max_evaluation_depth = 128;
    run_opts.max_heap_bytes = 1024 * 1024;
    
    auto result = g_engine->run(compiled.script, run_opts);
    return 0;
}

// For AFL++ persistent mode
#ifdef __AFL_HAVE_MANUAL_CONTROL
extern "C" int main(int argc, char** argv) {
    LLVMFuzzerInitialize(nullptr, nullptr);
    
    #ifdef __AFL_INIT
    __AFL_INIT();
    #endif
    
    unsigned char* buf = __AFL_FUZZ_TESTCASE_BUF;
    while (__AFL_LOOP(1000)) {
        size_t len = __AFL_FUZZ_TESTCASE_LEN;
        if (len > 0) {
            LLVMFuzzerTestOneInput(buf, len);
        }
    }
    
    delete g_engine;
    return 0;
}
#endif