#pragma once
#include <dragscript/diagnostic.hpp>
#include <dragscript/host_api.hpp>
#include <atomic>
#include <memory>

namespace dragscript {
// Execution backends share values, coercions, scopes, limits and host contracts.
// They differ only in dispatch: DSIR source nodes, or the verified MLVM ds.* tree.
enum class Backend { dsir, mlvm_ir };
struct EngineOptions {
    std::size_t max_source_bytes = 1024 * 1024;
    std::size_t max_nodes = 50000; // All syntax nodes, including punctuation. Hard ceiling: 50000.
    std::size_t max_compile_depth = 64; // Hard ceiling: 128.
    std::size_t max_diagnostics = 16; // At least one diagnostic is retained.
    std::size_t max_string_units = 1024 * 1024; // UTF-16 code units.
    std::size_t max_parse_checks = 10000; // Tree-sitter progress callback checks.
    std::size_t max_array_length = 65536; // Dense elements; index assignment cannot exceed it.
    std::size_t max_properties_per_object = 4096;
    std::size_t max_property_name_units = 256;
    bool enable_jsx = false; // Selects JSX grammar; JSX execution is still rejected.
    Backend backend = Backend::dsir;
};
struct CompileOptions { const std::atomic_bool* cancellation = nullptr; };
struct RunOptions {
    std::uint64_t max_steps = 100000;
    std::size_t max_call_depth = 64; // Hard ceiling: 128.
    std::size_t max_evaluation_depth = 256; // Hard ceiling: 512.
    std::size_t max_heap_bytes = 8 * 1024 * 1024; // Logical allocation quota; see docs.
    const std::atomic_bool* cancellation = nullptr; // Must outlive the call.
};
namespace detail { struct Artifact; }
// Immutable, owning, reusable across Engines; contains no instance-specific handles.
class CompiledScript {
public:
    CompiledScript() = default;
    [[nodiscard]] bool valid() const noexcept { return bool(artifact_); }
    [[nodiscard]] std::string dump_dsir() const;
    [[nodiscard]] std::string dump_mlvm() const;
private:
    std::shared_ptr<const detail::Artifact> artifact_;
    explicit CompiledScript(std::shared_ptr<const detail::Artifact> artifact) : artifact_(std::move(artifact)) {}
    friend class Engine;
};
struct CompileResult {
    CompiledScript script;
    std::vector<Diagnostic> diagnostics;
    [[nodiscard]] bool ok() const noexcept { return script.valid() && diagnostics.empty(); }
};
struct RunResult {
    Value value;
    std::vector<Diagnostic> diagnostics;
    std::uint64_t steps = 0;
    std::size_t heap_bytes = 0; // Cumulative logical allocation charges, not process RSS.
    [[nodiscard]] bool ok() const noexcept { return diagnostics.empty(); }
};
// Externally synchronized. Each run has a fresh script environment.
// Same-engine reentrancy is rejected; callback capabilities remain per-engine.
class Engine {
public:
    explicit Engine(EngineOptions options = {});
    ~Engine();
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;
    bool register_host_function(std::string name, HostFunction callback, HostFunctionOptions options = {});
    CompileResult compile(Source source, CompileOptions options = {});
    RunResult run(const CompiledScript& script, RunOptions options = {});

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace dragscript
