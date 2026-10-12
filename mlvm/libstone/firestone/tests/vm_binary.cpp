#include "VMTest.hpp"
#include <cmath>

int main() { return test_main([] {
    auto module = adder();
    module.constants.add(std::monostate{}); module.constants.add(true); module.constants.add(INT64_MIN);
    module.constants.add(INT64_MAX); module.constants.add(std::string("a\0b\"c", 6));
    module.constants.add(0.0); module.constants.add(-0.0); module.constants.add(std::numeric_limits<double>::quiet_NaN());
    auto bytes = vm::encode(module); auto decoded = vm::decode(bytes); CHECK(vm::encode(decoded) == bytes);
    CHECK(vm::disassemble(module).find("%2 = Add %0 %1 bits=64") != std::string::npos);
    CHECK(std::signbit(vm::require<double>(decoded.constants.at(6))));
    vm::Environment env; CHECK(integer_result(vm::execute(decoded, 0, {std::int64_t{20}, std::int64_t{22}}, env)) == 42);
    for (vm::Index n = 0; n < bytes.size(); ++n) {
        vm::BytecodeBytes truncated(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(n));
        throws<std::invalid_argument>([&] { vm::decode(truncated); });
    }
    auto bad = bytes; bad[0] = 0; throws<std::invalid_argument>([&] { vm::decode(bad); });
    bad = bytes; bad[4] = 2; throws<std::invalid_argument>([&] { vm::decode(bad); });
    bad = bytes; bad.push_back(0); throws<std::invalid_argument>([&] { vm::decode(bad); });
    bad = bytes; for (vm::Index n = 12; n < 20; ++n) bad[n] = 255;
    throws<std::invalid_argument>([&] { vm::decode(bad); });
    throws<std::invalid_argument>([&] { vm::decode(bytes, {bytes.size() - 1, 1000000, 1024}); });
    throws<std::invalid_argument>([&] { vm::decode(bytes, {bytes.size(), 2, 1024}); });
    throws<std::invalid_argument>([&] { vm::decode(bytes, {bytes.size(), 1000000, 1}); });
    throws<std::invalid_argument>([&] { module.constants.add(vm::Address{0}); });
    auto invalid = module; invalid.function(0).name.clear();
    CHECK(has_code(invalid.verify(), "vm.name")); throws<std::invalid_argument>([&] { vm::encode(invalid); });

    vm::BytecodeCache cache(1); auto first = cache.load(bytes); CHECK(cache.load(bytes) == first);
    CHECK(cache.hits() == 1 && cache.misses() == 1);
    throws<std::invalid_argument>([&] { cache.load(bytes, {1, 1000000, 1024}); });
    CHECK(cache.size() == 1);
    auto other = vm::encode(adder(vm::VMModel::Stack)); auto second = cache.load(other); CHECK(cache.size() == 1);
    CHECK(cache.load(bytes) != first); CHECK(first->size() == 1 && second->size() == 1);
    vm::Loader loader; auto loaded = loader.load("math", bytes);
    CHECK(loaded == loader.get("math")); loader.unload("math");
    vm_error("vm.module", [&] { loader.get("math"); });
    module.natives["external"] = {{vm::ValueKind::Integer}, vm::ValueKind::Integer};
    vm_error("vm.native", [&] { loader.link(module, env); });
    env.bind("external", {{}, vm::ValueKind::Null}, [](const std::vector<vm::Value>&) { return vm::Value{}; });
    vm_error("vm.signature", [&] { loader.link(module, env); });
    env.bind("external", module.natives.at("external"), [](const std::vector<vm::Value>& args) { return args[0]; });
    loader.link(module, env);
}); }
