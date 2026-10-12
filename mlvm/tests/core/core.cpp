#include "../test.hpp"
#include <mlvm/mlvm.hpp>

int main() { return test_main([] {
    mlvm::Context context;
    auto id = context.intern("stable"); const auto* data = context.spelling(id).data();
    for (unsigned i = 0; i < 10000; ++i) context.intern(std::to_string(i));
    CHECK(context.intern("stable") == id);
    CHECK(data == context.spelling(id).data()); CHECK(context.spelling(0).empty());
    mlvm::SourceManager sources;
    const auto file = sources.add("test.c", "a\nbc\n");
    CHECK((sources.location(file, 2) == mlvm::SourceLocation{file, 2, 1}));
    CHECK((sources.location(file, 5) == mlvm::SourceLocation{file, 3, 1}));
    CHECK(sources.location(file, 6).file_id == 0); CHECK(!sources.file(0));
    mlvm::Diagnostic diagnostic{mlvm::Severity::error, "test.code", "message", {file, 2, 1}, {{{file, 1, 1}, "related"}}, {}};
    CHECK(sources.format(diagnostic) == "test.c:2:1: error[test.code]: message\ntest.c:1:1: note: related");
    mlvm::DiagnosticEngine diagnostics;
    CHECK(mlvm::register_builtin_dialects(context, diagnostics));
    CHECK(mlvm::register_builtin_dialects(context, diagnostics));
    CHECK(!context.register_dialect("arith", diagnostics));
    CHECK(!context.register_operation({"missing.op"}, diagnostics));
    CHECK(diagnostics.has_error()); diagnostics.clear(); CHECK(!diagnostics.has_error());
}); }
