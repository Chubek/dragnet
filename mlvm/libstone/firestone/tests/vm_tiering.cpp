#include "VMTest.hpp"

int main() { return test_main([] {
    auto module = adder(); vm::Environment env; vm::Index compilations = 0;
    vm::EntryCompiler backend = [&](const vm::Module& input, vm::Index function) -> vm::CompiledEntry {
        ++compilations; auto owned = std::make_shared<const vm::Module>(input);
        return [owned, function](const std::vector<vm::Value>& args, vm::Environment& environment, const vm::ExecutionLimits& limits) {
            return vm::execute(*owned, function, args, environment, limits);
        };
    };
    vm::MethodJIT jit(module, backend, 2);
    CHECK(integer_result(jit.run(0, {std::int64_t{20}, std::int64_t{22}}, env)) == 42); CHECK(compilations == 0);
    CHECK(integer_result(jit.run(0, {std::int64_t{20}, std::int64_t{22}}, env)) == 42); CHECK(compilations == 1);
    CHECK(jit.profile(0).compiled_invocations == 1);
    auto paused = jit.run(0, {std::int64_t{20}, std::int64_t{22}}, env, {0, 32, 32}); CHECK(paused.stop == vm::ExecutionStop::Budget);
    CHECK(integer_result(vm::resume(jit.module(), env, paused.state)) == 42);
    jit.invalidate(0); jit.run(0, {std::int64_t{0}, std::int64_t{1}}, env); CHECK(compilations == 2);
    vm::MethodJIT declining(module, [](const vm::Module&, vm::Index) { return vm::CompiledEntry{}; }, 1);
    CHECK(integer_result(declining.run(0, {std::int64_t{1}, std::int64_t{2}}, env)) == 3);
    vm::AOT aot(module); CHECK(aot.compiled_count() == 0); aot.compile(backend); CHECK(aot.compiled_count() == 1);
    CHECK(integer_result(aot.run(0, {std::int64_t{20}, std::int64_t{22}}, env)) == 42); CHECK(aot.bytecode() == vm::encode(module));
    throws<std::runtime_error>([&] { aot.compile([](const vm::Module&, vm::Index) -> vm::CompiledEntry { throw std::runtime_error("backend failure"); }); });
    CHECK(aot.compiled_count() == 1);
    vm::CompiledEntry invalid = [](const std::vector<vm::Value>&, vm::Environment&, const vm::ExecutionLimits&) {
        vm::ExecutionState state; state.result = true; return vm::ExecutionResult{vm::ExecutionStop::Returned, state};
    };
    vm_error("vm.type", [&] { vm::invoke_compiled(module, 0, invalid, {std::int64_t{1}, std::int64_t{2}}, env, {}); });

    module.function(0).code.at(0).opcode = vm::Opcode::Sub;
    auto state = vm::execute(module, 0, {std::int64_t{19}, std::int64_t{23}}, env, {0, 32, 32}).state;
    vm::OSRPlan plan; plan.target = {0, 0};
    plan.transfers = {{{vm::SlotKind::Register, 0}, {vm::SlotKind::Register, 1}}, {{vm::SlotKind::Register, 1}, {vm::SlotKind::Register, 0}}};
    auto transferred = vm::transfer_osr(module, state, plan); CHECK(integer_result(vm::resume(module, env, transferred)) == 4);
    CHECK(integer_result(vm::resume(module, env, state)) == -4);
    auto restored = vm::deoptimize(module, transferred, plan); CHECK(integer_result(vm::resume(module, env, restored)) == -4);
    auto duplicate = plan; duplicate.transfers.push_back(plan.transfers[0]); vm_error("vm.osr", [&] { vm::transfer_osr(module, state, duplicate); });
    auto malformed = state; malformed.frames.top().pc = 99; vm_error("vm.state", [&] { vm::resume(module, env, malformed); });

    vm::TraceJIT traces(module); auto capture = traces.record(0, {std::int64_t{19}, std::int64_t{23}}, env);
    CHECK(integer_result(capture.execution) == -4); CHECK(capture.trace.path.size() == 2 && traces.heat({0, 0}) == 1);
    capture.trace.guards.push_back({{vm::SlotKind::Register, 0}, vm::ValueKind::Integer, vm::Value{std::int64_t{19}}});
    vm::Index entered = 0;
    auto trace = traces.compile(capture.trace, [&](const vm::Module& input, const vm::Trace&) -> vm::TraceBody {
        auto owned = std::make_shared<const vm::Module>(input);
        return [owned, &entered](vm::Environment& environment, const vm::ExecutionState& snapshot, const vm::ExecutionLimits& limits) {
            ++entered; auto result = vm::resume(*owned, environment, snapshot, limits);
            return vm::DeoptResult{result.state, result.stop == vm::ExecutionStop::Returned, "backend-exit"};
        };
    });
    auto accepted = traces.enter(trace, env, state); CHECK(accepted.optimized && entered == 1);
    CHECK(vm::require<std::int64_t>(*accepted.state.result) == -4);
    auto rejected = traces.enter(trace, env, transferred); CHECK(!rejected.optimized && rejected.reason == "guard" && entered == 1);
    CHECK(integer_result(vm::resume(module, env, rejected.state)) == 4);
    CHECK(traces.record(0, {std::int64_t{1}, std::int64_t{2}}, env, 1).trace.truncated);
}); }
