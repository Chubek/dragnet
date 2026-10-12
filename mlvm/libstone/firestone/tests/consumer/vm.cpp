#include <libvm/Compiler.hpp>
#include <libvm/BytecodeBinary.hpp>
#include <libvm/RegisterVM.hpp>

int main() {
    firestone::ir::SimpleSSAGraph ssa;
    auto entry = ssa.cfg().create_block("entry");
    ssa.cfg().return_value(entry, firestone::ir::Operand::constant(42));
    firestone::vm::Module module;
    auto function = firestone::vm::compile(module, ssa, "answer");
    auto loaded = firestone::vm::decode(firestone::vm::encode(module));
    firestone::vm::Environment environment;
    auto result = firestone::vm::RegisterVM{}.run(loaded, function, {}, environment);
    return result.stop == firestone::vm::ExecutionStop::Returned &&
        firestone::vm::require<std::int64_t>(*result.state.result) == 42 ? 0 : 1;
}
