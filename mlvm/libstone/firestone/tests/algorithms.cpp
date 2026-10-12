#include "Test.hpp"

using namespace firestone::ir;

int main() { return test_main([] {
    ControlFlowGraph cfg;
    auto entry = cfg.create_block("entry"), header = cfg.create_block("header"), body = cfg.create_block("body"), exit = cfg.create_block("exit");
    cfg.jump(entry, header); cfg.branch(header, Operand::constant(1, Type::boolean()), body, exit); cfg.jump(body, header); cfg.return_value(exit);
    auto unreachable = cfg.create_block("unreachable"); cfg.return_value(unreachable);
    DominatorTree dom(cfg);
    CHECK(dom.parent(header) == entry && dom.parent(body) == header && dom.depth(body) == 2);
    CHECK(dom.nearest_common_dominator(body, exit) == header);
    CHECK(!dom.nearest_common_dominator(body, unreachable));
    auto loops = natural_loops(cfg);
    CHECK(loops.size() == 1 && loops[0].header == header && loops[0].blocks == std::set<BlockId>({header, body}));
    CHECK(loops[0].exits == std::vector<std::pair<BlockId, BlockId>>{{header, exit}});
    CHECK(cfg.graph().shortest_path(entry, exit) == std::vector<NodeId>({entry, header, exit}));
    CHECK(cfg.graph().shortest_path(entry, unreachable).empty());
    CHECK(cfg.reverse_postorder().size() == 4 && cfg.unreachable_blocks() == std::set<BlockId>{unreachable});
    CHECK(irreducible_regions(cfg).empty());

    using State = std::set<BlockId>;
    auto meet = [](const std::vector<State>& incoming) { State result; for (const auto& state : incoming) result.insert(state.begin(), state.end()); return result; };
    auto transfer = [](BlockId block, State state) { state.insert(block); return state; };
    auto flow = solve_dataflow(cfg, State{}, meet, transfer, {{entry, State{}}});
    CHECK(flow.converged && flow.out.at(exit) == State({entry, header, body, exit}) && flow.in.count(unreachable) == 0);
    auto backward = solve_dataflow(cfg, State{}, meet, transfer, {{exit, State{}}}, FlowDirection::Backward);
    CHECK(backward.in.at(entry).count(exit));
    CHECK(!solve_dataflow(cfg, State{}, meet, transfer, {}, FlowDirection::Forward, 0).converged);

    ControlFlowGraph irreducible;
    auto root = irreducible.create_block(), a = irreducible.create_block(), b = irreducible.create_block();
    irreducible.branch(root, Operand::constant(1, Type::boolean()), a, b); irreducible.jump(a, b); irreducible.jump(b, a);
    CHECK(irreducible_regions(irreducible) == std::vector<std::set<BlockId>>{State{a, b}});
    CHECK(natural_loops(irreducible).empty());
    auto increasing = solve_dataflow(irreducible, 0, [](const std::vector<int>& values) { int result = 0; for (auto value : values) result = std::max(result, value); return result; },
        [](BlockId, int value) { return value + 1; }, {}, FlowDirection::Forward, 20);
    CHECK(!increasing.converged && increasing.evaluations == 20);

    ControlFlowGraph nested;
    auto start = nested.create_block(), outer = nested.create_block(), inner = nested.create_block(), latch = nested.create_block(), end = nested.create_block();
    nested.jump(start, outer); nested.branch(outer, Operand::constant(1, Type::boolean()), inner, end);
    nested.branch(inner, Operand::constant(1, Type::boolean()), inner, latch); nested.jump(latch, outer); nested.return_value(end);
    auto nesting = natural_loops(nested);
    CHECK(nesting.size() == 2 && nesting[1].header == inner && nesting[1].parent_header == outer);

    SimpleSSAGraph ssa;
    auto block = ssa.cfg().create_block(); auto input = ssa.parameter(Type::integer()); ssa.cfg().return_value(block, Operand::ref(input));
    PassManager passes;
    passes.add("valid", [](SimpleSSAGraph& program) { program.parameter(Type::integer()); return true; });
    passes.add("broken", [](SimpleSSAGraph& program) { program.reserve_value(Type::integer()); return true; });
    auto reports = passes.run(ssa);
    CHECK(reports.size() == 2 && reports[0].committed && !reports[1].committed && has_code(reports[1].diagnostics, "undefined"));
    CHECK(ssa.value_count() == 2 && ssa.verify().empty());
    PassManager throwing;
    throwing.add("throw", [](SimpleSSAGraph& program) -> bool { program.parameter(Type::integer()); throw std::runtime_error("failure"); });
    throws<std::runtime_error>([&] { throwing.run(ssa); }); CHECK(ssa.value_count() == 2);
}); }
