#include <mlvm/optilisp/sexpr.hpp>

extern "C" {
#include <sexp.h>
}
#include <algorithm>
#include <cctype>
#include <fstream>
#include <memory>
#include <mutex>
#include <stdexcept>

namespace mlvm::optilisp {
std::string Diagnostic::render() const {
    return location.file + ":" + std::to_string(location.line) + ":" +
           std::to_string(location.column) + ": error [" + code + "]: " + message;
}
bool Expr::operator==(const Expr& other) const {
    return kind == other.kind && value == other.value && items == other.items;
}
bool Expr::symbol(std::string_view text) const {
    return kind == ExprKind::symbol && value == text;
}
std::string Expr::print() const {
    if (kind == ExprKind::list) {
        std::string result = "(";
        for (std::size_t i = 0; i < items.size(); ++i) {
            if (i) result += ' ';
            result += items[i].print();
        }
        return result + ')';
    }
    if (kind != ExprKind::string) return value;
    std::string result = "\"";
    for (char c : value) {
        switch (c) {
        case '\n': result += "\\n"; break;
        case '\r': result += "\\r"; break;
        case '\t': result += "\\t"; break;
        case '\0': result += "\\0"; break;
        case '"': result += "\\\""; break;
        case '\\': result += "\\\\"; break;
        default: result += c; break;
        }
    }
    return result + '"';
}

namespace {
// sfsexp has global errno and allocator bookkeeping. All C calls, including
// destruction, are serialized even though this target disables its node caches.
std::mutex sfsexp_mutex;
struct Token {
    char type;
    std::size_t begin, end;
    Location location;
    std::string value;
};
bool whitespace(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v';
}
bool numeric(const std::string& text) {
    std::size_t i = 0, digits = 0;
    if (i < text.size() && (text[i] == '+' || text[i] == '-')) ++i;
    while (i < text.size() && text[i] >= '0' && text[i] <= '9') { ++i; ++digits; }
    if (i < text.size() && text[i] == '.') {
        ++i;
        while (i < text.size() && text[i] >= '0' && text[i] <= '9') { ++i; ++digits; }
    }
    if (!digits) return false;
    if (i < text.size() && (text[i] == 'e' || text[i] == 'E')) {
        ++i;
        if (i < text.size() && (text[i] == '+' || text[i] == '-')) ++i;
        const auto exponent = i;
        while (i < text.size() && text[i] >= '0' && text[i] <= '9') ++i;
        if (i == exponent) return false;
    }
    return i == text.size();
}
Expr convert(const sexp_t* node, const std::vector<Token>& tokens, std::size_t& cursor) {
    if (!node || cursor >= tokens.size()) throw std::runtime_error("sfsexp/token shape mismatch");
    const auto& token = tokens[cursor++];
    Expr result;
    result.location = token.location;
    if (node->ty == SEXP_LIST) {
        if (token.type != '(') throw std::runtime_error("sfsexp list shape mismatch");
        result.kind = ExprKind::list;
        for (const sexp_t* item = node->list; item; item = item->next)
            result.items.push_back(convert(item, tokens, cursor));
        if (cursor >= tokens.size() || tokens[cursor++].type != ')')
            throw std::runtime_error("sfsexp list terminator mismatch");
    } else {
        if (node->aty != SEXP_BASIC && node->aty != SEXP_DQUOTE)
            throw std::runtime_error("quoted or binary atoms are not Optilisp forms");
        if ((node->aty == SEXP_DQUOTE) != (token.type == 's'))
            throw std::runtime_error("sfsexp atom shape mismatch");
        result.value = token.value;
        result.kind = token.type == 's' ? ExprKind::string : token.value.starts_with(':') ? ExprKind::keyword
                    : token.value == "true" || token.value == "false" ? ExprKind::boolean
                    : token.value == "nil" ? ExprKind::nil : numeric(token.value) ? ExprKind::number
                    : ExprKind::symbol;
    }
    return result;
}
} // namespace

ParseResult parse(std::string_view source, std::string file, ParseLimits limits) {
    ParseResult result;
    Location location{std::move(file), 1, 1};
    auto error = [&](std::string code, std::string message, const Location& at) {
        result.diagnostics.push_back({std::move(code), std::move(message), at});
    };
    if (source.size() > limits.max_bytes) {
        error("optilisp.limit", "source exceeds byte limit", location); return result;
    }
    std::vector<Token> tokens;
    std::vector<std::pair<std::size_t, std::size_t>> forms;
    std::size_t offset = 0, depth = 0, form_start = 0, nodes = 0;
    auto advance = [&] {
        if (source[offset] == '\n') { ++location.line; location.column = 1; }
        else ++location.column;
        ++offset;
    };
    while (offset < source.size()) {
        char c = source[offset];
        if (whitespace(c)) { advance(); continue; }
        if (c == ';') { while (offset < source.size() && source[offset] != '\n') advance(); continue; }
        Token token{c, offset, offset, location, {}};
        if (c == '\0' || c == '\'' || c == '\\') {
            error("optilisp.syntax", "NUL, quoted forms, and atom escapes are unsupported", location); return result;
        }
        if (c == '(') {
            if (!depth) form_start = tokens.size();
            if (++depth > limits.max_depth) { error("optilisp.limit", "nesting exceeds depth limit", location); return result; }
            advance();
        } else if (c == ')') {
            if (!depth) { error("optilisp.syntax", "unexpected closing parenthesis", location); return result; }
            --depth; advance();
        } else if (c == '"') {
            token.type = 's'; advance();
            bool closed = false;
            while (offset < source.size()) {
                c = source[offset];
                if (c == '\0') { error("optilisp.syntax", "raw NUL in string", location); return result; }
                if (c == '"') { advance(); closed = true; break; }
                if (c == '\\') {
                    advance();
                    if (offset == source.size()) break;
                    c = source[offset];
                    switch (c) {
                    case 'n': token.value += '\n'; break;
                    case 'r': token.value += '\r'; break;
                    case 't': token.value += '\t'; break;
                    case '0': token.value += '\0'; break;
                    case '"': case '\\': token.value += c; break;
                    default: error("optilisp.syntax", "invalid string escape", location); return result;
                    }
                } else token.value += c;
                advance();
            }
            if (!closed) { error("optilisp.syntax", "unterminated string", token.location); return result; }
        } else {
            token.type = 'a';
            while (offset < source.size() && !whitespace(source[offset]) && source[offset] != '(' && source[offset] != ')' && source[offset] != ';') {
                c = source[offset];
                if (c == '"' || c == '\'' || c == '\\' || c == '\0') {
                    error("optilisp.syntax", "invalid character inside atom", location); return result;
                }
                token.value += c; advance();
            }
            if (token.value == ":" || token.value.starts_with("#b#")) {
                error("optilisp.syntax", "empty keyword or binary form", token.location); return result;
            }
        }
        token.end = offset;
        tokens.push_back(std::move(token));
        if (tokens.back().type != ')' && ++nodes > limits.max_nodes) {
            error("optilisp.limit", "node limit exceeded", location); return result;
        }
        if (!depth) {
            if (tokens.back().type != ')') form_start = tokens.size() - 1;
            forms.emplace_back(form_start, tokens.size());
        }
    }
    if (depth) { error("optilisp.syntax", "unterminated list", location); return result; }
    std::lock_guard lock(sfsexp_mutex);
    for (const auto& [first, end] : forms) {
        const auto begin = tokens[first].begin;
        std::string input(source.substr(begin, tokens[end - 1].end - begin));
        input += '\n'; // Ensure sfsexp terminates bare atoms.
        reset_sexp_errno();
        std::unique_ptr<sexp_t, decltype(&destroy_sexp)> tree(parse_sexp(input.data(), input.size()), destroy_sexp);
        if (!tree || sexp_errno != SEXP_ERR_OK) {
            error("optilisp.sfsexp", "sfsexp rejected expression (error " + std::to_string(sexp_errno) + ")", tokens[first].location);
            result.forms.clear(); return result;
        }
        std::size_t cursor = first;
        try {
            result.forms.push_back(convert(tree.get(), tokens, cursor));
            if (cursor != end) throw std::runtime_error("sfsexp did not consume the entire expression");
        } catch (const std::exception& failure) {
            error("optilisp.sfsexp", failure.what(), tokens[first].location);
            result.forms.clear(); return result;
        }
    }
    return result;
}

ParseResult parse_file(const std::string& path, ParseLimits limits) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) return {{}, {{"optilisp.io", "cannot open source file", {path, 1, 1}}}};
    std::string source;
    char buffer[4096];
    while (stream) {
        stream.read(buffer, sizeof(buffer));
        source.append(buffer, static_cast<std::size_t>(stream.gcount()));
        if (source.size() > limits.max_bytes) return {{}, {{"optilisp.limit", "source exceeds byte limit", {path, 1, 1}}}};
    }
    if (!stream.eof()) return {{}, {{"optilisp.io", "cannot read source file", {path, 1, 1}}}};
    return parse(source, path, limits);
}
} // namespace mlvm::optilisp
