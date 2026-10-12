#!/bin/bash
# AFL++ fuzzing script for Dragscript

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build_fuzz"
CORPUS_DIR="$SCRIPT_DIR/corpus"
DICT_FILE="$SCRIPT_DIR/dictionary.txt"
OUTPUT_DIR="$SCRIPT_DIR/output"
FUZZER="$SCRIPT_DIR/fuzz_compile"

# Build with AFL++ instrumentation
build_fuzzer() {
    echo "Building fuzzer with AFL++..."
    mkdir -p "$BUILD_DIR"
    cd "$BUILD_DIR"
    
    # Use AFL++ clang for instrumentation
    CC=afl-clang-fast CXX=afl-clang-fast++ cmake "$PROJECT_ROOT/dragscript" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined" \
        -DDRAGSCRIPT_BUILD_TESTS=OFF \
        -DDRAGSCRIPT_DIFFERENTIAL_TESTS=OFF \
        -DDRAGSCRIPT_SANITIZERS=OFF
    
    # Build the fuzzer
    CXX=afl-clang-fast++ cmake --build . --target dragscript -j$(nproc)
    
    # Compile the fuzzer
    afl-clang-fast++ -std=c++20 -I"$PROJECT_ROOT/dragscript/include" \
        -I"$PROJECT_ROOT/dragscript/src" \
        -I"$PROJECT_ROOT/dragscript/src/mlvm" \
        -I"$PROJECT_ROOT/dragscript/src/runtime" \
        -I"$PROJECT_ROOT/dragscript/src/semantic" \
        -I"$PROJECT_ROOT/dragscript/src/frontend" \
        -O2 -fsanitize=address,undefined \
        -o "$FUZZER" "$SCRIPT_DIR/fuzz_compile.cc" \
        -L"$BUILD_DIR" -ldragscript \
        -L"$BUILD_DIR/dependencies/mlvm" -lmlvm_ir -lmlvm_core \
        -ltree-sitter -ltree-sitter-typescript -ltree-sitter-tsx
    
    echo "Fuzzer built at $FUZZER"
}

# Minimize corpus
minimize_corpus() {
    echo "Minimizing corpus..."
    afl-cmin -i "$CORPUS_DIR" -o "$CORPUS_DIR.min" -- "$FUZZER" @@
    mv "$CORPUS_DIR.min" "$CORPUS_DIR"
}

# Run fuzzer
run_fuzzer() {
    local mode="${1:-explore}"
    echo "Running AFL++ fuzzer in $mode mode..."
    
    mkdir -p "$OUTPUT_DIR"
    
    # Run AFL++
    afl-fuzz -i "$CORPUS_DIR" -o "$OUTPUT_DIR" \
        -x "$DICT_FILE" \
        -t 5000 \
        -m none \
        -P "$mode" \
        -- "$FUZZER" @@
}

# Show help
usage() {
    echo "Usage: $0 [build|minimize|fuzz|all] [mode]"
    echo "  build     - Build the fuzzer with AFL++ instrumentation"
    echo "  minimize  - Minimize corpus with afl-cmin"
    echo "  fuzz      - Run AFL++ fuzzer (default mode: explore)"
    echo "  all       - Build, minimize, and fuzz"
    echo ""
    echo "Modes for fuzz: explore (default), fast, exploit, seek, rare, mmopt, coe, lin, quad"
}

# Main
case "${1:-all}" in
    build)
        build_fuzzer
        ;;
    minimize)
        minimize_corpus
        ;;
    fuzz)
        run_fuzzer "${2:-explore}"
        ;;
    all)
        build_fuzzer
        minimize_corpus
        run_fuzzer "${2:-explore}"
        ;;
    *)
        usage
        exit 1
        ;;
esac