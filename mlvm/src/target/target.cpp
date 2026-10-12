#include <mlvm/object.hpp>

namespace mlvm {
bool TargetRegistry::add(Target target) {
    if (target.triple.empty() || !target.pointer_bits || !target.machine || target.object_format.empty()) return false;
    auto key = target.triple; return targets_.emplace(std::move(key), std::move(target)).second;
}
const Target* TargetRegistry::find(std::string_view triple) const {
    const auto it = targets_.find(triple); return it == targets_.end() ? nullptr : &it->second;
}
TargetRegistry TargetRegistry::builtins() {
    TargetRegistry registry;
    registry.add({"x86_64-unknown-linux-gnu", 62, 64, Endianness::little, "elf64"});
    return registry;
}
} // namespace mlvm
