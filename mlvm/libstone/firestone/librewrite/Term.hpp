#pragma once

#include <cctype>
#include <algorithm>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace firestone::rewrite {

struct Term {
    std::string op;
    std::vector<Term> children;
    Term() = default;
    Term(std::string op, std::vector<Term> children = {}) : op(std::move(op)), children(std::move(children)) {
        if (this->op.empty()) throw std::invalid_argument("term operator is empty");
    }
    friend bool operator==(const Term& a, const Term& b) { return a.op == b.op && a.children == b.children; }
    friend bool operator!=(const Term& a, const Term& b) { return !(a == b); }
    friend bool operator<(const Term& a, const Term& b) { return a.op != b.op ? a.op < b.op : a.children < b.children; }
    std::size_t size() const { std::size_t n = 1; for (const auto& c : children) n += c.size(); return n; }
    std::size_t depth() const {
        std::size_t deepest = 0;
        for (const auto& child : children) deepest = std::max(deepest, child.depth());
        return deepest + 1;
    }
};

struct TermHash {
    std::size_t operator()(const Term& term) const {
        auto hash = std::hash<std::string>{}(term.op);
        for (const auto& child : term.children) hash ^= (*this)(child) + 0x9e3779b9u + (hash << 6u) + (hash >> 2u);
        return hash;
    }
};

// Patterns use ?name leaves as variables. All other operators are literal.
using Pattern = Term;
using Substitution = std::map<std::string, Term>;
inline bool is_variable(const Term& term) { return !term.op.empty() && term.op.front() == '?'; }

inline void validate_term(const Term& term, bool pattern = false) {
    if (term.op.empty()) throw std::invalid_argument("term operator is empty");
    if (pattern && is_variable(term) && (term.op.size() == 1 || !term.children.empty()))
        throw std::invalid_argument("pattern variable must be a named leaf");
    for (const auto& c : term.children) validate_term(c, pattern);
}

inline std::set<std::string> variables(const Pattern& p) {
    validate_term(p, true);
    std::set<std::string> out;
    if (is_variable(p)) out.insert(p.op);
    for (const auto& c : p.children) { auto v = variables(c); out.insert(v.begin(), v.end()); }
    return out;
}

inline bool match_into(const Pattern& pattern, const Term& term, Substitution& substitution) {
    validate_term(pattern, true); validate_term(term);
    auto tentative = substitution;
    std::function<bool(const Pattern&, const Term&)> walk = [&](const Pattern& p, const Term& t) {
        if (is_variable(p)) {
            auto it = tentative.find(p.op);
            if (it != tentative.end()) return it->second == t;
            tentative.emplace(p.op, t); return true;
        }
        if (p.op != t.op || p.children.size() != t.children.size()) return false;
        for (std::size_t i = 0; i < p.children.size(); ++i) if (!walk(p.children[i], t.children[i])) return false;
        return true;
    };
    if (!walk(pattern, term)) return false;
    substitution = std::move(tentative); return true;
}

inline std::optional<Substitution> match(const Pattern& pattern, const Term& term) {
    validate_term(pattern, true); validate_term(term);
    Substitution substitution;
    if (!match_into(pattern, term, substitution)) return {};
    return substitution;
}

inline Term substitute(const Pattern& pattern, const Substitution& substitution) {
    validate_term(pattern, true);
    if (is_variable(pattern)) {
        auto it = substitution.find(pattern.op);
        if (it == substitution.end()) throw std::invalid_argument("unbound replacement variable: " + pattern.op);
        return it->second;
    }
    std::vector<Term> children;
    for (const auto& c : pattern.children) children.push_back(substitute(c, substitution));
    return {pattern.op, std::move(children)};
}

inline Term parse(const std::string& source) {
    struct Parser {
        const std::string& s;
        std::size_t pos = 0;
        void space() { while (pos < s.size() && std::isspace(static_cast<unsigned char>(s[pos]))) ++pos; }
        std::string atom() {
            space(); std::string out;
            if (pos == s.size()) throw std::invalid_argument("expected an atom at end of input");
            if (s[pos] == '"') {
                ++pos;
                while (pos < s.size() && s[pos] != '"') {
                    char c = s[pos++];
                    if (c == '\\') {
                        if (pos == s.size()) throw std::invalid_argument("unterminated escape");
                        c = s[pos++];
                        if (c == 'n') c = '\n'; else if (c == 't') c = '\t';
                        else if (c != '\\' && c != '"') throw std::invalid_argument("unsupported escape");
                    }
                    out += c;
                }
                if (pos == s.size()) throw std::invalid_argument("unterminated quoted atom");
                ++pos;
            } else {
                while (pos < s.size() && !std::isspace(static_cast<unsigned char>(s[pos])) && s[pos] != '(' && s[pos] != ')') {
                    if (s[pos] == '"') throw std::invalid_argument("quote inside unquoted atom");
                    out += s[pos++];
                }
            }
            if (out.empty()) throw std::invalid_argument("empty atom");
            return out;
        }
        Term term() {
            space();
            if (pos == s.size()) throw std::invalid_argument("expected a term");
            if (s[pos] != '(') return Term(atom());
            ++pos; auto op = atom(); std::vector<Term> children;
            while (true) {
                space();
                if (pos == s.size()) throw std::invalid_argument("unclosed term");
                if (s[pos] == ')') { ++pos; break; }
                children.push_back(term());
            }
            return {std::move(op), std::move(children)};
        }
    } parser{source};
    auto result = parser.term(); parser.space();
    if (parser.pos != source.size()) throw std::invalid_argument("trailing input after term");
    return result;
}

inline std::string to_string(const Term& term) {
    validate_term(term);
    auto quote = [](const std::string& atom) {
        bool quoted = false;
        for (char c : atom) if (std::isspace(static_cast<unsigned char>(c)) || c == '(' || c == ')' || c == '"' || c == '\\') quoted = true;
        if (!quoted) return atom;
        std::string out = "\"";
        for (char c : atom) {
            if (c == '\n') out += "\\n";
            else if (c == '\t') out += "\\t";
            else { if (c == '\\' || c == '"') out += '\\'; out += c; }
        }
        return out + '"';
    };
    auto out = quote(term.op);
    if (term.children.empty()) return out;
    out = '(' + out;
    for (const auto& c : term.children) out += ' ' + to_string(c);
    return out + ')';
}

} // namespace firestone::rewrite
