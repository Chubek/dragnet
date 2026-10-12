#include "Test.hpp"
using namespace firestone::ir;

int main() { return test_main([] {
    DirectedGraph<std::string> graph;
    auto a = graph.add_node("a"), b = graph.add_node("b"), c = graph.add_node("c");
    CHECK(graph.add_edge(a, b)); CHECK(!graph.add_edge(a, b));
    graph.add_edge(a, b, EdgeKind::Data, 1); graph.add_edge(b, c);
    CHECK(graph.topological_order() == std::vector<NodeId>({a, b, c}));
    graph.add_edge(c, b);
    CHECK(graph.strongly_connected_components() == std::vector<std::vector<NodeId>>({{a}, {b, c}}));
    throws<std::logic_error>([&] { graph.topological_order(); });
    graph.remove_node(b); CHECK(graph.contains(c)); CHECK(graph.edges().empty());
    CHECK(graph.add_node("d") == 3); throws<std::out_of_range>([&] { graph.node(b); });
    CHECK(graph.to_dot([](const std::string& label) { return label + "\n\""; }).find("\\n\\\"") != std::string::npos);

    RawDAG<std::string> dag;
    a = dag.add_node("a"); b = dag.add_node("b"); dag.add_edge(a, b);
    throws<std::invalid_argument>([&] { dag.add_edge(b, a); }); CHECK(dag.verify().empty()); CHECK(dag.edges().size() == 1);

    SimpleSSAGraph ssa;
    auto& cfg = ssa.cfg();
    auto entry = cfg.create_block("entry"), yes = cfg.create_block("yes"), no = cfg.create_block("no"), join = cfg.create_block("join");
    auto x = ssa.parameter(Type::integer()), condition = ssa.parameter(Type::boolean());
    cfg.branch(entry, Operand::ref(condition), yes, no);
    auto y = ssa.define(yes, Opcode::Add, Type::integer(), {Operand::ref(x), Operand::constant(1)}); cfg.jump(yes, join);
    auto n = ssa.define(no, Opcode::Sub, Type::integer(), {Operand::ref(x), Operand::constant(1)}); cfg.jump(no, join);
    auto p = ssa.phi(join, Type::integer(), {{yes, Operand::ref(y)}, {no, Operand::ref(n)}});
    auto z = ssa.define(join, Opcode::Mul, Type::integer(), {Operand::ref(p), Operand::ref(x)}); cfg.return_value(join, Operand::ref(z));
    CHECK(ssa.verify().empty());
    CHECK(cfg.dominators().at(join) == std::set<NodeId>({entry, join}));
    CHECK(immediate_dominator(join, cfg.dominators()) == entry);
    CHECK(cfg.dominance_frontiers().at(yes) == std::set<BlockId>({join}));
    CHECK(cfg.postdominators().at(entry) == std::set<NodeId>({entry, join}));
    const auto controls = cfg.control_dependencies();
    CHECK(controls == std::vector<std::pair<BlockId, BlockId>>({{entry, yes}, {entry, no}}));
    auto live = compute_liveness(ssa);
    CHECK(live.live_out[yes].count(y)); CHECK(!live.live_out[yes].count(n));
    CHECK(live.live_in[join].count(x)); CHECK(!live.live_in[join].count(y));
    auto interference = InterferenceGraph::from_ssa(ssa);
    CHECK(interference.interferes(x, y)); CHECK(!interference.interferes(y, n));
    CHECK(!interference.color(4).empty());
    auto ddg = DataDependenceGraph::from_ssa(ssa);
    CHECK(ddg.size() == 10);
    CHECK(ddg.backward_slice(ddg.nodes().back()).size() >= 5);
    auto pdg = ProgramDependenceGraph::from_ssa(ssa);
    CHECK(pdg.edges().size() > ddg.edges().size());
    auto vdg = ValueDependenceGraph::from_ssa(ssa); CHECK(vdg.size() == ssa.value_count());
    throws<std::logic_error>([&] { cfg.return_value(entry); });

    SimpleSSAGraph loop;
    auto e = loop.cfg().create_block(), h = loop.cfg().create_block(), body = loop.cfg().create_block(), exit = loop.cfg().create_block();
    auto limit = loop.parameter(Type::integer());
    auto init = loop.define(e, Opcode::Constant, Type::integer(), {Operand::constant(0)}); loop.cfg().jump(e, h);
    auto next = loop.reserve_value(Type::integer());
    auto counter = loop.phi(h, Type::integer(), {{e, Operand::ref(init)}, {body, Operand::ref(next)}});
    auto cond = loop.define(h, Opcode::Lt, Type::boolean(), {Operand::ref(counter), Operand::ref(limit)});
    loop.cfg().branch(h, Operand::ref(cond), body, exit);
    loop.define_reserved(body, next, Opcode::Add, {Operand::ref(counter), Operand::constant(1)}); loop.cfg().jump(body, h);
    loop.cfg().return_value(exit, Operand::ref(counter)); CHECK(loop.verify().empty());
    auto loop_values = ValueDependenceGraph::from_ssa(loop);
    bool cyclic = false; for (const auto& component : loop_values.strongly_connected_components()) cyclic = cyclic || component.size() > 1;
    CHECK(cyclic); CHECK(compute_liveness(loop).live_out[body].count(next));

    SimpleSSAGraph bad;
    e = bad.cfg().create_block();
    auto undefined = bad.reserve_value(Type::integer());
    bad.cfg().return_value(e, Operand::ref(undefined)); CHECK(has_code(bad.verify(), "undefined"));
    throws<std::invalid_argument>([&] { DataDependenceGraph::from_ssa(bad); });

    SimpleSSAGraph nondominating;
    e = nondominating.cfg().create_block(); yes = nondominating.cfg().create_block(); no = nondominating.cfg().create_block(); join = nondominating.cfg().create_block();
    condition = nondominating.parameter(Type::boolean()); nondominating.cfg().branch(e, Operand::ref(condition), yes, no);
    y = nondominating.define(yes, Opcode::Constant, Type::integer(), {Operand::constant(1)});
    nondominating.cfg().jump(yes, join); nondominating.cfg().jump(no, join); nondominating.cfg().return_value(join, Operand::ref(y));
    CHECK(has_code(nondominating.verify(), "dominance"));

    InterferenceGraph triangle; triangle.interfere(0, 1); triangle.interfere(1, 2); triangle.interfere(2, 0);
    throws<std::runtime_error>([&] { triangle.color(2); });
    throws<std::invalid_argument>([&] { triangle.color(3, {{0, 0}, {1, 0}}); });
    CHECK(triangle.color(3).size() == 3);

    CallGraph calls; auto f = calls.function("f"), g = calls.function("g"), external = calls.function("puts", true);
    calls.call(f, g); calls.call(g, f); calls.call(f, external);
    CHECK(calls.recursive_functions() == std::set<NodeId>({f, g})); CHECK(calls.find("puts") == external);
    throws<std::invalid_argument>([&] { calls.function("puts"); });
}); }
