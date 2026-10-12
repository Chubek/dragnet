#include "VMTest.hpp"

namespace ir = firestone::ir;

static ir::SimpleSSAGraph swapping_loop() {
    ir::SimpleSSAGraph ssa;
    auto entry = ssa.cfg().create_block("entry"), header = ssa.cfg().create_block("header");
    auto body = ssa.cfg().create_block("body"), exit = ssa.cfg().create_block("exit");
    auto type = ir::Type::integer(); auto n = ssa.parameter(type);
    auto a = ssa.reserve_value(type), b = ssa.reserve_value(type), count = ssa.reserve_value(type);
    auto next = ssa.reserve_value(type);
    ssa.cfg().jump(entry, header);
    ssa.phi_reserved(header, a, {{entry, ir::Operand::constant(1)}, {body, ir::Operand::ref(b)}});
    ssa.phi_reserved(header, b, {{entry, ir::Operand::constant(2)}, {body, ir::Operand::ref(a)}});
    ssa.phi_reserved(header, count, {{entry, ir::Operand::constant(0)}, {body, ir::Operand::ref(next)}});
    auto condition = ssa.define(header, ir::Opcode::Lt, ir::Type::boolean(), {ir::Operand::ref(count), ir::Operand::ref(n)});
    ssa.cfg().branch(header, ir::Operand::ref(condition), body, exit);
    ssa.define_reserved(body, next, ir::Opcode::Add, {ir::Operand::ref(count), ir::Operand::constant(1)});
    ssa.cfg().jump(body, header);
    auto tens = ssa.define(exit, ir::Opcode::Mul, type, {ir::Operand::ref(a), ir::Operand::constant(10)});
    auto result = ssa.define(exit, ir::Opcode::Add, type, {ir::Operand::ref(tens), ir::Operand::ref(b)});
    ssa.cfg().return_value(exit, ir::Operand::ref(result));
    return ssa;
}
int main() { return test_main([] {
    auto loop = swapping_loop(); CHECK(loop.verify().empty()); vm::Environment env;
    for (auto model : {vm::VMModel::Register, vm::VMModel::Stack}) {
        vm::Module module; auto f = vm::compile(module, loop, "swap", model); CHECK(module.verify().empty());
        CHECK(integer_result(vm::execute(module, f, {std::int64_t{0}}, env)) == 12);
        CHECK(integer_result(vm::execute(module, f, {std::int64_t{1}}, env)) == 21);
        CHECK(integer_result(vm::execute(module, f, {std::int64_t{2}}, env)) == 12);
        auto paused = vm::execute(module, f, {std::int64_t{9}}, env, {3, 32, 32});
        for (int n = 0; n < 1000 && paused.stop == vm::ExecutionStop::Budget; ++n) paused = vm::resume(module, env, paused.state, {3, 32, 32});
        CHECK(integer_result(paused) == 21);
        auto loaded = vm::decode(vm::encode(module)); CHECK(integer_result(vm::execute(loaded, f, {std::int64_t{3}}, env)) == 21);
    }

    ir::SimpleSSAGraph diamond;
    auto entry = diamond.cfg().create_block("entry"), left = diamond.cfg().create_block("left");
    auto right = diamond.cfg().create_block("right"), merge = diamond.cfg().create_block("merge");
    auto condition = diamond.parameter(ir::Type::boolean()); diamond.cfg().branch(entry, ir::Operand::ref(condition), left, right);
    diamond.cfg().jump(left, merge); diamond.cfg().jump(right, merge);
    auto value = diamond.phi(merge, ir::Type::integer(), {{left, ir::Operand::constant(42)}, {right, ir::Operand::constant(-5)}});
    diamond.cfg().return_value(merge, ir::Operand::ref(value));
    vm::Module module; auto f = vm::Compiler{}.compile(module, diamond, "diamond");
    CHECK(integer_result(vm::execute(module, f, {true}, env)) == 42);
    CHECK(integer_result(vm::execute(module, f, {false}, env)) == -5);

    // Self-recursion resolves before publishing the new function.
    ir::SimpleSSAGraph factorial;
    auto type64 = ir::Type::integer(64); entry = factorial.cfg().create_block("entry");
    auto base = factorial.cfg().create_block("base"), recursive = factorial.cfg().create_block("recursive");
    auto n = factorial.parameter(type64);
    auto small = factorial.define(entry, ir::Opcode::Le, ir::Type::boolean(), {ir::Operand::ref(n), ir::Operand::constant(1, type64)});
    factorial.cfg().branch(entry, ir::Operand::ref(small), base, recursive);
    factorial.cfg().return_value(base, ir::Operand::constant(1, type64));
    auto previous = factorial.define(recursive, ir::Opcode::Sub, type64, {ir::Operand::ref(n), ir::Operand::constant(1, type64)});
    auto subresult = factorial.define(recursive, ir::Opcode::Call, type64, {ir::Operand::ref(previous)}, "factorial");
    auto product = factorial.define(recursive, ir::Opcode::Mul, type64, {ir::Operand::ref(n), ir::Operand::ref(subresult)});
    factorial.cfg().return_value(recursive, ir::Operand::ref(product));
    for (auto model : {vm::VMModel::Register, vm::VMModel::Stack}) {
        vm::Module recursive_module; auto id = vm::compile(recursive_module, factorial, "factorial", model);
        CHECK(integer_result(vm::execute(recursive_module, id, {std::int64_t{5}}, env)) == 120);
        vm_error("vm.call.limit", [&] { vm::execute(recursive_module, id, {std::int64_t{5}}, env, {1000, 3, 32}); });
        vm_error("vm.overflow", [&] { vm::execute(recursive_module, id, {std::int64_t{21}}, env); });
    }

    // Checked 32-bit semantics survive lowering even though runtime storage is 64-bit.
    ir::SimpleSSAGraph checked; entry = checked.cfg().create_block("entry");
    auto input = checked.parameter(ir::Type::integer());
    value = checked.define(entry, ir::Opcode::Add, ir::Type::integer(), {ir::Operand::ref(input), ir::Operand::constant(1)});
    checked.cfg().return_value(entry, ir::Operand::ref(value)); f = vm::compile(module, checked, "checked");
    vm_error("vm.overflow", [&] { vm::execute(module, f, {std::int64_t{INT32_MAX}}, env); });

    // Native calls, symbols, and memory effects use the same environment in both models.
    module.natives["twice"] = {{vm::ValueKind::Integer}, vm::ValueKind::Integer};
    env.bind("twice", module.natives.at("twice"), [](const std::vector<vm::Value>& args) { return vm::require<std::int64_t>(args[0]) * 2; });
    ir::SimpleSSAGraph effect; entry = effect.cfg().create_block("entry");
    auto pointer = effect.parameter(ir::Type::pointer());
    auto old = effect.define(entry, ir::Opcode::Load, ir::Type::integer(), {ir::Operand::ref(pointer)});
    auto doubled = effect.define(entry, ir::Opcode::Call, ir::Type::integer(), {ir::Operand::ref(old)}, "twice");
    effect.cfg().append(entry, {ir::Opcode::Store, {}, ir::Type::void_type(), {ir::Operand::ref(pointer), ir::Operand::ref(doubled)}, {}, {}, {}});
    effect.cfg().return_value(entry, ir::Operand::ref(doubled));
    for (auto model : {vm::VMModel::Register, vm::VMModel::Stack}) {
        auto address = env.allocate(std::int64_t{21});
        auto id = vm::compile(module, effect, model == vm::VMModel::Stack ? "stack-effect" : "register-effect", model);
        CHECK(integer_result(vm::execute(module, id, {address}, env)) == 42); CHECK(vm::require<std::int64_t>(env.load(address)) == 42);
    }

    // Failure does not leave declarations or constants in the caller's module.
    auto before = vm::encode(module);
    ir::SimpleSSAGraph unsupported; entry = unsupported.cfg().create_block("entry");
    unsupported.cfg().return_value(entry, ir::Operand::real(1.0, ir::Type::floating(32)));
    throws<std::invalid_argument>([&] { vm::compile(module, unsupported, "unsupported"); });
    CHECK(vm::encode(module) == before);
    throws<std::invalid_argument>([&] { vm::compile(module, loop, "diamond"); }); CHECK(vm::encode(module) == before);
}); }
