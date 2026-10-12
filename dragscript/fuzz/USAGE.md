# AFL++ Fuzzing for Dragscript

## Quick Start

```bash
# Build with AFL++ instrumentation
CC=afl-clang-fast CXX=afl-clang-fast++ cmake -B build_fuzz -S dragscript -DFUZZING=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build_fuzz -j$(nproc)

# Prepare corpus
mkdir -p fuzz/corpus
# (Add seed files to fuzz/corpus/)

# Run AFL++ fuzzer
afl-fuzz -i fuzz/corpus -o fuzz/output -x fuzz/dictionary.txt -- build_fuzz/fuzz_compile @@

# Or use persistent mode for speed
afl-fuzz -i fuzz/corpus -o fuzz/output -x fuzz/dictionary.txt -- build_fuzz/fuzz_persistent_mode @@
```

## Build Configurations

### Standard AFL++ Build
```bash
CC=afl-clang-fast CXX=afl-clang-fast++ cmake -B build_fuzz -S dragscript -DFUZZING=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build_fuzz -j$(nproc)
```

### ASan + UBSan + AFL++ (recommended for finding bugs)
```bash
CC=afl-clang-fast CXX=afl-clang-fast++ cmake -B build_fuzz -S dragscript \
    -DFUZZING=ON -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_FLAGS="-fsanitize=address,undefined" \
    -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined" \
    -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
cmake --build build_fuzz -j$(nproc)
```

### With MSan (memory sanitizer)
```bash
CC=afl-clang-fast CXX=afl-clang-fast++ cmake -B build_fuzz -S dragscript \
    -DFUZZING=ON -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_C_FLAGS="-fsanitize=memory -fno-omit-frame-pointer" \
    -DCMAKE_CXX_FLAGS="-fsanitize=memory -fno-omit-frame-pointer" \
    -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=memory"
cmake --build build_fuzz -j$(nproc)
```

## Corpus Management

### Seed Files
Place seed files in `dragscript/fuzz/corpus/`:
- `empty.ts` - empty file
- `simple.ts` - basic arithmetic
- `function.ts` - function declarations
- `loops.ts` - while/for/do-while loops
- `objects.ts` - object literals and property access
- `arrays.ts` - array literals and indexing
- `closures.ts` - closure functions
- `strings.ts` - string operations
- `conditionals.ts` - if/else statements
- `break_continue.ts` - break/continue in loops
- `for_of.ts` - for-of iteration
- `closures.ts` - closure functions
- `recursion.ts` - recursive functions
- `edge_cases.ts` - NaN, Infinity, increment/decrement
- `typed.ts` - TypeScript type annotations
- `comparisons.ts` - comparison operators
- `recursion.ts` - recursive functions

### Minimize Corpus
```bash
afl-cmin -i fuzz/corpus -o fuzz/corpus.min -- build/fuzz_compile @@
mv fuzz/corpus.min fuzz/corpus
```

### Add New Seeds from Crashes
```bash
afl-cmin -i fuzz/output/crashes -o fuzz/corpus_new -- build_fuzz/fuzz_compile @@
cp fuzz/corpus_new/* fuzz/corpus/
```

## Fuzzing Modes

### Standard Mode
```bash
afl-fuzz -i fuzz/corpus -o fuzz/output -x fuzz/dictionary.txt -- build/fuzz_compile @@
```

### Persistent Mode (faster)
```bash
afl-fuzz -i fuzz/corpus -o fuzz/output -x fuzz/dictionary.txt -- build_fuzz/fuzz_persistent_mode @@
```

### Power Schedules
```bash
# Explore mode (default, good for new coverage)
afl-fuzz -P explore ...

# Fast mode (quick initial exploration)
afl-fuzz -P fast ...

# Exploit mode (focus on crashes)
afl-fuzz -P exploit ...

# COE (coverage-oriented)
afl-fuzz -P coe ...
```

## Crash Triage

### Minimize Crash Input
```bash
afl-tmin -i crash_input -o minimized_input -- build/fuzz_compile @@
```

### Debug Crash
```bash
gdb --args build/fuzz_compile crash_input
# Or with sanitizer output
ASAN_OPTIONS=symbolize=1 build/fuzz_compile crash_input
```

## Dictionary

The dictionary file (`fuzz/dictionary.txt`) contains TypeScript/JavaScript keywords and operators. This helps AFL++ generate syntactically valid inputs faster.

## Continuous Fuzzing

### Run Multiple Instances
```bash
# Master instance
afl-fuzz -M main -i corpus -o output -- build/fuzz_compile @@

# Secondary instances
afl-fuzz -S secondary1 -i corpus -o output -- build/fuzz_compile @@
afl-fuzz -S secondary2 -i corpus -o output -- build/fuzz_compile @@
```

### Resume Fuzzing
```bash
afl-fuzz -i - -o output -- build/fuzz_compile @@
```

## Parallel Fuzzing on Multiple Cores

```bash
# Use all available cores
for i in {1..$(nproc)}; do
    if [ $i -eq 1 ]; then
        afl-fuzz -M fuzzer$i -i corpus -o output -- build/fuzz_compile @@ &
    else
        afl-fuzz -S fuzzer$i -i corpus -o output -- build/fuzz_compile @@ &
    fi
done
```

## Crash Analysis

### Classify Crashes
```bash
# Use afl-collect to group similar crashes
afl-collect -i output/crashes -o classified -- build/fuzz_compile @@

# Or use afl-whatsup to monitor
afl-whatsup output/
```

### ASan Symbolication
```bash
# Make sure llvm-symbolizer is in PATH
export ASAN_OPTIONS=symbolize=1:external_symbolizer_path=$(which llvm-symbolizer)
./build/fuzz_compile crash_input
```

## Troubleshooting

### Low Coverage
- Add more diverse seed files to corpus
- Increase timeout with `-t`
- Use dictionary with `-x`
- Try different power schedules

### Low Execution Speed
- Use persistent mode (`fuzz_persistent_mode`)
- Reduce timeout with `-t`
- Increase memory limit with `-m`
- Use `fast` power schedule initially

### Crashes Not Found
- Verify the crash is reproducible: `./build/fuzz_compile crash_file`
- Check if it's a timeout vs actual crash
- Ensure sanitizers are enabled

## Integration with CI

```yaml
# GitHub Actions example
- name: Run AFL++ fuzzing
  run: |
    CC=afl-clang-fast CXX=afl-clang-fast++ cmake -B build -S dragscript -DFUZZING=ON
    cmake --build build -j4
    timeout 3600 afl-fuzz -i fuzz/corpus -o fuzz/output -x fuzz/dictionary.txt -- build/fuzz_compile @@ || true
    # Check for crashes
    if [ -d "fuzz/output/crashes" ] && [ "$(ls -A fuzz/output/crashes)" ]; then
      echo "Crashes found!"
      exit 1
    fi
```

## Tips for Better Coverage

1. **Diverse seeds**: Include files covering all language features
2. **Valid and invalid inputs**: Mix valid TypeScript with syntax errors
3. **Incremental complexity**: Start with simple cases, add complex ones
4. **TypeScript features**: Include type annotations, generics, interfaces
5. **Edge cases**: Empty files, very long lines, deep nesting, unicode

## Monitoring Progress

```bash
# Watch progress
watch -n 5 'afl-whatsup fuzz/output'

# Or tail the fuzzer stats
tail -f fuzz/output/default/fuzzer_stats
```

## Reporting Bugs

When a crash is found:
1. Minimize with `afl-tmin`
2. Verify it's not a false positive (run multiple times)
3. Check if it's a known issue
4. Report with minimized test case and stack trace