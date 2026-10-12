#include <cephyr/frontend.hpp>
#include <mlvm/pass.hpp>
#include <cctype>
#include <sstream>

namespace cephyr {
std::optional<std::string> emit_assembly(const mlvm::Module& input, mlvm::DiagnosticEngine& diagnostics) {
    auto module = input;
    mlvm::PassManager passes; passes.add(mlvm::constant_fold_pass()); passes.add(mlvm::dead_code_elimination_pass());
    if (!passes.run(module, diagnostics)) return std::nullopt;
    std::ostringstream out; out << ".text\n";
    for (const auto& top : module.body.blocks) for (const auto& fn : top.operations) {
        auto fail = [&]() -> std::optional<std::string> {
            diagnostics.error("codegen.unsupported", "x86-64 codegen requires a no-argument i32 constant-return function", fn.location); return std::nullopt;
        };
        if (fn.name != "builtin.func" || fn.regions.size() != 1 || fn.regions[0].blocks.size() != 1 || !fn.attributes.contains("sym")) return fail();
        const auto& name = fn.attributes.at("sym");
        if (name.empty() || !(std::isalpha(static_cast<unsigned char>(name.front())) || name.front() == '_')) return fail();
        for (unsigned char c : name) if (!(std::isalnum(c) || c == '_')) return fail();
        const auto& block = fn.regions[0].blocks[0];
        if (!block.arguments.empty() || block.operations.size() != 2) return fail();
        const auto& value = block.operations[0]; const auto& ret = block.operations[1];
        if (value.name != "arith.constant" || value.results.size() != 1 || !value.attributes.contains("value") ||
            module.context->spelling(module.value(value.results[0])->type) != "i32" || ret.name != "func.return" || ret.operands != value.results) return fail();
        out << ".globl " << name << "\n.type " << name << ", @function\n" << name << ":\n  mov $" << value.attributes.at("value") << ", %eax\n  ret\n.size " << name << ", .-" << name << '\n';
    }
    return out.str();
}
} // namespace cephyr
