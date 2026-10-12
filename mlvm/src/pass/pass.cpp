#include <mlvm/pass.hpp>
#include <bit>
#include <charconv>

namespace mlvm {
bool AnalysisManager::register_analysis(std::string name, Producer producer) {
    return !name.empty() && producer && producers_.emplace(std::move(name), std::move(producer)).second;
}
const std::any* AnalysisManager::get(std::string_view name, const Module& module) {
    if (auto it = cache_.find(name); it != cache_.end()) return &it->second;
    auto it = producers_.find(name); if (it == producers_.end()) return nullptr;
    return &cache_.emplace(it->first, it->second(module)).first->second;
}
void AnalysisManager::invalidate(const std::set<std::string>& preserved) {
    for (auto it = cache_.begin(); it != cache_.end();) {
        if (preserved.contains(it->first)) ++it; else it = cache_.erase(it);
    }
}
bool PassManager::run(Module& module, DiagnosticEngine& diagnostics) {
    analyses.invalidate();
    if (!verify(module, diagnostics)) return false;
    for (const auto& pass : passes_) {
        if (!pass.run) { diagnostics.error("pass.invalid", "pass has no implementation: " + pass.name); return false; }
        for (const auto& group : {pass.consumes, pass.produces}) for (const auto& dialect : group)
            if (!module.context->has_dialect(dialect)) { diagnostics.error("pass.dialect", "missing dialect: " + dialect); return false; }
        Module candidate = module;
        PassEvent event{pass.name, false, {}, dump_ir ? print_ir(module) : "", {}};
        const auto start = std::chrono::steady_clock::now();
        const auto diagnostic_start = diagnostics.diagnostics().size();
        bool ok = true;
        try {
            for (const auto& name : pass.requires_analyses) if (!analyses.get(name, candidate)) {
                diagnostics.error("pass.analysis", "unregistered required analysis: " + name); ok = false;
            }
            if (ok) ok = pass.run(candidate, analyses, diagnostics);
            for (std::size_t i = diagnostic_start; i < diagnostics.diagnostics().size(); ++i) {
                const auto severity = diagnostics.diagnostics()[i].severity;
                if (severity == Severity::error || severity == Severity::fatal) ok = false;
            }
            if (ok && candidate.context != module.context) { diagnostics.error("pass.context", "pass changed the module context"); ok = false; }
            if (ok) ok = verify(candidate, diagnostics);
        } catch (const std::exception& error) {
            diagnostics.error("pass.exception", pass.name + ": " + error.what()); ok = false;
        }
        event.success = ok; event.elapsed = std::chrono::steady_clock::now() - start;
        if (ok) {
            module = std::move(candidate); analyses.invalidate(pass.preserves_analyses);
            if (dump_ir) event.after = print_ir(module);
        } else {
            analyses.invalidate(); diagnostics.error("pass.failed", "pass '" + pass.name + "' failed; its changes were discarded");
        }
        if (instrumentation) instrumentation(event);
        if (!ok) return false;
    }
    return true;
}
Pass constant_fold_pass() {
    return {"constant-fold", {"arith"}, {"arith"}, {}, {},
        [](Module& module, AnalysisManager&, DiagnosticEngine&) {
            std::map<ValueId, std::uint64_t> constants;
            auto width_of = [&](ValueId id) -> unsigned {
                const auto* v = module.value(id);
                if (!v) return 64;
                const auto type = module.context->spelling(v->type);
                unsigned width = 0;
                std::from_chars(type.data() + 1, type.data() + type.size(), width);
                return width ? width : 64;
            };
            auto mask = [](unsigned width) -> std::uint64_t {
                return width >= 64 ? ~std::uint64_t{0} : ((std::uint64_t{1} << width) - 1);
            };
            auto sign_extend = [](std::uint64_t bits, unsigned width) -> std::int64_t {
                if (width >= 64) return std::bit_cast<std::int64_t>(bits);
                bits &= (std::uint64_t{1} << width) - 1;
                if (width && (bits & (std::uint64_t{1} << (width - 1)))) bits |= ~((std::uint64_t{1} << width) - 1);
                return std::bit_cast<std::int64_t>(bits);
            };
            walk(module, [&](Operation& op) {
                if (op.name == "arith.constant") {
                    std::int64_t value = 0;
                    const auto& s = op.attributes.at("value"); std::from_chars(s.data(), s.data() + s.size(), value);
                    constants[op.results[0]] = static_cast<std::uint64_t>(value);
                } else if (op.name == "arith.addi" || op.name == "arith.subi" || op.name == "arith.muli" ||
                           op.name == "arith.and" || op.name == "arith.or" || op.name == "arith.xor") {
                    if (!constants.contains(op.operands[0]) || !constants.contains(op.operands[1])) return;
                    const auto a = constants.at(op.operands[0]), b = constants.at(op.operands[1]);
                    std::uint64_t result = op.name == "arith.addi" ? a + b : op.name == "arith.subi" ? a - b
                        : op.name == "arith.muli"                                 ? a * b
                        : op.name == "arith.and"                                  ? a & b
                        : op.name == "arith.or"                                   ? a | b
                                                                                 : a ^ b;
                    const unsigned width = width_of(op.results[0]);
                    if (width < 64) result &= mask(width);
                    constants[op.results[0]] = result;
                    op.name = "arith.constant"; op.operands.clear();
                    op.attributes = {{"value", std::to_string(sign_extend(result, width))}};
                } else if (op.name == "arith.divsi" || op.name == "arith.remsi") {
                    if (!constants.contains(op.operands[0]) || !constants.contains(op.operands[1])) return;
                    const unsigned width = width_of(op.results[0]);
                    const auto a = sign_extend(constants.at(op.operands[0]), width);
                    const auto b = sign_extend(constants.at(op.operands[1]), width);
                    // Division by zero and INT_MIN / -1 are undefined: leave unfolded.
                    if (b == 0) return;
                    if (a == INT64_MIN && b == -1 && width == 64) return;
                    if (width < 64) {
                        const auto min = -(std::int64_t{1} << (width - 1));
                        if (a == min && b == -1) return;
                    }
                    const std::int64_t q = op.name == "arith.divsi" ? a / b : a % b;
                    constants[op.results[0]] = static_cast<std::uint64_t>(q) & mask(width);
                    op.name = "arith.constant"; op.operands.clear();
                    op.attributes = {{"value", std::to_string(q)}};
                }
            });
            module.rebuild_uses(); return true;
        }};
}
Pass dead_code_elimination_pass() {
    return {"dead-code-elimination", {}, {}, {}, {},
        [](Module& module, AnalysisManager&, DiagnosticEngine& diagnostics) {
            std::function<void(Region&)> eliminate = [&](Region& region) {
                for (auto& block : region.blocks) {
                    for (auto& op : block.operations) for (auto& nested : op.regions) eliminate(nested);
                    for (std::size_t i = block.operations.size(); i > 0; --i) {
                        const auto& op = block.operations[i - 1];
                        const auto* def = module.context->operation(op.name);
                        if (!def || def->effect != Effect::pure || def->terminator || !op.regions.empty()) continue;
                        bool live = false;
                        for (auto result : op.results) if (!module.value(result)->uses.empty()) live = true;
                        if (!live) module.erase(block, op.id, diagnostics);
                    }
                }
            };
            eliminate(module.body); return true;
        }};
}
Pass canonicalize_pass() {
    return {"canonicalize", {"arith"}, {"arith"}, {}, {},
        [](Module& module, AnalysisManager&, DiagnosticEngine& diagnostics) {
            // Identity/absorbing rewrites only; no reassociation or distribution.
            // Each rewrite replaces result uses with an existing same-type value,
            // then erases the now-dead operation.
            std::function<void(Region&)> simplify = [&](Region& region) {
                for (auto& block : region.blocks) {
                    for (auto& op : block.operations) for (auto& nested : op.regions) simplify(nested);
                    for (std::size_t i = 0; i < block.operations.size();) {
                        Operation& op = block.operations[i];
                        std::optional<ValueId> replacement;
                        if ((op.name == "arith.addi" || op.name == "arith.or" || op.name == "arith.xor") &&
                            op.operands.size() == 2 && op.results.size() == 1) {
                            // x + 0 = x, x | 0 = x, x ^ 0 = x when either operand is constant 0.
                            for (int k = 0; k < 2 && !replacement; ++k) {
                                ValueId cid = op.operands[static_cast<std::size_t>(k)];
                                bool is_zero = false;
                                walk(module, [&](Operation& probe) {
                                    if (probe.name == "arith.constant" && !probe.results.empty() &&
                                        probe.results[0] == cid && probe.attributes.at("value") == "0")
                                        is_zero = true;
                                });
                                if (is_zero) replacement = op.operands[1 - static_cast<std::size_t>(k)];
                            }
                        } else if (op.name == "arith.muli" && op.operands.size() == 2 && op.results.size() == 1) {
                            // x * 1 = x when either operand is constant 1.
                            for (int k = 0; k < 2 && !replacement; ++k) {
                                ValueId cid = op.operands[static_cast<std::size_t>(k)];
                                bool is_one = false;
                                walk(module, [&](Operation& probe) {
                                    if (probe.name == "arith.constant" && !probe.results.empty() &&
                                        probe.results[0] == cid && probe.attributes.at("value") == "1")
                                        is_one = true;
                                });
                                if (is_one) replacement = op.operands[1 - static_cast<std::size_t>(k)];
                            }
                        } else if ((op.name == "arith.subi" || op.name == "arith.xor") &&
                                   op.operands.size() == 2 && op.results.size() == 1 &&
                                   op.operands[0] == op.operands[1]) {
                            // x - x = 0 and x ^ x = 0: fold in place to a zero constant.
                            op.name = "arith.constant"; op.operands.clear();
                            op.attributes = {{"value", "0"}};
                            module.rebuild_uses(); ++i; continue;
                        }
                        if (replacement) {
                            if (!module.replace_uses(op.results[0], *replacement, diagnostics)) return;
                            const auto id = op.id;
                            if (!module.erase(block, id, diagnostics)) return;
                            continue; // block.operations shifted; re-examine index i.
                        }
                        ++i;
                    }
                }
            };
            simplify(module.body); return true;
        }};
}
} // namespace mlvm
