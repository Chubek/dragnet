#include "Test.hpp"
#include "VMTest.hpp"

int main() { return test_main([] {
    using namespace firestone;
    throws<std::invalid_argument>([] { (void)ir::Operand::ref(ir::invalid_id); });
    throws<std::invalid_argument>([] { (void)ir::Operand::named(""); });
    { ir::CallGraph g; auto a = g.function("a"); throws<std::invalid_argument>([&] { g.call(a, 99); }); }
    { ir::SimpleSSAGraph ssa; auto b = ssa.cfg().create_block("e");
      auto v = ssa.reserve_value(ir::Type::integer());
      throws<std::invalid_argument>([&] { ssa.define_reserved(b, 99, ir::Opcode::Add, {}); });
      throws<std::invalid_argument>([&] { ssa.phi_reserved(b, v, {}); });
      auto p = ssa.parameter(ir::Type::integer());
      throws<std::invalid_argument>([&] { ssa.define_reserved(b, p, ir::Opcode::Add, {}); }); }
    { ir::CPSGraph g; throws<std::invalid_argument>([&] { g.continuation(""); }); }
    { ir::ClickSeaOfNodesGraph g; auto br = g.branch(g.literal(ir::Operand::constant(1, ir::Type::boolean())), g.start());
      auto p0 = g.control_projection(br, 0); CHECK(g.graph().contains(p0)); }
    { ir::ProgramDependenceGraph pdg; (void)pdg; }
    { ir::RVSDG r; auto port = r.argument(0, ir::Type::integer());
      CHECK(r.type(port) == ir::Type::integer());
      throws<std::out_of_range>([&] { (void)r.type({99, ir::invalid_id, 0}); }); }
    { ir::ControlFlowGraph cfg; auto e = cfg.create_block("e"); cfg.return_value(e, ir::Operand::constant(0));
      auto regions = ir::irreducible_regions(cfg); CHECK(regions.empty());
      throws<std::logic_error>([] { ir::MemorySSAGraph m; (void)m.live_on_entry(); }); }
    { memory::Pool pool(7, 4); CHECK(pool.block_size() == 7 && pool.stride() >= 7);
      throws<std::invalid_argument>([] { memory::Pool bad(0, 1); }); }
    { memory::GC<int> heap; auto h = heap.allocate(1); heap.pin(h);
      heap.unpin(h); throws<std::logic_error>([&] { heap.unpin(h); }); }
    { memory::IntrusivePtr<memory::RefCounted> null; throws<std::logic_error>([&] { (void)*null; }); }
    { object::ObjectGraph g; auto a = g.create("A");
      throws<std::invalid_argument>([&] { g.erase_field(a, "missing"); }); }
    { object::ClassHierarchy h; auto b = h.create("B"); h.add_field(b, "x", type::Type::atomic(ir::Type::integer()));
      auto d = h.create("D", b); CHECK(h.fields(d).size() == 1); }
    { sema::FunctionSignatures sigs{{"f", {}}}; ir::Instruction bad; bad.opcode = ir::Opcode::Call;
      auto diagnostics = sema::check_call(bad, sigs, [](ir::ValueId) { return ir::Type{}; });
      CHECK(!diagnostics.empty()); }
    { ir::Instruction wide; wide.opcode = ir::Opcode::Add; wide.type = ir::Type::integer(65);
      wide.operands = {ir::Operand::constant(1, ir::Type::integer(65)), ir::Operand::constant(2, ir::Type::integer(65))};
      CHECK(sema::check_overflow(wide)[0].code == "integer.width"); }
    { type::LayoutPolicy policy; (void)type::layout(type::Type::record({}), policy); CHECK(type::layout(type::Type::record({}), policy).size == 1); }
    { auto from = type::Atomic{type::AtomicKind::Pointer, 0, "a"}, to = type::Atomic{type::AtomicKind::Pointer, 0, "b"};
      CHECK(type::classify_conversion(from, to) == type::Conversion::Invalid); }
    { vm::CallStack stack; throws<vm::VMError>([&] { (void)stack.top(); }); }
    { vm::BytecodeCache cache(2); cache.load(vm::encode(adder())); cache.clear();
      CHECK(cache.hits() == 0 && cache.misses() == 0 && cache.size() == 0); }
    { vm::Loader loader; vm_error("vm.module", [&] { loader.get("nope"); }); }
    { vm::MethodJIT jit(adder(), [](const vm::Module&, vm::Index) { return vm::CompiledEntry{}; }, 100);
      vm::Environment env;
      jit.run(0, {std::int64_t{1}, std::int64_t{2}}, env); jit.invalidate(0);
      CHECK(jit.profile(0).invocations == 1); }
    { bignum::BigInt v(1); throws<std::length_error>([&] { (void)(v << (std::size_t{1} << 27)); }); }
    { gfx::HLSL hlsl(gfx::ShaderStage::Vertex, "x"); CHECK(hlsl.profile() == "vs_6_0"); }
    { gfx::SPIRV module; module.append(17, {1}); CHECK(module.instructions().size() == 1); }
    { concurrency::GreenThread sched; auto id = sched.spawn([] { return concurrency::FiberStep::Complete; });
      CHECK(sched.cancel(id)); sched.run(); CHECK(!sched.pending()); }
    { concurrency::Thread t; CHECK(!t.joinable()); }
}); }
