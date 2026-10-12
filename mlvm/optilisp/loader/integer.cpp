#include <mlvm/optilisp/pass.hpp>
#include <algorithm>
#include <bit>
#include <charconv>

namespace mlvm::optilisp {
namespace {
std::string head(const Expr& e) {
    return e.kind == ExprKind::list && !e.items.empty() ? e.items[0].value : "";
}
const std::map<std::string, std::string> operations = {
    {"add", "arith.addi"}, {"sub", "arith.subi"}, {"mul", "arith.muli"},
    {"bitwise-and", "arith.and"}, {"bitwise-or", "arith.or"}, {"bitwise-xor", "arith.xor"}
};
const std::set<std::string> positive = {
    "operation-has-defined-compile-time-semantics", "result-representable-under-integer-semantics",
    "operand-has-compatible-integer-type", "addition-has-identity-zero", "multiplication-has-identity-one"
};
const std::set<std::string> negative = {"operation-has-observable-side-effects", "signed-overflow-traps"};
const std::set<std::string> capabilities = {
    "typed-expressions", "integer-constant-operands", "integer-overflow-semantics"
};
const std::set<std::string> correctness = {"preserve-observable-behavior", "respect-integer-overflow-semantics"};
unsigned width(const Module& module, ValueId id) {
    const auto* value = module.value(id);
    if (!value) return 0;
    const auto type = module.context->spelling(value->type);
    if (type.size() < 2 || type[0] != 'i') return 0;
    unsigned bits = 0;
    const auto r = std::from_chars(type.data() + 1, type.data() + type.size(), bits);
    return r.ec == std::errc{} && r.ptr == type.data() + type.size() && bits <= 64 ? bits : 0;
}
std::uint64_t mask(unsigned bits) { return bits == 64 ? ~std::uint64_t{0} : (std::uint64_t{1} << bits) - 1; }
bool number(const std::string& text, std::uint64_t& bits) {
    std::int64_t value = 0;
    auto r = std::from_chars(text.data(), text.data() + text.size(), value);
    if (r.ec != std::errc{} || r.ptr != text.data() + text.size()) return false;
    bits = static_cast<std::uint64_t>(value); return true;
}
std::string signed_text(std::uint64_t bits, unsigned size) {
    bits &= mask(size);
    if (size < 64 && (bits & (std::uint64_t{1} << (size - 1)))) bits |= ~mask(size);
    return std::to_string(std::bit_cast<std::int64_t>(bits));
}
struct Binding { ValueId value = 0; std::uint64_t literal = 0; bool constant = false; };
using Bindings = std::map<std::string, Binding>;
using Definitions = std::map<ValueId, const Operation*>;
bool constant(ValueId id, const Definitions& defs, std::uint64_t& bits) {
    auto it = defs.find(id);
    return it != defs.end() && it->second->name == "arith.constant" &&
        it->second->attributes.contains("value") && number(it->second->attributes.at("value"), bits);
}
bool bind(const Expr& pattern, Binding binding, Bindings& bindings) {
    if (pattern.kind == ExprKind::number) {
        std::uint64_t literal = 0;
        return binding.constant && number(pattern.value, literal) && binding.literal == literal;
    }
    if (pattern.kind != ExprKind::symbol) return false;
    auto [it, inserted] = bindings.emplace(pattern.value, binding);
    return inserted || (it->second.constant == binding.constant &&
        (binding.constant ? it->second.literal == binding.literal : it->second.value == binding.value));
}
bool match(const Expr& pattern, ValueId id, const Definitions& defs, Bindings& bindings) {
    if (pattern.kind == ExprKind::symbol) return bind(pattern, {id, 0, false}, bindings);
    auto found = defs.find(id);
    if (found == defs.end() || pattern.kind != ExprKind::list) return false;
    const auto& op = *found->second;
    if (head(pattern) == "integer" && pattern.items.size() == 2) {
        std::uint64_t bits = 0;
        return constant(id, defs, bits) && bind(pattern.items[1], {0, bits, true}, bindings);
    }
    auto opcode = operations.find(head(pattern));
    if (opcode == operations.end() || op.name != opcode->second || pattern.items.size() != 3 ||
        op.operands.size() != 2 || !op.attributes.empty()) return false;
    return match(pattern.items[1], op.operands[0], defs, bindings) && match(pattern.items[2], op.operands[1], defs, bindings);
}
bool valid_pattern(const Expr& pattern) {
    if (pattern.kind == ExprKind::symbol) return true;
    if (head(pattern) == "integer" && pattern.items.size() == 2)
        return pattern.items[1].kind == ExprKind::symbol || pattern.items[1].kind == ExprKind::number;
    return operations.contains(head(pattern)) && pattern.items.size() == 3 &&
        valid_pattern(pattern.items[1]) && valid_pattern(pattern.items[2]);
}
bool legal_forward(const Operation& op, ValueId result, const Definitions& defs, unsigned bits) {
    if (op.operands.size() != 2) return false;
    const auto a = op.operands[0], b = op.operands[1];
    if ((op.name == "arith.and" || op.name == "arith.or") && a == b && result == a) return true;
    for (unsigned i = 0; i < 2; ++i) {
        if (result != op.operands[i]) continue;
        std::uint64_t c = 0;
        if (!constant(op.operands[1 - i], defs, c)) continue;
        c &= mask(bits);
        if ((op.name == "arith.addi" || op.name == "arith.or" || op.name == "arith.xor") && c == 0) return true;
        if (op.name == "arith.subi" && i == 0 && c == 0) return true;
        if (op.name == "arith.muli" && c == 1) return true;
        if (op.name == "arith.and" && c == mask(bits)) return true;
    }
    return false;
}
class IntegerOptimizer final : public ModuleOptimizer {
public:
    bool supports(const Kernel& k) const override {
        if (k.stage != Stage::hir || !k.prerequisites.empty() || !k.forbidden_prior.empty()) return false;
        for (const auto& r : k.requirements)
            if (r.kind == "capability" ? !capabilities.contains(r.id) : r.kind != "analysis" || r.id != "constant-value-analysis") return false;
        for (const auto& p : k.preconditions) if (!positive.contains(p)) return false;
        for (const auto& p : k.forbids) if (!negative.contains(p)) return false;
        for (const auto& p : k.correctness) if (!correctness.contains(p)) return false;
        const auto& t = k.transformation;
        if (t.items.size() != 3 || !operations.contains(head(t.items[1])) || !valid_pattern(t.items[1])) return false;
        if (std::find(k.preconditions.begin(), k.preconditions.end(), "addition-has-identity-zero") != k.preconditions.end() && head(t.items[1]) != "add") return false;
        if (std::find(k.preconditions.begin(), k.preconditions.end(), "multiplication-has-identity-one") != k.preconditions.end() && head(t.items[1]) != "mul") return false;
        if (head(t) == "rewrite") {
            if (t.items[2].kind != ExprKind::symbol) return false;
            std::function<bool(const Expr&)> bound = [&](const Expr& pattern) {
                if (pattern.kind == ExprKind::symbol) return pattern.value == t.items[2].value;
                if (head(pattern) == "integer") return false;
                return bound(pattern.items[1]) || bound(pattern.items[2]);
            };
            return bound(t.items[1]);
        }
        if (head(t) != "fold" || head(t.items[2]) != "integer-result" || t.items[2].items.size() != 2) return false;
        const auto& computation = t.items[2].items[1];
        auto name = head(computation);
        if (name.starts_with("integer-")) name = name.substr(8);
        // Only the same operation on the same literal bindings is executable.
        const auto& pattern = t.items[1];
        return name == head(pattern) && computation.items.size() == 3 &&
            head(pattern.items[1]) == "integer" && head(pattern.items[2]) == "integer" &&
            computation.items[1] == pattern.items[1].items[1] && computation.items[2] == pattern.items[2].items[1];
    }
    Stage stage(const Module&) const override { return Stage::hir; }
    Fact fact(std::string_view key, const Kernel&, const Module& module, AnalysisManager& analyses) const override {
        if (key.starts_with("capability:") && capabilities.contains(std::string(key.substr(11)))) return Fact::proven_true;
        if (key == "analysis:constant-value-analysis") return analyses.get("constant-value-analysis", module) ? Fact::proven_true : Fact::not_computed;
        if (key.starts_with("condition:")) {
            auto p = std::string(key.substr(10));
            if (positive.contains(p)) return Fact::proven_true;
            if (negative.contains(p)) return Fact::proven_false;
        }
        return Fact::unknown;
    }
    bool apply(const Kernel& k, Module& module, AnalysisManager&, DiagnosticEngine& diagnostics) override {
        std::function<bool(Region&)> visit = [&](Region& region) {
            for (auto& block : region.blocks) {
                for (std::size_t i = 0; i < block.operations.size();) {
                    auto& op = block.operations[i];
                    for (auto& child : op.regions) if (!visit(child)) return false;
                    Definitions defs;
                    walk(module, [&](const Operation& definition) {
                        if (definition.results.size() == 1) defs.emplace(definition.results[0], &definition);
                    });
                    Bindings bindings;
                    const auto* definition = module.context->operation(op.name);
                    const unsigned bits = op.results.size() == 1 ? width(module, op.results[0]) : 0;
                    if (!bits || !definition || definition->effect != Effect::pure || !op.regions.empty() ||
                        !match(k.transformation.items[1], op.results[0], defs, bindings)) { ++i; continue; }
                    bool requires_constants = false;
                    for (const auto& requirement : k.requirements)
                        if (requirement.kind == "capability" && requirement.id == "integer-constant-operands") requires_constants = true;
                    if (requires_constants) {
                        std::uint64_t ignored = 0;
                        if (!constant(op.operands[0], defs, ignored) || !constant(op.operands[1], defs, ignored)) { ++i; continue; }
                    }
                    if (head(k.transformation) == "rewrite") {
                        auto binding = bindings.find(k.transformation.items[2].value);
                        if (binding == bindings.end() || binding->second.constant ||
                            !legal_forward(op, binding->second.value, defs, bits)) { ++i; continue; }
                        const auto id = op.id;
                        if (!module.replace_uses(op.results[0], binding->second.value, diagnostics) ||
                            !module.erase(block, id, diagnostics)) return false;
                        continue;
                    }
                    std::uint64_t a = 0, b = 0;
                    if (!constant(op.operands[0], defs, a) || !constant(op.operands[1], defs, b)) { ++i; continue; }
                    const auto result = op.name == "arith.addi" ? a + b : op.name == "arith.subi" ? a - b :
                        op.name == "arith.muli" ? a * b : op.name == "arith.and" ? a & b : op.name == "arith.or" ? a | b : a ^ b;
                    op.name = "arith.constant"; op.operands.clear();
                    op.attributes = {{"value", signed_text(result, bits)}};
                    module.rebuild_uses(); ++i;
                }
            }
            return true;
        };
        return visit(module.body);
    }
};
void report(const Diagnostics& errors, DiagnosticEngine& diagnostics) {
    for (const auto& error : errors) diagnostics.error(error.code, error.render());
}
}
std::shared_ptr<ModuleOptimizer> integer_optimizer() { return std::make_shared<IntegerOptimizer>(); }

bool run_kernels(Module& module, const std::filesystem::path& registry_directory,
                 const std::filesystem::path& kernel_directory, DiagnosticEngine& diagnostics) {
    if (module.kernel_sources.empty() && module.kernel_applications.empty()) return true;
    const auto registry = load_registry(registry_directory);
    if (!registry.ok()) { report(registry.diagnostics, diagnostics); return false; }
    Loader loader(registry.registry, {}, Loader::Profile::blackstone);
    std::map<std::string, Kernel> kernels;
    auto add = [&](const LoadResult& result) {
        if (!result.ok()) { report(result.diagnostics, diagnostics); return false; }
        for (const auto& kernel : result.kernels) if (!kernels.emplace(kernel.id, kernel).second) {
            diagnostics.error("optilisp.duplicate", "duplicate kernel ID: " + kernel.id); return false;
        }
        return true;
    };
    for (std::size_t i = 0; i < module.kernel_sources.size(); ++i)
        if (!add(loader.load_source(module.kernel_sources[i], "<IR kernel " + std::to_string(i) + ">"))) return false;
    // Index identities, rather than deriving them from filenames. Only requested
    // upstream definitions are validated; unrelated catalog defects cannot hide
    // diagnostics in, or prevent use of, a selected valid kernel.
    std::set<std::string> missing;
    for (const auto& id : module.kernel_applications) if (!kernels.contains(id)) missing.insert(id);
    const std::set<std::string> requested(module.kernel_applications.begin(), module.kernel_applications.end());
    std::error_code directory_error;
    if (!missing.empty() || (!requested.empty() && std::filesystem::is_directory(kernel_directory, directory_error))) {
        std::map<std::string, std::filesystem::path> index;
        try {
            std::vector<std::filesystem::path> files;
            for (const auto& entry : std::filesystem::recursive_directory_iterator(kernel_directory))
                if (entry.is_regular_file() && entry.path().extension() == ".olisp") files.push_back(entry.path());
            std::sort(files.begin(), files.end());
            for (const auto& path : files) {
                const auto parsed = parse_file(path.string());
                for (const auto& form : parsed.forms) if (head(form) == "define-kernel")
                    for (std::size_t i = 1; i + 1 < form.items.size(); ++i)
                        if (form.items[i].kind == ExprKind::keyword && form.items[i].value == ":id" &&
                            form.items[i + 1].kind == ExprKind::string && requested.contains(form.items[i + 1].value)) {
                            if (!index.emplace(form.items[i + 1].value, path).second) {
                                diagnostics.error("optilisp.duplicate", "ambiguous catalog ID: " + form.items[i + 1].value); return false;
                            }
                        }
            }
            for (const auto& [id, path] : index) if (kernels.contains(id)) {
                diagnostics.error("optilisp.duplicate", "custom kernel conflicts with catalog ID: " + id + " in " + path.string()); return false;
            }
            std::vector<std::filesystem::path> selected;
            for (const auto& id : missing) {
                if (!index.contains(id)) { diagnostics.error("optilisp.reference", "unknown kernel: " + id); return false; }
                selected.push_back(index.at(id));
            }
            if (!selected.empty() && !add(loader.load(selected))) return false;
        } catch (const std::exception& error) { diagnostics.error("optilisp.io", error.what()); return false; }
    }
    auto optimizer = integer_optimizer();
    PassManager passes;
    passes.analyses.register_analysis("constant-value-analysis", [](const Module& input) {
        std::map<ValueId, std::uint64_t> constants;
        walk(input, [&](const Operation& op) {
            std::uint64_t bits = 0;
            if (op.name == "arith.constant" && op.results.size() == 1 && number(op.attributes.at("value"), bits))
                constants.emplace(op.results[0], bits & mask(width(input, op.results[0])));
        });
        return std::any(std::move(constants));
    });
    for (const auto& id : module.kernel_applications) {
        const auto& kernel = kernels.at(id);
        if (!optimizer->supports(kernel)) {
            diagnostics.error("optilisp.backend", "unsupported integer kernel contract: " + id); return false;
        }
        passes.add(kernel_pass(kernel, optimizer));
    }
    Module candidate = module;
    if (!passes.run(candidate, diagnostics)) return false;
    candidate.kernel_applications.clear();
    module = std::move(candidate); return true;
}
} // namespace mlvm::optilisp
