#include <dragscript/engine.hpp>
#include "semantic/dsir.hpp"
#include "mlvm/lowering.hpp"
#include <algorithm>
#include <new>

namespace dragscript {
struct Engine::Impl {
    EngineOptions options;
    detail::Hosts hosts;
    bool active = false;
    explicit Impl(EngineOptions options) : options(options) {
        this->options.max_compile_depth = std::min<std::size_t>(options.max_compile_depth, 128);
        this->options.max_nodes = std::min<std::size_t>(options.max_nodes, 50000);
        this->options.max_diagnostics = std::max<std::size_t>(options.max_diagnostics, 1);
    }
};
namespace {
struct ActiveGuard {
    bool& active;
    explicit ActiveGuard(bool& active) : active(active) { active = true; }
    ~ActiveGuard() { active = false; }
};
} // namespace
Engine::Engine(EngineOptions options) : impl_(std::make_unique<Impl>(options)) {}
Engine::~Engine() = default;
bool Engine::register_host_function(std::string name, HostFunction callback, HostFunctionOptions options) {
    if (impl_->active || !callback || !detail::identifier_name(name) || options.min_arguments > options.max_arguments ||
        name == "undefined" || name == "NaN" || name == "Infinity") return false;
    return impl_->hosts.emplace(std::move(name), detail::HostBinding{std::move(callback), options}).second;
}
CompileResult Engine::compile(Source source, CompileOptions options) {
    CompileResult result;
    if (impl_->active) {
        result.diagnostics.push_back(detail::diagnostic(source, "DS_REENTRANT", "Engine is already executing a request"));
        return result;
    }
    ActiveGuard active(impl_->active);
    const Source error_source{source.name, {}, source.kind, source.origin};
    try {
        auto program = detail::frontend(std::move(source), impl_->options, options, result.diagnostics);
        if (!program || !detail::validate(*program, result.diagnostics)) return result;
        mlvm::DiagnosticEngine diagnostics;
        auto lowered = detail::lower(*program, diagnostics);
        if (!lowered) {
            for (const auto& d : diagnostics.diagnostics()) {
                if (result.diagnostics.size() == impl_->options.max_diagnostics) break;
                SourceSpan span;
                for (const auto& node : program->nodes)
                    if (node.span.begin.line == d.location.line && node.span.begin.column == d.location.column) { span = node.span; break; }
                result.diagnostics.push_back(detail::diagnostic(program->source, "DS_IR", d.code + ": " + d.message, span));
            }
            if (result.diagnostics.empty()) result.diagnostics.push_back(detail::diagnostic(program->source, "DS_IR", "MLVM lowering failed"));
            return result;
        }
        if (options.cancellation && options.cancellation->load(std::memory_order_relaxed)) {
            result.diagnostics.push_back(detail::diagnostic(program->source, "DS_CANCELLED", "Compilation cancelled")); return result;
        }
        auto artifact = std::make_shared<detail::Artifact>();
        artifact->mlvm_ir = lowered->text;
        artifact->lowered = std::make_shared<detail::LoweredModule>(std::move(*lowered));
        artifact->program = std::move(*program);
        result.script = CompiledScript(std::move(artifact));
    } catch (const std::bad_alloc&) {
        result.diagnostics.push_back(detail::diagnostic(error_source, "DS_ALLOCATION", "Compilation allocation failed"));
    } catch (const std::exception& error) {
        result.diagnostics.push_back(detail::diagnostic(error_source, "DS_INTERNAL", std::string("Compilation failed: ") + error.what()));
    }
    return result;
}
RunResult Engine::run(const CompiledScript& script, RunOptions options) {
    Source empty;
    const auto& source = script.artifact_ ? script.artifact_->program.source : empty;
    if (impl_->active) return {{}, {detail::diagnostic(source, "DS_REENTRANT", "Engine is already executing a request", {}, Phase::runtime)}, 0};
    if (!script.artifact_) return {{}, {detail::diagnostic(source, "DS_ARTIFACT", "Invalid compiled script", {}, Phase::runtime)}, 0};
    ActiveGuard active(impl_->active);
    try {
        options.max_call_depth = std::min<std::size_t>(options.max_call_depth, 128);
        options.max_evaluation_depth = std::min<std::size_t>(options.max_evaluation_depth, 512);
        if (impl_->options.backend == Backend::mlvm_ir) {
            const auto& lowered = script.artifact_->lowered;
            if (!lowered) return {{}, {detail::diagnostic(source, "DS_ARTIFACT", "Compiled script has no verified MLVM module", {}, Phase::runtime)}, 0};
            return detail::interpret_mlvm(*lowered, script.artifact_->program, impl_->hosts, impl_->options, options);
        }
        return detail::interpret(script.artifact_->program, impl_->hosts, impl_->options, options);
    } catch (const std::bad_alloc&) {
        return {{}, {detail::diagnostic(source, "DS_ALLOCATION", "Runtime allocation failed", {}, Phase::runtime)}, 0};
    } catch (const std::exception& error) {
        return {{}, {detail::diagnostic(source, "DS_INTERNAL", std::string("Runtime failed: ") + error.what(), {}, Phase::runtime)}, 0};
    }
}
std::string CompiledScript::dump_dsir() const { return artifact_ ? detail::dump(artifact_->program) : std::string{}; }
std::string CompiledScript::dump_mlvm() const { return artifact_ ? artifact_->mlvm_ir : std::string{}; }
} // namespace dragscript
