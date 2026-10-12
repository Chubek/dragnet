#include <mlvm/mlvm.hpp>
#include <mlvm/object.hpp>
#ifdef MLVM_TEST_BINUTILS
#include <mlvm/binutils.hpp>
#endif
#ifdef MLVM_TEST_CEPHYR
#include <cephyr/frontend.hpp>
#endif
int main() {
    mlvm::Context context;
    mlvm::DiagnosticEngine diagnostics;
    if (!mlvm::register_builtin_dialects(context, diagnostics)) return 1;
    mlvm::Module module(context);
    if (!mlvm::verify(module, diagnostics)) return 2;
    if (!mlvm::write_elf(mlvm::Object{}, diagnostics)) return 3;
#ifdef MLVM_TEST_BINUTILS
    mlvm::BinaryObject object;
    object.sections.push_back({".bss", 8, 3, 8, 0, 16, {}});
    auto encoded = mlvm::write_binary_object(object, diagnostics);
    if (!encoded || !mlvm::read_binary_object(*encoded, diagnostics)) return 5;
    const std::vector<mlvm::ArchiveMember> members{{"long archive member name.bin", {1, 2, 3}}};
    if (!mlvm::write_indexed_archive(members, diagnostics)) return 6;
#endif
#ifdef MLVM_TEST_CEPHYR
    mlvm::SourceManager sources;
    if (!cephyr::lower(context, sources, sources.add("consumer.c", "int answer(void) { return 42; }"), diagnostics)) return 4;
#endif
    return 0;
}
