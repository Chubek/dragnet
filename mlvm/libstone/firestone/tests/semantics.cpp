#include "Test.hpp"
#include <array>

using namespace firestone::ir;
using namespace firestone::sema;

int main() { return test_main([] {
    const auto min = std::numeric_limits<std::int64_t>::min(), max = std::numeric_limits<std::int64_t>::max();
    CHECK(checked_integer(Opcode::Add, max, 1).error == ArithmeticError::Overflow);
    CHECK(checked_integer(Opcode::Sub, min, 1).error == ArithmeticError::Overflow);
    CHECK(checked_integer(Opcode::Mul, min, -1).error == ArithmeticError::Overflow);
    CHECK(checked_integer(Opcode::Div, min, -1).error == ArithmeticError::Overflow);
    CHECK(checked_integer(Opcode::Mod, min, -1).value == 0);
    CHECK(checked_integer(Opcode::Neg, min).error == ArithmeticError::Overflow);
    CHECK(checked_integer(Opcode::Shr, min, 63).value == -1);
    CHECK(checked_integer(Opcode::Shl, 1, 63).error == ArithmeticError::Overflow);
    CHECK(checked_integer(Opcode::Shl, 1, 64).error == ArithmeticError::InvalidShift);
    CHECK(checked_integer(Opcode::Div, 4, 0).error == ArithmeticError::DivisionByZero);
    for (int a = -128; a < 128; ++a) for (int b = -128; b < 128; ++b) {
        const std::array<std::pair<Opcode, int>, 3> cases{{{Opcode::Add, a + b}, {Opcode::Sub, a - b}, {Opcode::Mul, a * b}}};
        for (const auto& item : cases) {
            auto actual = checked_integer(item.first, a, b, 8);
            bool fits = item.second >= -128 && item.second <= 127;
            CHECK(static_cast<bool>(actual) == fits);
            if (fits) CHECK(actual.value == item.second);
        }
    }
    Instruction overflow{Opcode::Add, ValueId{0}, Type::integer(8), {Operand::constant(127, Type::integer(8)), Operand::constant(1, Type::integer(8))}, {}, {}, {}};
    CHECK(has_code(check_overflow(overflow), "integer.overflow"));
    Instruction boolean_constant{Opcode::Constant, ValueId{0}, Type::boolean(), {Operand::constant(1, Type::boolean())}, {}, {}, {}};
    CHECK(check_overflow(boolean_constant).empty());

    SymbolTable symbols;
    symbols.declare(0, {"x", Type::integer(), false, 0});
    auto child = symbols.child(0); symbols.declare(child, {"x", Type::floating(), true, 1});
    CHECK(symbols.lookup(child, "x")->type == Type::floating() && symbols.visible(child).size() == 1);
    throws<std::invalid_argument>([&] { symbols.declare(child, {"x", Type::integer(), false, 0}); });
    auto use = Instruction{Opcode::Copy, ValueId{0}, Type::integer(), {Operand::named("x", Type::integer())}, {}, {}, {}};
    CHECK(check_symbols(use, symbols).empty() && has_code(check_symbols(use, symbols, child), "symbol.type"));
    use.operands[0].symbol = "missing"; CHECK(has_code(check_symbols(use, symbols), "symbol.undefined"));

    ControlFlowGraph cfg;
    auto entry = cfg.create_block("entry"), yes = cfg.create_block("yes"), no = cfg.create_block("no"), join = cfg.create_block("join");
    cfg.branch(entry, Operand::constant(1, Type::boolean()), yes, no);
    cfg.append(yes, {Opcode::Constant, ValueId{0}, Type::integer(), {Operand::constant(1)}, {}, {}, {}}); cfg.jump(yes, join);
    cfg.jump(no, join); cfg.return_value(join, Operand::ref(0));
    CHECK(has_code(check_definite_assignment(cfg).diagnostics, "assignment.undefined"));
    CHECK(check_definite_assignment(cfg, {0}).diagnostics.empty());
    CHECK(check_returns(cfg, Type::integer(), [](ValueId) { return Type::integer(); }).empty());
    CHECK(has_code(check_returns(cfg, Type::void_type(), [](ValueId) { return Type::integer(); }), "return.arity"));

    SimpleSSAGraph edge_phi;
    auto phi_entry = edge_phi.cfg().create_block(), dead = edge_phi.cfg().create_block(), phi_join = edge_phi.cfg().create_block();
    auto parameter = edge_phi.parameter(Type::integer());
    edge_phi.cfg().jump(phi_entry, phi_join); edge_phi.cfg().jump(dead, phi_join);
    auto selected = edge_phi.phi(phi_join, Type::integer(), {{phi_entry, Operand::ref(parameter)}, {dead, Operand::ref(parameter)}});
    edge_phi.cfg().return_value(phi_join, Operand::ref(selected));
    CHECK(edge_phi.verify().empty());
    CHECK(check_definite_assignment(edge_phi.cfg(), {parameter}).diagnostics.empty());

    SimpleSSAGraph ssa;
    auto block = ssa.cfg().create_block("calls");
    auto value = ssa.define(block, Opcode::Call, Type::integer(), {Operand::constant(7)}, "pure_call");
    ssa.cfg().return_value(block, Operand::ref(value));
    FunctionSignatures signatures{{"pure_call", {Type::integer(), {Type::integer()}, false}}};
    CHECK(check_types(ssa, signatures).empty());
    signatures["pure_call"].parameters = {Type::floating()}; CHECK(has_code(check_types(ssa, signatures), "call.type"));
    auto policy = EffectPolicy::pure(); CHECK(has_code(check_effects(ssa.cfg(), policy), "effect.call"));
    policy.calls["pure_call"] = {}; CHECK(check_effects(ssa.cfg(), policy).empty());
    auto unreachable = ssa.cfg().create_block("unreachable"); ssa.cfg().return_value(unreachable);
    CHECK(has_code(check_blocks(ssa.cfg(), {true, true}), "block.unreachable"));
}); }
