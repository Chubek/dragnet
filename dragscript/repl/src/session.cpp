#include "editor.hpp"
#include <algorithm>
#include <cstdint>

namespace dragscript::repl_detail {
namespace {
std::uint32_t newlines(const std::string& text) {
    return static_cast<std::uint32_t>(std::count(text.begin(), text.end(), '\n'));
}
} // namespace

std::uint32_t submission_line(std::uint32_t program_line, std::uint32_t first_line) {
    // A span inside the preamble or the earlier context has no submission line; it is
    // reported as-is rather than mapped to a misleading number.
    return program_line >= first_line ? program_line - first_line + 1 : program_line;
}

SessionSource compose_session(const std::string& preamble, const std::string& context,
                              const std::string& submission) {
    // Both preamble and context are newline-terminated, so their newline counts are
    // exactly the number of complete lines preceding the submission. Inserting another
    // separator here would shift every reported line by one.
    return SessionSource{preamble + context + submission, 1 + newlines(preamble) + newlines(context)};
}

std::string extend_context(const std::string& context, const std::string& submission) {
    if (submission.empty()) return context;
    std::string extended = context + submission;
    if (extended.back() != '\n') extended += '\n';
    return extended;
}
} // namespace dragscript::repl_detail