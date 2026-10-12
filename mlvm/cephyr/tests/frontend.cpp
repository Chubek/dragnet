#include "../../tests/test.hpp"
#include <cephyr/frontend.hpp>

int main() { return test_main([] {
    mlvm::SourceManager sources; mlvm::DiagnosticEngine diagnostics; mlvm::Context context;
    auto preprocess = [&](std::string code) { return cephyr::preprocess(sources, sources.add("test.c", std::move(code)), {}, diagnostics); };
    auto expanded = preprocess("#define A 6\n#define B (A * 7)\n#ifdef A\nint answer(void) { return B; }\n#else\ninvalid\n#endif\n");
    CHECK(expanded); CHECK(expanded->find("(6 * 7)") != std::string::npos); CHECK(expanded->find("invalid") == std::string::npos);
    auto file = sources.add("expanded.c", *expanded); CHECK(cephyr::parse(sources, file, diagnostics));
    auto module = cephyr::lower(context, sources, file, diagnostics); CHECK(module);
    auto assembly = cephyr::emit_assembly(*module, diagnostics); CHECK(assembly); CHECK(assembly->find("mov $42, %eax") != std::string::npos);
    expanded = preprocess("#define A B\n#define B A\nA \"A\" /* A */\n"); CHECK(expanded); CHECK(expanded->find("A \"A\"") != std::string::npos);
    expanded = preprocess("#define xFF 42\n0xFF\n"); CHECK(expanded); CHECK(expanded->find("0xFF") != std::string::npos);
    expanded = preprocess("#if 0\n#if UNSUPPORTED\nignored\n#endif\n#endif\n"); CHECK(expanded);
    for (const auto* code : {"#define F(x) x\n", "#if 1\n", "#if 0\n#elif 1\n#endif\n", "/*", "#include \"missing-header.h\"\n"}) {
        CHECK(!preprocess(code)); CHECK(diagnostics.has_error()); diagnostics.clear();
    }
    for (const auto* code : {"int f(void) { return ;", "int f(void) { return 2147483647 + 1; }", "int f(int x) { return x; }",
                            "unsigned int f(void) { return 1; }", "int f(void) { return 1 / 0; }", "int f(void) { return missing; }",
                            "int f(void) { return 1; } int f(void) { return 2; }"}) {
        CHECK(!cephyr::lower(context, sources, sources.add("bad.c", code), diagnostics)); CHECK(diagnostics.has_error()); diagnostics.clear();
    }
    auto general = sources.add("parse.c", "struct S { int x; }; int f(int x) { return x + 1; }");
    CHECK(cephyr::parse(sources, general, diagnostics));
}); }
