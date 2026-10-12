#pragma once
#include <dragscript/engine.hpp>
#include <replxx.hxx>
#include <filesystem>
#include <string>
#include <vector>

namespace dragscript {
struct ReplConfig {
    std::filesystem::path history_file = ".dragscript_history";
    bool multiline = true;
    Backend backend = Backend::dsir;
};

// The engine gives every run a fresh script environment, so the REPL accumulates
// accepted submissions as a growing context and recompiles it with each new one.
// Declarations therefore persist across submissions, as a REPL is expected to do.
class Repl {
public:
    explicit Repl(ReplConfig config = {});
    void run();
private:
    ReplConfig config_;
    std::unique_ptr<Engine> engine_;
    replxx::Replxx editor_;
    std::string context_; // Accepted submissions, re-compiled with every new one.
    std::string pending_;
    bool stopped_ = false;
    void select_backend(Backend backend);
    void execute(const std::string& source);
    void report(std::uint32_t first_line, const std::vector<Diagnostic>& diagnostics) const;
    void command(const std::string& input);
};
} // namespace dragscript
