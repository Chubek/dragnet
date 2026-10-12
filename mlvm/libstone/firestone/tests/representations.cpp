#include "Test.hpp"
using namespace firestone::ir;

int main() { return test_main([] {
    Typed3AC linear; auto p = linear.create_register(Type::integer(), true);
    auto r = linear.compute(Opcode::Add, Type::integer(), {Operand::ref(p), Operand::constant(2)});
    linear.emit({Opcode::Return, {}, {}, {Operand::ref(r)}, {}, {}, {}}); CHECK(linear.verify().empty());
    Typed3AC wrong; wrong.compute(Opcode::Add, Type::integer(), {Operand::real(1.0), Operand::constant(1)}); CHECK(has_code(wrong.verify(), "type"));
    Untyped3AC untyped; auto reg = untyped.create_register(); untyped.emit({Opcode::Copy, reg, {}, {Operand::named("external")}, {}, {}, {}}); CHECK(untyped.verify().empty());
    Triplets triples; auto a = triples.emit(Opcode::Constant, {Operand::constant(2)}); auto b = triples.emit(Opcode::Add, {Operand::ref(a), Operand::constant(3)});
    CHECK(b == 1); throws<std::invalid_argument>([&] { triples.emit(Opcode::Copy, {Operand::ref(99)}); });
    Quadruplets quads; auto q = quads.create_register(); quads.emit({Opcode::Constant, q, {}, {Operand::constant(4)}, {}, {}, {}}); CHECK(quads.verify().empty());
    throws<std::invalid_argument>([&] { quads.emit({Opcode::Select, q, {}, {Operand::constant(1), Operand::constant(2), Operand::constant(3)}, {}, {}, {}}); });
    DataflowGraph data; auto x = data.input("x", Type::integer()), zero = data.constant(0);
    auto sum = data.operation(Opcode::Add, Type::integer(), {x, zero}); CHECK(sum == 2); CHECK(data.verify().empty()); CHECK(data.schedule().size() == 3);
    throws<std::invalid_argument>([&] { data.operation(Opcode::Load, Type::integer(), {x}); });
    throws<std::invalid_argument>([&] { data.operation(Opcode::Add, Type::integer(), {x}); });
    ANFGraph anf; p = anf.create_register(Type::integer(), true); r = anf.bind(Opcode::Mul, Type::integer(), {Operand::ref(p), Operand::constant(2)});
    anf.set_result(Operand::ref(r)); CHECK(anf.verify().empty()); throws<std::logic_error>([&] { anf.bind(Opcode::Copy, Type::integer(), {Operand::ref(r)}); });

    CPSGraph cps; auto start = cps.continuation("start", {Type::integer()}), done = cps.continuation("done", {Type::integer()});
    p = cps.graph().node(start).parameters[0]; r = cps.bind(start, Opcode::Add, Type::integer(), {Operand::ref(p), Operand::constant(1)});
    cps.jump(start, done, {Operand::ref(r)}); cps.halt(done, Operand::ref(cps.graph().node(done).parameters[0])); CHECK(cps.verify().empty());
    CPSGraph bad_cps; start = bad_cps.continuation("start", {Type::integer()}); done = bad_cps.continuation("done");
    bad_cps.halt(start); bad_cps.halt(done, Operand::ref(bad_cps.graph().node(start).parameters[0])); CHECK(has_code(bad_cps.verify(), "scope"));

    ClickSeaOfNodesGraph sea;
    auto addr = sea.literal(Operand::named("ptr", Type::pointer()));
    auto val = sea.literal(Operand::constant(42));
    auto store = sea.operation(Opcode::Store, Type::void_type(), {addr, val}, sea.start());
    auto load = sea.operation(Opcode::Load, Type::integer(), {addr}, store); sea.end(load, load); CHECK(sea.verify().empty());
    auto region = sea.region({sea.start(), store}); auto phi = sea.phi(region, Type::integer(), {val, load}); CHECK(sea.verify().empty());
    sea.set_inputs(phi, {val, phi}, {region}); CHECK(sea.verify().empty());
    auto sea_predicate = sea.literal(Operand::constant(1, Type::boolean()));
    auto sea_branch = sea.branch(sea_predicate, load);
    auto arm0 = sea.control_projection(sea_branch, 0), arm1 = sea.control_projection(sea_branch, 1);
    sea.end(arm0, val); sea.end(arm1, phi); CHECK(sea.verify().empty());
    throws<std::invalid_argument>([&] { sea.control_projection(sea_branch, 2); });
    ClickSeaOfNodesGraph bad_sea; addr = bad_sea.literal(Operand::named("ptr", Type::pointer())); bad_sea.operation(Opcode::Load, Type::integer(), {addr});
    CHECK(has_code(bad_sea.verify(), "control"));

    RVSDG regional; auto input = regional.argument(regional.root(), Type::integer()), predicate = regional.argument(regional.root(), Type::boolean());
    auto gamma = regional.gamma(regional.root(), predicate, {input}, {Type::integer()});
    for (auto child : regional.node(gamma).subregions) regional.set_results(child, {regional.argument_port(child, 0)});
    auto theta = regional.theta(regional.root(), {regional.output(gamma)}); auto body = regional.node(theta).subregions[0];
    auto test = regional.constant(body, Operand::constant(0, Type::boolean()));
    regional.set_results(body, {regional.output(test), regional.argument_port(body, 0)});
    regional.set_results(regional.root(), {regional.output(theta)}); CHECK(regional.verify().empty());
    throws<std::invalid_argument>([&] { regional.simple(body, Opcode::Copy, {input}, {Type::integer()}); });
    regional.set_results(body, {regional.argument_port(body, 0)}); CHECK(has_code(regional.verify(), "region"));
    RVSDG function; auto lambda = function.lambda(0, {Type::integer()}, {Type::integer()}); auto fn_body = function.node(lambda).subregions[0];
    function.set_results(fn_body, {function.argument_port(fn_body, 0)}); CHECK(function.verify().empty());

    GatedSSAGraph gated; auto& cfg = gated.cfg(); auto e = cfg.create_block(), yes = cfg.create_block(), no = cfg.create_block(), join = cfg.create_block();
    auto cond = gated.parameter(Type::boolean()), v = gated.parameter(Type::integer());
    auto neg = gated.define(e, Opcode::Not, Type::boolean(), {Operand::ref(cond)}); cfg.branch(e, Operand::ref(cond), yes, no); cfg.jump(yes, join); cfg.jump(no, join);
    auto merge = gated.gated_phi(join, Type::integer(), {{yes, Operand::ref(v), Operand::ref(cond)}, {no, Operand::ref(v), Operand::ref(neg)}});
    cfg.return_value(join, Operand::ref(merge)); CHECK(gated.verify().empty());

    ControlFlowGraph memory_cfg; e = memory_cfg.create_block(); yes = memory_cfg.create_block(); no = memory_cfg.create_block(); join = memory_cfg.create_block();
    memory_cfg.branch(e, Operand::constant(1, Type::boolean()), yes, no);
    memory_cfg.append(yes, {Opcode::Store, {}, Type::void_type(), {Operand::named("p", Type::pointer()), Operand::constant(1)}, {}, {}, {}}); memory_cfg.jump(yes, join);
    memory_cfg.append(no, {Opcode::Store, {}, Type::void_type(), {Operand::named("p", Type::pointer()), Operand::constant(2)}, {}, {}, {}}); memory_cfg.jump(no, join);
    memory_cfg.append(join, {Opcode::Load, 0, Type::integer(), {Operand::named("p", Type::pointer())}, {}, {}, {}}); memory_cfg.return_value(join, Operand::ref(0));
    auto memory = MemorySSAGraph::from_cfg(memory_cfg); auto access = *memory.access(join, 0);
    auto reaching = memory.reaching_definitions(access); CHECK(reaching.size() == 2);
    CHECK(reaching.count(*memory.access(yes, 0))); CHECK(reaching.count(*memory.access(no, 0)));
    CHECK(!memory.access(e, 0));
}); }
