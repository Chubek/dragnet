#include "Test.hpp"

using namespace firestone::ir;
using namespace firestone::cps;

int main() { return test_main([] {
    CPSBuilder builder;
    auto entry = builder.create("entry", {Type::boolean()});
    auto join = builder.create("join", {Type::integer()});
    builder.branch(entry, builder.parameter(entry, 0), join, {Operand::constant(1)}, join, {Operand::constant(2)});
    builder.graph().halt(join, builder.parameter(join, 0));
    auto graph = builder.finish();
    auto lowered = lower_to_ssa(graph);
    CHECK(lowered.ssa.verify().empty());
    const auto& phi = lowered.ssa.cfg().block(lowered.blocks.at(join)).instructions.front();
    CHECK(phi.opcode == Opcode::Phi && phi.incoming_blocks.size() == 2 && phi.incoming_blocks[0] != phi.incoming_blocks[1]);
    CHECK(phi.operands[0].integer == 1 && phi.operands[1].integer == 2);
    auto live = analyze_liveness(graph);
    CHECK(live.required_parameters.at(join).count(graph.continuation(join).parameters[0]));

    CPSBuilder recursive;
    auto start = recursive.create("start", {Type::integer()});
    auto loop = recursive.create("loop", {Type::integer(), Type::boolean()});
    auto exit = recursive.create("exit", {Type::integer()});
    recursive.jump(start, loop, {recursive.parameter(start, 0), Operand::constant(1, Type::boolean())});
    auto next = recursive.graph().bind(loop, Opcode::Add, Type::integer(), {recursive.parameter(loop, 0), Operand::constant(1)});
    recursive.graph().bind(loop, Opcode::Mul, Type::integer(), {Operand::ref(next), Operand::constant(5)});
    recursive.graph().effect(loop, Opcode::Call, {}, "observe");
    recursive.branch(loop, recursive.parameter(loop, 1), loop, {Operand::ref(next), Operand::constant(0, Type::boolean())}, exit, {Operand::ref(next)});
    recursive.graph().halt(exit, recursive.parameter(exit, 0));
    auto recursive_graph = recursive.finish();
    live = analyze_liveness(recursive_graph);
    CHECK(live.dead_bindings.at(loop) == std::set<std::size_t>{1});
    CHECK(live.required_parameters.at(loop).size() == 2);
    auto recursive_ssa = lower_to_ssa(recursive_graph);
    CHECK(recursive_ssa.ssa.verify().empty() && natural_loops(recursive_ssa.ssa.cfg()).size() == 1);
    CHECK(firestone::sema::check_definite_assignment(recursive_ssa.ssa.cfg(), {recursive_ssa.values.at(recursive_graph.continuation(start).parameters[0])}).diagnostics.empty());
    throws<std::invalid_argument>([&] { recursive.create("loop"); });
    CHECK(signature(recursive_graph, loop).parameters == std::vector<Type>({Type::integer(), Type::boolean()}));

    CPSGraph bad;
    auto c = bad.continuation("bad", {Type::integer(0 + 1)});
    bad.halt(c, Operand::constant(2, Type::boolean()));
    CHECK(has_code(bad.verify(), "literal"));
    CPSGraph empty; CHECK(has_code(empty.verify(), "entry"));
    CPSBuilder incomplete; incomplete.create("unfinished");
    throws<std::invalid_argument>([&] { incomplete.finish(); });
    CPSBuilder scoped;
    auto from = scoped.create("from"), to = scoped.create("to", {Type::boolean()});
    throws<std::invalid_argument>([&] { scoped.branch(from, scoped.parameter(to, 0), to, {Operand::constant(1, Type::boolean())},
                                                   to, {Operand::constant(0, Type::boolean())}); });
    CHECK(!scoped.graph().continuation(from).tail);
}); }
