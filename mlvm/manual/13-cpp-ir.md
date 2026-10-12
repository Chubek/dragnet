# 13. Building and processing IR with C++

[Contents](README.md) · [Next: C API](14-c-api.md)

Link this complete program to `MLVM::IR` using the consumer setup in chapter 4.
It parses a module, runs constant folding, reports diagnostics, and prints the
canonical result.

```cpp
#include <mlvm/ir.hpp>
#include <mlvm/pass.hpp>
#include <iostream>

int main() {
    mlvm::Context context;
    mlvm::SourceManager sources;
    mlvm::DiagnosticEngine diagnostics;
    auto report = [&] {
        for (const auto& d : diagnostics.diagnostics())
            std::cerr << sources.format(d) << '\n';
    };
    if (!mlvm::register_builtin_dialects(context, diagnostics)) {
        report();
        return 1;
    }
    auto file = sources.add("example.mlvm", R"IR(
mlvm 1 {
  block() {
    %a:"i32" = "arith.constant"() ["value"="6"];
    %b:"i32" = "arith.constant"() ["value"="7"];
    %c:"i32" = "arith.muli"(%a, %b) [];
  }
}
)IR");
    auto module = mlvm::parse_ir(context, sources, file, diagnostics);
    if (!module) { report(); return 1; }
    mlvm::PassManager passes;
    passes.add(mlvm::constant_fold_pass());
    if (!passes.run(*module, diagnostics)) { report(); return 1; }
    std::cout << mlvm::print_ir(*module);
}
```

The result contains a constant `42`. This program deliberately omits DCE so that
the unused top-level arithmetic remains visible for inspection.

## Constructing instead of parsing

Use `Module::add_block` and `Module::append` with spans of operand IDs and result
type identifiers. `append` returns a reference to the created operation; set its
attributes before further vector mutations invalidate that reference. Save its
result IDs to connect later operations.

Building through C++ avoids text parsing but does not bypass verification or
dialect registration. The context must outlive the returned module, including
copies stored by the application.

Reference: [`ir.hpp`](../include/mlvm/ir.hpp), [`pass.hpp`](../include/mlvm/pass.hpp).
