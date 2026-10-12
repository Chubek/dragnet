#pragma once
#include <replxx.hxx>
#include <cstdint>
#include <string>

namespace dragscript::repl_detail {
bool needs_more_input(const std::string& input);
replxx::Replxx::completions_t complete(const std::string& input, int& context_length);
void highlight(const std::string& input, replxx::Replxx::colors_t& colors);

// The engine gives each run a fresh environment, so the REPL recompiles the whole
// session. These helpers own that composition and stay free of terminal state.
struct SessionSource {
    std::string program;   // Preamble + context + submission.
    std::uint32_t first_line = 1; // Program line that submission line 1 maps to.
};
// Resolves a diagnostic span measured in `program` back to a submission line.
std::uint32_t submission_line(std::uint32_t program_line, std::uint32_t first_line);
SessionSource compose_session(const std::string& preamble, const std::string& context,
                              const std::string& submission);
// Returns the context extended by an accepted submission; always newline-terminated.
std::string extend_context(const std::string& context, const std::string& submission);
} // namespace dragscript::repl_detail
