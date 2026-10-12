#include <mlvm/ir.hpp>
#include <charconv>
#include <limits>

namespace mlvm {
bool register_builtin_dialects(Context& context, DiagnosticEngine& diagnostics) {
    bool ok = true;
    for (const auto* name : {"builtin", "arith", "func"})
        if (!context.has_dialect(name)) ok = context.register_dialect(name, diagnostics) && ok;
    auto add = [&](OperationDefinition def) {
        if (!context.operation(def.name)) ok = context.register_operation(std::move(def), diagnostics) && ok;
    };
    auto integer = [](const Operation& op, const Module& module, DiagnosticEngine& diagnostics) {
        Identifier type = 0;
        for (auto id : op.results) if (const auto* v = module.value(id)) type = v->type;
        bool valid = type != 0;
        const auto spelling = module.context->spelling(type);
        unsigned width = 0;
        if (spelling.size() < 2 || spelling[0] != 'i') valid = false;
        else {
            const auto result = std::from_chars(spelling.data() + 1, spelling.data() + spelling.size(), width);
            if (result.ec != std::errc{} || result.ptr != spelling.data() + spelling.size() || width == 0 || width > 64) valid = false;
        }
        for (auto id : op.operands) {
            const auto* v = module.value(id); if (!v || v->type != type) valid = false;
        }
        if (!valid) diagnostics.error("arith.type", "integer arithmetic requires identical i1..i64 operand/result types", op.location);
        return valid;
    };
    auto constant = [integer](const Operation& op, const Module& module, DiagnosticEngine& diagnostics) {
        bool valid = integer(op, module, diagnostics);
        const auto it = op.attributes.find("value");
        std::int64_t number = 0;
        if (it == op.attributes.end()) valid = false;
        else {
            const auto& s = it->second; const auto r = std::from_chars(s.data(), s.data() + s.size(), number);
            if (r.ec != std::errc{} || r.ptr != s.data() + s.size()) valid = false;
            if (valid && !op.results.empty()) {
                auto spelling = module.context->spelling(module.value(op.results[0])->type);
                unsigned width = 0; std::from_chars(spelling.data() + 1, spelling.data() + spelling.size(), width);
                if (width < 64 && (number < -(std::int64_t{1} << (width - 1)) ||
                                  (number >= 0 && static_cast<std::uint64_t>(number) >= (std::uint64_t{1} << width)))) valid = false;
            }
        }
        if (!valid) diagnostics.error("arith.constant", "constant requires a representable decimal 'value' attribute", op.location);
        return valid;
    };
    add({"builtin.func", 0, 0, 0, 0, 1, 1, Effect::unknown, false,
        [](const Operation& op, const Module&, DiagnosticEngine& d) {
            if (op.regions.size() != 1 || op.regions[0].blocks.size() != 1) {
                d.error("func.body", "builtin.func currently requires exactly one entry block", op.location); return false;
            }
            return true;
        }});
    add({"func.return", 0, 1, 0, 0, 0, 0, Effect::unknown, true, {}});
    add({"arith.constant", 0, 0, 1, 1, 0, 0, Effect::pure, false, constant});
    for (auto name : {"arith.addi", "arith.subi", "arith.muli", "arith.divsi", "arith.remsi",
                      "arith.and", "arith.or", "arith.xor"})
        add({name, 2, 2, 1, 1, 0, 0, Effect::pure, false, integer});
    return ok;
}
} // namespace mlvm
