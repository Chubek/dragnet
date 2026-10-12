#include "editor.hpp"
#include <algorithm>
#include <cctype>

namespace dragscript::repl_detail {
std::vector<std::string> matching(const std::string& input, int& context_length) {
    static const std::vector<std::string> words = {
        "let", "const", "function", "return", "if", "else", "while", "do",
        "for", "of", "break", "continue", "typeof", "void", "true", "false",
        "null", "undefined", "NaN", "Infinity", "number", "string", "boolean",
        "type", "interface", "as", "print", ".help", ".quit", ".exit",
        ".history", ".clear", ".reset", ".backend", ".multiline"
    };
    auto start = input.size();
    while (start > 0) {
        const auto ch = static_cast<unsigned char>(input[start - 1]);
        if (!std::isalnum(ch) && ch != '_' && ch != '$' && ch != '.') break;
        --start;
    }
    const auto prefix = input.substr(start);
    context_length = static_cast<int>(prefix.size()); // Candidates are ASCII only.
    std::vector<std::string> result;
    for (const auto& word : words)
        if (word.starts_with(prefix)) result.push_back(word);
    std::sort(result.begin(), result.end());
    return result;
}

replxx::Replxx::completions_t complete(const std::string& input, int& context_length) {
    replxx::Replxx::completions_t result;
    for (auto& word : matching(input, context_length)) result.emplace_back(std::move(word));
    return result;
}
} // namespace dragscript::repl_detail
