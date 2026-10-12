#include "../../tests/test.hpp"
#include <mlvm/mlvm.hpp>
#include <mlvm/optilisp/pass.hpp>
#include <fstream>
#include <sstream>

using namespace mlvm;
using namespace mlvm::optilisp;
int main() { return test_main([] {
    const std::filesystem::path root = MLVM_SOURCE_ROOT;
    const auto registry_path = root / "libstone/blackstone/optilisp/stdlib";
    const auto catalog_path = root / "libstone/blackstone/kernels";
    const auto add_path = catalog_path / "hir/constant-folding/integer-add.olisp";
    auto registry = load_registry(registry_path); CHECK(registry.ok());
    const std::filesystem::path files[] = {add_path};
    CHECK(!Loader(registry.registry).load(files).ok()); // strict remains strict
    Loader loader(registry.registry, {}, Loader::Profile::blackstone);
    auto loaded = loader.load(files); CHECK(loaded.ok());
    CHECK(loaded.kernels[0].analyses.requires_analyses.contains("constant-value-analysis"));
    CHECK(loaded.kernels[0].analyses.preserves.empty());
    CHECK(loaded.kernels[0].analyses.invalidates == registry.registry.analyses);
    Context context; DiagnosticEngine diagnostics; SourceManager sources;
    CHECK(register_builtin_dialects(context, diagnostics));
    auto parse_module = [&](const std::string& source) {
        auto module = parse_ir(context, sources, sources.add("test.mlvm", source), diagnostics);
        CHECK(module.has_value()); return std::move(*module);
    };
    auto module = parse_module(R"(mlvm 1 {
      apply "blackstone.hir.constant-folding.integer-add";
      block() {
        %a:"i8" = "arith.constant"() ["value"="127"];
        %b:"i8" = "arith.constant"() ["value"="1"];
        %c:"i8" = "arith.addi"(%a, %b) [];
        %d:"i8" = "arith.muli"(%a, %b) [];
      }
    })");
    auto roundtrip = print_ir(module);
    CHECK(print_ir(parse_module(roundtrip)) == roundtrip);
    CHECK(run_kernels(module, registry_path, catalog_path, diagnostics));
    CHECK(module.body.blocks[0].operations[2].attributes.at("value") == "-128");
    CHECK(module.body.blocks[0].operations[3].name == "arith.muli"); // one kernel, one objective
    CHECK(module.kernel_applications.empty()); CHECK(verify(module, diagnostics));

    // A custom kernel keeps the established grammar and contract, while authoring
    // a different pattern: the commuted additive identity.
    std::ifstream input(catalog_path / "hir/algebraic-simplification/add-zero.olisp");
    std::ostringstream buffer; buffer << input.rdbuf(); auto custom = buffer.str();
    auto replace = [&](std::string from, std::string to) {
        auto at = custom.find(from); CHECK(at != std::string::npos); custom.replace(at, from.size(), to);
    };
    replace("blackstone.hir.algebraic-simplification.add-zero", "blackstone.hir.custom.zero-left");
    replace("(add x (integer 0))", "(add (integer 0) x)");
    CHECK(loader.load_source(custom).ok());
    auto identity = parse_module(R"(mlvm 1 { block(%x:"i32") {
      %zero:"i32" = "arith.constant"() ["value"="0"];
      %one:"i32" = "arith.constant"() ["value"="1"];
      %sum:"i32" = "arith.addi"(%zero, %x) [];
      %other:"i32" = "arith.addi"(%one, %x) [];
      %use:"i32" = "arith.muli"(%sum, %other) [];
    } })");
    identity.kernel_sources.push_back(custom);
    identity.kernel_applications.push_back("blackstone.hir.custom.zero-left");
    auto identity_text = print_ir(identity);
    CHECK(print_ir(parse_module(identity_text)) == identity_text);
    CHECK(run_kernels(identity, registry_path, catalog_path, diagnostics));
    CHECK(identity.body.blocks[0].operations.size() == 4);
    CHECK(identity.body.blocks[0].operations.back().operands[0] == identity.body.blocks[0].arguments[0]);
    CHECK(identity.body.blocks[0].operations[2].name == "arith.addi");
    CHECK(verify(identity, diagnostics));

    // Unsupported/unknown contracts and malformed author input never commit.
    for (const auto& id : {"missing.kernel", "blackstone.hir.constant-folding.float-add"}) {
        auto bad = parse_module(roundtrip); bad.kernel_applications.push_back(id);
        auto before = print_ir(bad);
        CHECK(!run_kernels(bad, registry_path, catalog_path, diagnostics));
        CHECK(print_ir(bad) == before); diagnostics.clear();
    }
    auto bad = identity; bad.kernel_sources.push_back(custom);
    auto before = print_ir(bad);
    CHECK(!run_kernels(bad, registry_path, catalog_path, diagnostics));
    CHECK(print_ir(bad) == before); diagnostics.clear();
    CHECK(!loader.load_source(custom + "(").ok());
    replace("addition-has-identity-zero", "unknown-proof");
    CHECK(loader.load_source(custom).ok());
    CHECK(!integer_optimizer()->supports(loader.load_source(custom).kernels[0]));

    // Differential modular evaluation over widths, signs and overflow boundaries.
    for (const auto& family : {"add", "sub", "mul", "bitwise-and", "bitwise-or", "bitwise-xor"}) {
        const std::filesystem::path paths[] = {catalog_path / ("hir/constant-folding/integer-" + std::string(family) + ".olisp")};
        auto kernels = loader.load(paths); CHECK(kernels.ok());
        for (unsigned bits : {1u, 8u, 32u, 64u}) for (std::int64_t a : {-1, 0, 1}) {
            Module fragment(context); auto& block = fragment.add_block(fragment.body);
            const auto type = context.intern("i" + std::to_string(bits));
            auto lhs = fragment.append(block, "arith.constant", {}, std::span(&type, 1)).results[0];
            block.operations.back().attributes["value"] = std::to_string(a);
            auto rhs = fragment.append(block, "arith.constant", {}, std::span(&type, 1)).results[0];
            block.operations.back().attributes["value"] = "1";
            const ValueId operands[] = {lhs, rhs};
            const auto opcode = std::string(family) == "add" ? "arith.addi" : std::string(family) == "sub" ? "arith.subi" :
                std::string(family) == "mul" ? "arith.muli" : std::string(family) == "bitwise-and" ? "arith.and" :
                std::string(family) == "bitwise-or" ? "arith.or" : "arith.xor";
            fragment.append(block, opcode, operands, std::span(&type, 1));
            PassManager manager;
            manager.analyses.register_analysis("constant-value-analysis", [](const Module&) { return std::any(1); });
            manager.add(kernel_pass(kernels.kernels[0], integer_optimizer()));
            CHECK(manager.run(fragment, diagnostics));
            const auto ua = static_cast<std::uint64_t>(a);
            auto expected = std::string(family) == "add" ? ua + 1 : std::string(family) == "sub" ? ua - 1 :
                std::string(family) == "mul" ? ua : std::string(family) == "bitwise-and" ? ua & 1 :
                std::string(family) == "bitwise-or" ? ua | 1 : ua ^ 1;
            const auto mask = bits == 64 ? ~std::uint64_t{0} : (std::uint64_t{1} << bits) - 1;
            auto actual = std::stoll(fragment.body.blocks[0].operations.back().attributes.at("value"));
            CHECK((static_cast<std::uint64_t>(actual) & mask) == (expected & mask));
        }
    }
}); }
