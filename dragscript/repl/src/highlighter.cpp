#include "editor.hpp"
#include <algorithm>
#include <cctype>
#include <string_view>

namespace dragscript::repl_detail {
namespace {
enum class State { code, string, line_comment, block_comment };
struct Scan {
    std::vector<replxx::Replxx::Color> bytes;
    bool incomplete = false;
};
Scan scan(const std::string& input) {
    using Color = replxx::Replxx::Color;
    Scan result{std::vector<Color>(input.size(), Color::DEFAULT), false};
    State state = State::code;
    char quote = 0;
    std::string brackets;
    bool malformed = false;
    const std::string_view keywords = " let const function return if else while do for of break continue typeof void true false null undefined type interface as number string boolean ";
    for (std::size_t i = 0; i < input.size(); ++i) {
        const char ch = input[i];
        const char next = i + 1 < input.size() ? input[i + 1] : '\0';
        if (state == State::line_comment) {
            result.bytes[i] = Color::GRAY;
            if (ch == '\n') state = State::code;
        } else if (state == State::block_comment) {
            result.bytes[i] = Color::GRAY;
            if (ch == '*' && next == '/') {
                result.bytes[++i] = Color::GRAY;
                state = State::code;
            }
        } else if (state == State::string) {
            result.bytes[i] = Color::GREEN;
            if (ch == '\\' && i + 1 < input.size()) result.bytes[++i] = Color::GREEN;
            else if (ch == quote) state = State::code;
            else if (ch == '\n' && quote != '`') malformed = true;
        } else if (ch == '/' && (next == '/' || next == '*')) {
            state = next == '/' ? State::line_comment : State::block_comment;
            result.bytes[i] = result.bytes[i + 1] = Color::GRAY;
            ++i;
        } else if (ch == '\'' || ch == '"' || ch == '`') {
            state = State::string;
            quote = ch;
            result.bytes[i] = Color::GREEN;
        } else if (ch == '(' || ch == '[' || ch == '{') brackets.push_back(ch);
        else if (ch == ')' || ch == ']' || ch == '}') {
            const char expected = ch == ')' ? '(' : ch == ']' ? '[' : '{';
            if (brackets.empty() || brackets.back() != expected) malformed = true;
            else brackets.pop_back();
        } else if (std::isalpha(static_cast<unsigned char>(ch)) || ch == '_' || ch == '$') {
            const auto start = i;
            while (i + 1 < input.size()) {
                const auto c = static_cast<unsigned char>(input[i + 1]);
                if (!std::isalnum(c) && c != '_' && c != '$') break;
                ++i;
            }
            if (keywords.find(" " + input.substr(start, i - start + 1) + " ") != std::string_view::npos)
                std::fill(result.bytes.begin() + start, result.bytes.begin() + i + 1, Color::BLUE);
        } else if (std::isdigit(static_cast<unsigned char>(ch))) result.bytes[i] = Color::YELLOW;
    }
    result.incomplete = !malformed && (!brackets.empty() || state == State::string || state == State::block_comment);
    return result;
}
} // namespace
bool needs_more_input(const std::string& input) { return scan(input).incomplete; }
void highlight(const std::string& input, replxx::Replxx::colors_t& colors) {
    const auto scanned = scan(input);
    std::fill(colors.begin(), colors.end(), replxx::Replxx::Color::DEFAULT);
    std::size_t point = 0;
    for (std::size_t byte = 0; byte < input.size(); ++byte) {
        if ((static_cast<unsigned char>(input[byte]) & 0xc0) == 0x80) continue;
        if (point < colors.size()) colors[point++] = scanned.bytes[byte];
    }
}
} // namespace dragscript::repl_detail
