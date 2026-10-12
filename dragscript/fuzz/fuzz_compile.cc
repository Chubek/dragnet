// Fuzzer for Dragscript compilation and execution
// Compile with: afl-clang-fast++ -std=c++20 -Iinclude -Isrc -Isrc/mlvm -Isrc/runtime -Isrc/semantic -Isrc/frontend -O2 -fsanitize=address,undefined -o fuzz_compile fuzz_compile.cc -L/path/to/build -ldragscript -lmlvm_ir -lmlvm_core -ltree-sitter -ltree-sitter-typescript -ltree-sitter-tsx

#include <dragscript/engine.hpp>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size == 0 || size > 1024 * 1024) return 0;
    
    // Create a null-terminated copy for parsing
    char* source = new char[size + 1];
    std::memcpy(source, data, size);
    source[size] = '\0';
    
    dragscript::Engine engine;
    dragscript::Source src{"fuzz.ts", source, dragscript::SourceKind::typescript, "fuzz://input"};
    
    // Compile with limits
    dragscript::CompileOptions compile_opts;
    compile_opts.cancellation = nullptr;
    
    auto compiled = engine.compile(src, compile_opts);
    delete[] source;
    
    if (!compiled.ok()) return 0;
    
    // Run with tight limits
    dragscript::RunOptions run_opts;
    run_opts.max_steps = 10000;
    run_opts.max_call_depth = 32;
    run_opts.max_evaluation_depth = 128;
    run_opts.max_heap_bytes = 1024 * 1024;
    
    auto result = engine.run(compiled.script, run_opts);
    return 0;
}

// Standalone main for AFL++ standalone mode
int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <input_file>" << std::endl;
        return 1;
    }
    
    std::ifstream file(argv[1], std::ios::binary | std::ios::ate);
    if (!file) {
        std::cerr << "Failed to open: " << argv[1] << std::endl;
        return 1;
    }
    
    size_t size = file.tellg();
    file.seekg(0, std::ios::beg);
    
    std::vector<uint8_t> buffer(size);
    file.read(reinterpret_cast<char*>(buffer.data()), size);
    
    return LLVMFuzzerTestOneInput(buffer.data(), size);
}