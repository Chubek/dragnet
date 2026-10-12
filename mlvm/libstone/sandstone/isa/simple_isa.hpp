// Small, header-only .isa reader. Requires C++20 and DomDSL.hpp.
// Independent of SymAS databanks; see simple-isa/README.md for the format.
#pragma once

#include "DomDSL.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <optional>
#include <ranges>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace qarch::simple_isa {

struct ISAError : std::runtime_error { using std::runtime_error::runtime_error; };

struct Value {
    std::variant<std::string, std::int64_t, std::uint64_t> data;
    Value(const char *v) : data(std::string(v)) {}
    Value(std::string v) : data(std::move(v)) {}
    template <typename T> requires std::is_integral_v<T>
    Value(T v) : data([](T n) -> decltype(data) {
        if constexpr (std::is_signed_v<T>) return std::int64_t(n);
        else return std::uint64_t(n);
    }(v)) {}
};
using Operands = std::map<std::string, Value>;

struct Register { std::string name, regclass; unsigned width{}, number{}; };
struct Field { std::string source; unsigned low{}, bits{}, source_low{}; };
struct MemRef {
    unsigned mod{}, rm{};
    std::optional<unsigned> base, index, scale;
    bool sib{};
    unsigned disp_bytes{};
    std::int64_t disp{};
};
enum class Width { bits16, bits32, bits48, bits64, variable, stream, text };
struct Encoding {
    std::string name;
    Width width{};
    std::uint64_t base{};
    std::vector<Field> fields;
    std::map<std::string, std::string> directives;
};
struct Operation {
    std::string name, category, encoding, syntax;
    std::vector<std::pair<std::string, std::string>> operands;
    std::vector<std::string> stack_operands{};
};

namespace detail {
inline std::string lower(std::string s) {
    for (char &c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
inline std::uint64_t number(std::string_view s) {
    if (s.empty() || !std::isdigit(static_cast<unsigned char>(s.front())))
        throw ISAError("expected unsigned number: " + std::string(s));
    std::size_t pos = 0;
    std::uint64_t n{};
    const bool hex = s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X');
    try { n = std::stoull(std::string(s), &pos, hex ? 16 : 10); }
    catch (const std::exception &) { throw ISAError("invalid number: " + std::string(s)); }
    if (pos != s.size()) throw ISAError("invalid number: " + std::string(s));
    return n;
}
inline unsigned small(std::string_view s) {
    auto n = number(s);
    if (n > std::numeric_limits<unsigned>::max()) throw ISAError("number too large: " + std::string(s));
    return static_cast<unsigned>(n);
}
inline std::uint64_t mask(unsigned n) {
    return n == 64 ? ~std::uint64_t{0} : ((std::uint64_t{1} << n) - 1);
}
inline std::string directive(const Encoding &e, const std::string &key, std::string def = {}) {
    auto it = e.directives.find(key);
    return it == e.directives.end() ? def : it->second;
}
inline std::string trim(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
    return std::string(s);
}
inline std::vector<std::string> split(std::string_view s, char separator) {
    std::vector<std::string> out;
    std::size_t start = 0;
    while (true) {
        auto at = s.find(separator, start);
        if (at == std::string_view::npos) { out.push_back(trim(s.substr(start))); return out; }
        out.push_back(trim(s.substr(start, at - start)));
        start = at + 1;
    }
}
// Variable-width encodings spell a trailing immediate as imm8/imm16/imm32/imm64.
inline unsigned immediate_size(const std::string &key) {
    if (!key.starts_with("imm")) return 0;
    auto rest = key.substr(3);
    if (rest == "8") return 1;
    if (rest == "16") return 2;
    if (rest == "32" || rest == "64") return rest == "32" ? 4 : 8;
    return 0;
}
inline unsigned index_of(unsigned power_of_two) {
    unsigned n = 0;
    while ((1u << n) != power_of_two) ++n;
    return n;
}
inline std::string kind_of(const Operation &op, const std::string &operand) {
    for (const auto &[key, kind] : op.operands) if (key == operand) return kind;
    return {};
}
inline std::uint64_t bits(const Value &v) {
    if (auto p = std::get_if<std::uint64_t>(&v.data)) return *p;
    if (auto p = std::get_if<std::int64_t>(&v.data)) return static_cast<std::uint64_t>(*p);
    throw ISAError("numeric operand required");
}
inline bool negative(const Value &v) {
    if (auto p = std::get_if<std::int64_t>(&v.data)) return *p < 0;
    return false;
}
inline void check_fit(const Value &v, unsigned width, bool is_signed, std::string_view label) {
    if (!width || width > 64) throw ISAError("invalid immediate width");
    const auto n = bits(v);
    bool ok = true;
    if (negative(v)) {
        ok = is_signed && (width == 64 ||
             std::get<std::int64_t>(v.data) >= -(std::int64_t{1} << (width - 1)));
    } else if (is_signed) {
        ok = width == 64 ? n <= static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())
                         : n < (std::uint64_t{1} << (width - 1));
    } else {
        ok = width == 64 || n <= mask(width);
    }
    if (!ok) throw ISAError(std::string(label) + " does not fit " + std::to_string(width) + " bits");
}
inline void append(std::vector<std::uint8_t> &out, std::uint64_t n, unsigned bytes, bool big) {
    for (unsigned i = 0; i < bytes; ++i) {
        unsigned shift = (big ? bytes - 1 - i : i) * 8;
        out.push_back(static_cast<std::uint8_t>(n >> shift));
    }
}
struct Token { std::string text; std::size_t offset{}; bool quoted{}; };
inline bool ident_start(char c) {
    return std::isalpha(static_cast<unsigned char>(c)) || c == '_';
}
inline bool ident_next(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '.';
}

// DomDSL's ParsecInput owns the cursor. Every token is parsed through its
// ExpectedResult/Parser API, so lexer diagnostics keep source offsets.
inline dsl::Parser<Token> token_parser() {
    return dsl::parser([](dsl::ParsecInput &in) -> dsl::ExpectedResult<Token> {
        while (!in.eof()) {
            if (std::isspace(static_cast<unsigned char>(in.peek()))) { in.consume(); continue; }
            if (in.peek() == '#') {
                while (!in.eof() && in.consume() != '\n') {}
                continue;
            }
            break;
        }
        if (in.eof()) return dsl::fail_expected<Token>(in, "token");
        const auto start = in.pos;
        const char c = in.consume();
        if (c == '"') {
            std::string content;
            while (!in.eof()) {
                const char ch = in.consume();
                if (ch == '"') return Token{std::move(content), start, true};
                if (ch == '\\' && !in.eof()) {
                    const char next = in.consume();
                    if (next == '"' || next == '\\') content += next;
                    else { content += '\\'; content += next; }
                } else content += ch;
            }
            return dsl::fail_expected<Token>(in, "closing quote", dsl::ParseFailureKind::Committed);
        }
        if (ident_start(c) || std::isdigit(static_cast<unsigned char>(c))) {
            const bool numeric = std::isdigit(static_cast<unsigned char>(c));
            while (!in.eof() && (numeric ? std::isalnum(static_cast<unsigned char>(in.peek()))
                                         : ident_next(in.peek()))) in.consume();
            return Token{std::string(in.get_span(start)), start, false};
        }
        if (std::string_view("{}[]=;,:()").find(c) != std::string_view::npos)
            return Token{std::string(1, c), start, false};
        return dsl::fail_expected<Token>(in, "ISA punctuation", dsl::ParseFailureKind::Committed);
    });
}
inline std::vector<Token> lex(std::string_view source) {
    const auto one = token_parser();
    auto all = dsl::parser([one](dsl::ParsecInput &in) -> dsl::ExpectedResult<std::vector<Token>> {
        std::vector<Token> result;
        while (true) {
            auto next = one(in);
            if (next) { result.push_back(std::move(*next)); continue; }
            if (next.error.kind == dsl::ParseFailureKind::Committed)
                return dsl::ExpectedResult<std::vector<Token>>::failure(
                    next.error.pos, next.error.kind, next.error.expected);
            if (!in.eof()) return dsl::ExpectedResult<std::vector<Token>>::failure(in.pos,
                    dsl::ParseFailureKind::Committed, {"token"});
            return result;
        }
    });
    auto result = dsl::run_parser(all, source);
    if (!result.value) {
        const auto n = std::min(result.error.pos, source.size());
        const auto line = std::count(source.begin(), source.begin() + n, '\n') + 1;
        throw ISAError("line " + std::to_string(line) + ": invalid ISA token");
    }
    return std::move(*result.value);
}
} // namespace detail

struct ISA {
    std::string name, endian;
    unsigned word_size{}, addr_size{}, align{};
    std::map<std::string, Register> registers;
    std::map<std::string, std::string> aliases;
    std::map<std::string, Encoding> encodings;
    std::map<std::string, Operation> operations;

    const Register &reg(std::string name) const {
        name = detail::lower(std::move(name));
        if (auto it = aliases.find(name); it != aliases.end()) name = it->second;
        auto it = registers.find(name);
        if (it == registers.end()) throw ISAError(this->name + ": unknown register '" + name + "'");
        return it->second;
    }
    const Operation &operation(std::string name) const {
        name = detail::lower(std::move(name));
        auto it = operations.find(name);
        if (it == operations.end()) throw ISAError(this->name + ": unknown operation '" + name + "'");
        return it->second;
    }
    std::string render(std::string name, const Operands &arguments = {}) const;
    std::vector<std::uint8_t> encode(std::string name, const Operands &arguments = {}) const;

private:
    std::map<std::string, Value> values(const Operation &, const Operands &) const;
public:
    MemRef memory(const Operation &, const std::string &operand, const std::string &text) const;
private:
    std::vector<std::uint8_t> encode_stream(const Operation &, const Encoding &,
                                            const std::map<std::string, Value> &) const;
    std::vector<std::uint8_t> encode_x86(const Operation &, const Encoding &,
                                         const std::map<std::string, Value> &) const;
};

// Decode an Intel-order "[base+index*scale+disp]" reference into ModR/M, SIB
// and displacement fields.
inline MemRef ISA::memory(const Operation &op, const std::string &operand,
                          const std::string &text) const {
    auto fail = [&](const std::string &why) { throw ISAError(op.name + "." + operand + ": " + why); };
    if (text.size() < 2 || text.back() != ']' || text.find(']', 0) != text.size() - 1)
        fail("unbalanced memory reference " + text);
    MemRef out;
    bool rip = false;
    std::int64_t sign = 1;
    std::string body = text.substr(1, text.size() - 2);
    std::size_t pos = 0;
    while (pos < body.size()) {
        while (pos < body.size() && std::isspace(static_cast<unsigned char>(body[pos]))) ++pos;
        if (pos < body.size() && (body[pos] == '+' || body[pos] == '-')) {
            sign = body[pos] == '-' ? -1 : 1;
            ++pos;
        }
        auto start = pos;
        while (pos < body.size() && !std::isspace(static_cast<unsigned char>(body[pos])) &&
               body[pos] != '+' && body[pos] != '-') ++pos;
        auto term = body.substr(start, pos - start);
        if (term.empty()) {
            if (pos >= body.size()) fail("dangling sign in " + text);
            continue;
        }
        auto stars = term.find('*');
        auto name = stars == std::string::npos ? term : term.substr(0, stars);
        auto scale_text = stars == std::string::npos ? std::string() : term.substr(stars + 1);
        bool numeric = !name.empty() && (std::isdigit(static_cast<unsigned char>(name[0])) ||
                                         name[0] == '-');
        if (numeric) {
            std::size_t used = 0;
            std::int64_t value = 0;
            try { value = std::stoll(name, &used, 0); } catch (...) { fail("bad term " + term); }
            if (used != name.size()) fail("bad term " + term);
            out.disp += sign * value;
        } else if (detail::lower(name) == "rip") {
            if (sign < 0 || !scale_text.empty() || out.base || out.index) fail("bad rip base");
            rip = true;
        } else {
            const auto &entry = reg(name);
            if (entry.regclass != "GPR" || entry.number > 15)
                fail("address registers must be low GPRs");
            auto number = entry.number;
            if (!scale_text.empty()) {
                std::size_t used = 0;
                unsigned factor = 0;
                try { factor = std::stoul(scale_text, &used); } catch (...) { fail("bad scale"); }
                if (used != scale_text.size() || (factor != 1 && factor != 2 && factor != 4 &&
                                                  factor != 8) || out.index || (number & 7) == 4)
                    fail("the index needs a low GPR other than rsp");
                out.index = number;
                out.scale = factor;
            } else if (!out.base) {
                out.base = number;
            } else if (!out.index) {
                out.index = number;
                out.scale = 1;
            } else fail("too many address registers");
        }
        sign = 1;
    }
    if (out.disp < -(std::int64_t{1} << 31) || out.disp >= (std::int64_t{1} << 31))
        fail("displacement out of range");
    if (rip) {
        out.mod = 0;
        out.rm = 5;
        out.disp_bytes = 4;
        return out;
    }
    if (out.scale && *out.scale != 1 && *out.scale != 2 && *out.scale != 4 && *out.scale != 8)
        fail("scale must be 1, 2, 4 or 8");
    // A scaled index and rsp or r12 need a SIB byte, and rbp or r13 as the only
    // register always needs a displacement.
    if (!out.base && !out.index) fail("a 64-bit address needs a base, an index or rip");
    const bool sib = out.index || ((*out.base & 7) == 4);
    if (!out.base) { out.mod = 0; out.rm = 4; out.disp_bytes = 4; }
    else if (out.disp == 0 && (*out.base & 7) != 5) {
        out.mod = 0; out.rm = sib ? 4u : *out.base;
    } else if (out.disp >= -128 && out.disp < 128) {
        out.mod = 1; out.rm = sib ? 4u : *out.base; out.disp_bytes = 1;
    } else {
        out.mod = 2; out.rm = sib ? 4u : *out.base; out.disp_bytes = 4;
    }
    out.scale = out.scale ? detail::index_of(*out.scale) : 0u;
    out.sib = sib;
    return out;
}

inline std::map<std::string, Value> ISA::values(const Operation &op, const Operands &arguments) const {
    if (arguments.size() != op.operands.size())
        throw ISAError(op.name + ": wrong operand count");
    std::map<std::string, Value> out;
    for (const auto &[key, kind] : op.operands) {
        auto it = arguments.find(key);
        if (it == arguments.end()) throw ISAError(op.name + ": missing operand " + key);
        const auto &arg = it->second;
        if (kind == "GPR" || kind == "FPR") {
            const auto *text = std::get_if<std::string>(&arg.data);
            if (!text) throw ISAError(op.name + "." + key + ": register name required");
            const Register &r = reg(*text);
            if (r.regclass != kind) throw ISAError(op.name + "." + key + ": expected " + kind);
            if (this->name == "aarch64" && r.number == 31) {
                bool sp = std::ranges::find(op.stack_operands, key) != op.stack_operands.end();
                if ((r.name == "sp") != sp)
                    throw ISAError(op.name + "." + key + ": register 31 must be " + (sp ? "sp" : "xzr"));
            }
            if (this->name == "m68k" && (op.name == "move_l" || op.name == "add_l" || op.name == "sub_l") &&
                r.name[0] != 'd') throw ISAError(op.name + ": data register required");
            if (this->name == "tricore" && (op.name == "add" || op.name == "sub" || op.name == "mov") &&
                r.name[0] != 'd') throw ISAError(op.name + ": data register required");
            if (this->name == "nvptx" && op.name.ends_with("_u32") && r.width != 32)
                throw ISAError(op.name + ": 32-bit PTX register required");
            out.emplace(key, Value(std::uint64_t(r.number)));
        } else if (kind == "mem") {
            const auto *text = std::get_if<std::string>(&arg.data);
            if (!text || text->empty() || text->front() != '[')
                throw ISAError(op.name + "." + key + ": memory reference required");
            memory(op, key, *text);
            out.emplace(key, arg);
        } else if (kind == "imm" || kind == "rel") {
            if (std::holds_alternative<std::string>(arg.data))
                throw ISAError(op.name + "." + key + ": integer required");
            out.emplace(key, arg);
        } else throw ISAError(op.name + ": invalid operand type " + kind);
    }
    return out;
}
inline std::string ISA::render(std::string name, const Operands &arguments) const {
    const auto &op = operation(std::move(name));
    auto normalized = values(op, arguments);
    std::string out;
    for (std::size_t i = 0; i < op.syntax.size();) {
        char c = op.syntax[i++];
        if (c == '{' && i < op.syntax.size() && op.syntax[i] == '{') { out += '{'; ++i; continue; }
        if (c == '}' && i < op.syntax.size() && op.syntax[i] == '}') { out += '}'; ++i; continue; }
        if (c != '{') { out += c; continue; }
        const auto end = op.syntax.find('}', i);
        if (end == std::string::npos) throw ISAError(op.name + ": unclosed placeholder");
        auto key = op.syntax.substr(i, end - i);
        auto original = arguments.find(key);
        if (original == arguments.end()) throw ISAError(op.name + ": unknown placeholder " + key);
        const Value &v = (this->name == "ppc32" || this->name == "ppc64") &&
                         std::holds_alternative<std::string>(original->second.data)
                             ? normalized.at(key) : original->second;
        if (const auto *s = std::get_if<std::string>(&v.data)) out += *s;
        else if (const auto *n = std::get_if<std::int64_t>(&v.data)) out += std::to_string(*n);
        else out += std::to_string(std::get<std::uint64_t>(v.data));
        i = end + 1;
    }
    return out;
}
inline std::vector<std::uint8_t> ISA::encode(std::string name, const Operands &arguments) const {
    const auto &op = operation(std::move(name));
    auto vals = values(op, arguments);
    const auto &enc = encodings.at(op.encoding);
    if (enc.width == Width::text) throw ISAError(this->name + ": " + op.name + " is text-only; use render()");
    if (enc.width == Width::stream) return encode_stream(op, enc, vals);
    if (enc.width == Width::variable) return encode_x86(op, enc, vals);
    unsigned width = enc.width == Width::bits16 ? 16 : enc.width == Width::bits32 ? 32
                   : enc.width == Width::bits48 ? 48 : 64;
    std::uint64_t word = enc.base;
    std::map<std::string, unsigned> consumed;
    for (const auto &f : enc.fields) {
        auto it = vals.find(f.source);
        if (it == vals.end()) throw ISAError(op.name + ": missing encoding operand " + f.source);
        consumed[f.source] = std::max(consumed[f.source], f.source_low + f.bits);
        word |= ((detail::bits(it->second) >> f.source_low) & detail::mask(f.bits)) << f.low;
    }
    for (const auto &[key, kind] : op.operands) {
        if ((kind == "imm" || kind == "rel") && consumed.contains(key))
            detail::check_fit(vals.at(key), consumed.at(key), detail::directive(enc, "signed") == "1",
                              op.name + "." + key);
        if (kind == "imm" || kind == "rel") {
            auto align_str = detail::directive(enc, "alignment");
            if (!align_str.empty() && detail::bits(vals.at(key)) % detail::number(align_str))
                throw ISAError(op.name + "." + key + ": unaligned displacement");
        }
    }
    std::vector<std::uint8_t> result;
    detail::append(result, word, width / 8, endian == "big");
    return result;
}
inline std::vector<std::uint8_t> ISA::encode_stream(
    const Operation &op, const Encoding &enc, const std::map<std::string, Value> &vals) const {
    std::vector<std::uint8_t> out;
    std::string seq = detail::directive(enc, "sequence");
    unsigned limit = detail::small(detail::directive(enc, "leb_bits", "64"));
    std::size_t i = 0;
    while (i < seq.size()) {
        const auto open = seq.find('(', i), close = seq.find(')', open);
        if (open == std::string::npos || close == std::string::npos)
            throw ISAError(enc.name + ": invalid stream sequence");
        const auto kind = seq.substr(i, open - i), arg = seq.substr(open + 1, close - open - 1);
        Value v = vals.contains(arg) ? vals.at(arg) : Value(detail::number(arg));
        const auto n = detail::bits(v);
        if (kind == "byte" || kind == "word") {
            const unsigned size = kind == "byte" ? 1 : 4;
            detail::check_fit(v, size * 8, false, op.name + "." + arg);
            detail::append(out, n, size, endian == "big");
        } else if (kind == "uleb") {
            detail::check_fit(v, limit, false, op.name + "." + arg);
            auto x = n;
            do {
                std::uint8_t b = static_cast<std::uint8_t>(x & 127u);
                x >>= 7;
                out.push_back(b | (x ? 128u : 0u));
            } while (x);
        } else if (kind == "sleb") {
            detail::check_fit(v, limit, true, op.name + "." + arg);
            auto x = static_cast<std::int64_t>(n);
            while (true) {
                std::uint8_t b = static_cast<std::uint8_t>(x & 127);
                // Floor division by 128, independent of implementation-defined signed shifts.
                x = x / 128 - (x < 0 && x % 128 != 0 ? 1 : 0);
                bool more = !((x == 0 && (b & 64u) == 0) || (x == -1 && (b & 64u) != 0));
                out.push_back(b | (more ? 128u : 0u));
                if (!more) break;
            }
        } else throw ISAError(enc.name + ": unsupported stream kind");
        i = close + 1;
        if (i < seq.size()) {
            if (seq[i] != ',') throw ISAError(enc.name + ": missing sequence separator");
            ++i;
        }
    }
    return out;
}
inline std::vector<std::uint8_t> ISA::encode_x86(
    const Operation &op, const Encoding &enc, const std::map<std::string, Value> &vals) const {
    if (this->name != "amd64") throw ISAError("variable encoding is only defined for amd64");
    std::vector<std::uint8_t> out;
    auto modrm = detail::directive(enc, "modrm");
    std::uint64_t reg = 0, rm = 0;
    std::optional<MemRef> mem;
    if (!modrm.empty()) {
        auto comma = modrm.find(',');
        if (comma == std::string::npos) throw ISAError(enc.name + ": invalid modrm");
        auto reg_name = detail::trim(modrm.substr(0, comma));
        auto rm_name = detail::trim(modrm.substr(comma + 1));
        reg = detail::bits(vals.at(reg_name));
        if (detail::kind_of(op, reg_name) == "imm" && reg > 7)
            throw ISAError(op.name + ": ModR/M opcode selector must be in 0..7");
        if (detail::kind_of(op, rm_name) == "mem")
            mem = memory(op, rm_name, std::get<std::string>(vals.at(rm_name).data)), rm = mem->rm;
        else rm = detail::bits(vals.at(rm_name));
    }
    auto opreg_name = detail::directive(enc, "opreg");
    std::uint64_t opreg = opreg_name.empty() ? 0 : detail::bits(vals.at(opreg_name));
    const auto rex_w = detail::directive(enc, "rex_w");
    unsigned rex = (rex_w == "1") ? 8u : 0u;
    if (reg >= 8) rex |= 1u << 2;
    if (mem && mem->base && *mem->base >= 8) rex |= 1u;
    if (mem && mem->index && *mem->index >= 8) rex |= 1u << 1;
    if (!mem && rm >= 8) rex |= static_cast<unsigned>(rm >> 3);
    if (opreg >= 8) rex |= 1u;
    if (auto pref = detail::directive(enc, "prefix"); !pref.empty())
        for (auto byte : detail::split(pref, ','))
            out.push_back(static_cast<std::uint8_t>(detail::number(byte)));
    if (rex) out.push_back(static_cast<std::uint8_t>(0x40 | rex));
    std::uint64_t opcode = detail::number(detail::directive(enc, "opcode")) + (opreg & 7u);
    unsigned bytes = opcode > 0xffffff ? 4 : opcode > 0xffff ? 3 : opcode > 255 ? 2 : 1;
    detail::append(out, opcode, bytes, true);
    if (!modrm.empty()) {
        out.push_back(static_cast<std::uint8_t>((mem ? mem->mod << 6 : 0xc0u) | ((reg & 7) << 3) |
                                                (rm & 7)));
        if (mem) {
            if (mem->sib)
                out.push_back(static_cast<std::uint8_t>(*mem->scale << 6 |
                                                        ((mem->index ? *mem->index : 4u) << 3) |
                                                        ((mem->base ? *mem->base : 5u) & 7)));
            if (mem->disp_bytes == 1) out.push_back(static_cast<std::uint8_t>(mem->disp));
            else if (mem->disp_bytes == 4)
                detail::append(out, static_cast<std::uint64_t>(mem->disp), 4, false);
        }
    }
    for (const auto &[key, value] : enc.directives) {
        if (!detail::immediate_size(key)) continue;
        const auto size = detail::immediate_size(key);
        detail::check_fit(vals.at(value), size * 8, true, op.name + "." + value);
        detail::append(out, detail::bits(vals.at(value)), size, false);
    }
    auto rel = detail::directive(enc, "rel32");
    if (!rel.empty()) {
        detail::check_fit(vals.at(rel), 32, true, op.name + "." + rel);
        detail::append(out, detail::bits(vals.at(rel)), 4, false);
    }
    return out;
}

// A native, fluent facade over DomDSL's CRTP CombinatorParser feature. The file
// parser and C++ callers build the same checked ISA model through this class.
class SourceDSL : public dsl::DSL<SourceDSL, dsl::CombinatorParser> {
    ISA model_;
    bool has_arch_{};
public:
    SourceDSL &architecture(std::string name, unsigned word, unsigned address,
                            std::string endian, unsigned align) {
        if (has_arch_) throw ISAError("multiple arch blocks");
        if (name.empty() || !word || !address || !align || (endian != "big" && endian != "little"))
            throw ISAError("invalid arch declaration");
        model_.name = std::move(name);
        model_.word_size = word;
        model_.addr_size = address;
        model_.endian = std::move(endian);
        model_.align = align;
        has_arch_ = true;
        return *this;
    }
    SourceDSL &reg(std::string name, std::string regclass, unsigned width, unsigned number) {
        if (!has_arch_) throw ISAError("arch must precede registers");
        if ((regclass != "GPR" && regclass != "FPR") || !width || name.empty() ||
            !model_.registers.emplace(detail::lower(name), Register{name, regclass, width, number}).second)
            throw ISAError("invalid or duplicate register " + name);
        return *this;
    }
    SourceDSL &alias(std::string name, std::string target) {
        if (!has_arch_) throw ISAError("arch must precede aliases");
        if (!model_.aliases.emplace(detail::lower(name), detail::lower(target)).second)
            throw ISAError("duplicate alias " + name);
        return *this;
    }
    SourceDSL &encoding(Encoding e) {
        if (!has_arch_) throw ISAError("arch must precede encodings");
        auto name = e.name;
        if (name.empty() || !model_.encodings.emplace(name, std::move(e)).second)
            throw ISAError("duplicate encoding");
        return *this;
    }
    SourceDSL &form(Operation op) {
        if (!has_arch_) throw ISAError("arch must precede operations");
        auto key = detail::lower(op.name);
        if (key.empty() || !model_.encodings.contains(op.encoding) ||
            !model_.operations.emplace(key, std::move(op)).second)
            throw ISAError("invalid, duplicate, or unbound operation " + key);
        return *this;
    }
    ISA finish() {
        if (!has_arch_) throw ISAError("expected arch block first");
        for (const auto &[alias, target] : model_.aliases)
            if (model_.registers.contains(alias) || !model_.registers.contains(target))
                throw ISAError("invalid alias " + alias + " = " + target);
        for (const auto &[name, enc] : model_.encodings) {
            if (enc.width == Width::text) {
                if (enc.base || !enc.fields.empty() || !enc.directives.empty())
                    throw ISAError(name + ": text encoding cannot contain binary fields");
                continue;
            }
            if (enc.width == Width::stream) {
                if (enc.base || !enc.fields.empty() ||
                    !enc.directives.contains("sequence") || enc.directives.size() >
                    (enc.directives.contains("leb_bits") ? 2u : 1u))
                    throw ISAError(name + ": stream requires sequence and optional leb_bits");
                auto limit = detail::directive(enc, "leb_bits", "64");
                if (limit != "32" && limit != "64") throw ISAError(name + ": invalid leb_bits");
                std::string seq = enc.directives.at("sequence");
                std::size_t i = 0;
                if (seq.empty()) throw ISAError(name + ": empty stream sequence");
                while (i < seq.size()) {
                    auto a = seq.find('(', i), b = seq.find(')', a);
                    if (a == std::string::npos || b == std::string::npos || b == a + 1)
                        throw ISAError(name + ": invalid stream sequence");
                    auto type = seq.substr(i, a - i);
                    if (type != "byte" && type != "word" && type != "uleb" && type != "sleb")
                        throw ISAError(name + ": unknown sequence emission " + type);
                    i = b + 1;
                    if (i < seq.size() && seq[i++] != ',') throw ISAError(name + ": stream needs comma");
                    if (i == seq.size() && seq.back() == ',') throw ISAError(name + ": trailing comma");
                }
                continue;
            }
            if (enc.width == Width::variable) {
                if (model_.name != "amd64" || enc.base || !enc.fields.empty() ||
                    !enc.directives.contains("opcode"))
                    throw ISAError(name + ": variable encoding is only defined for amd64");
                for (const auto &[key, value] : enc.directives) {
                    if (key != "opcode" && key != "prefix" && key != "rex_w" && key != "modrm" &&
                        key != "opreg" && key != "rel32" && !detail::immediate_size(key))
                        throw ISAError(name + ": unknown directive " + key);
                    if (key == "prefix") {
                        if (value.empty()) throw ISAError(name + ": empty prefix");
                        for (auto byte : detail::split(value, ','))
                            if (detail::number(byte) > 255) throw ISAError(name + ": invalid prefix");
                    }
                    if (key == "rex_w" && value != "0" && value != "1") throw ISAError(name + ": invalid rex_w");
                    if (key.starts_with("imm") && !detail::immediate_size(key))
                        throw ISAError(name + ": invalid immediate size " + key);
                }
                if (detail::number(enc.directives.at("opcode")) > 0xffffffffull)
                    throw ISAError(name + ": opcode exceeds 4 bytes");
                continue;
            }
            unsigned width = enc.width == Width::bits16 ? 16 : enc.width == Width::bits32 ? 32
                           : enc.width == Width::bits48 ? 48 : 64;
            if (width != 64 && enc.base > detail::mask(width)) throw ISAError(name + ": base does not fit");
            std::uint64_t occupied = enc.base;
            for (const auto &f : enc.fields) {
                if (!f.bits || f.bits > 64 || f.source_low > 63 || f.source_low + f.bits > 64 ||
                    f.low + f.bits > width) throw ISAError(name + ": field exceeds width");
                auto mask = detail::mask(f.bits) << f.low;
                if (occupied & mask) throw ISAError(name + ": overlapping fields");
                occupied |= mask;
            }
            for (const auto &[key, value] : enc.directives) {
                if (key == "alignment") {
                    if (!detail::number(value)) throw ISAError(name + ": alignment must be positive");
                } else if ((key != "signed" && key != "unsigned") || (value != "0" && value != "1"))
                    throw ISAError(name + ": invalid directive " + key);
            }
            if (detail::directive(enc, "signed") == "1" && detail::directive(enc, "unsigned") == "1")
                throw ISAError(name + ": conflicting signedness");
        }
        for (const auto &[key, op] : model_.operations) {
            std::set<std::string> expected;
            for (const auto &[field, kind] : op.operands) {
                if (!expected.insert(field).second || (kind != "GPR" && kind != "FPR" &&
                    kind != "imm" && kind != "rel" && kind != "mem"))
                    throw ISAError(key + ": invalid operand " + field);
            }
            for (const auto &sp : op.stack_operands)
                if (!expected.contains(sp) || detail::kind_of(op, sp) != "GPR")
                    throw ISAError(key + ": sp names an unknown GPR operand " + sp);
            const auto &enc = model_.encodings.at(op.encoding);
            for (const auto &f : enc.fields)
                if (!expected.contains(f.source)) throw ISAError(key + ": unknown encoding field " + f.source);
            if (enc.width == Width::variable) {
                for (const char *directive_name : {"modrm", "opreg", "rel32"}) {
                    auto it = enc.directives.find(directive_name);
                    if (it == enc.directives.end()) continue;
                    std::string refs = it->second;
                    std::size_t start = 0, count = 0;
                    while (true) {
                        auto comma = refs.find(',', start);
                        auto arg = detail::trim(refs.substr(start, comma == std::string::npos ? comma
                                                                                             : comma - start));
                        if (!expected.contains(arg)) throw ISAError(key + ": unknown " + directive_name + " operand " + arg);
                        ++count;
                        if (comma == std::string::npos) break;
                        start = comma + 1;
                    }
                    if (count != (std::string_view(directive_name) == "modrm" ? 2u : 1u))
                        throw ISAError(key + ": wrong operand count in " + directive_name);
                }
                for (const auto &[directive, target] : enc.directives)
                    if (detail::immediate_size(directive) && !expected.contains(detail::trim(target)))
                        throw ISAError(key + ": " + directive + " names an unknown operand");
                if (enc.directives.contains("modrm")) {
                    auto refs = detail::split(enc.directives.at("modrm"), ',');
                    if (refs.size() != 2 || !expected.contains(refs[0]) || !expected.contains(refs[1]) ||
                        (detail::kind_of(op, refs[0]) != "GPR" && detail::kind_of(op, refs[0]) != "FPR" &&
                         detail::kind_of(op, refs[0]) != "imm") ||
                        (detail::kind_of(op, refs[1]) != "GPR" && detail::kind_of(op, refs[1]) != "FPR" &&
                         detail::kind_of(op, refs[1]) != "mem"))
                        throw ISAError(key + ": modrm requires a register/selector and a register or mem");
                }
            }
            if (enc.width == Width::stream) {
                const auto &seq = enc.directives.at("sequence");
                std::size_t pos = 0;
                while (pos < seq.size()) {
                    const auto a = seq.find('(', pos), b = seq.find(')', a);
                    auto value = seq.substr(a + 1, b - a - 1);
                    if (!(std::isdigit(static_cast<unsigned char>(value[0])) || expected.contains(value)))
                        throw ISAError(key + ": unknown stream operand " + value);
                    if (std::isdigit(static_cast<unsigned char>(value[0]))) detail::number(value);
                    pos = b + 1;
                    if (pos < seq.size()) ++pos;
                }
            }
            std::set<std::string> seen;
            for (std::size_t i = 0; i < op.syntax.size(); ++i) {
                if (op.syntax[i] != '{') continue;
                if (i + 1 < op.syntax.size() && op.syntax[i + 1] == '{') { ++i; continue; }
                auto b = op.syntax.find('}', i + 1);
                if (b == std::string::npos) throw ISAError(key + ": unterminated placeholder");
                seen.insert(op.syntax.substr(i + 1, b - i - 1)); i = b;
            }
            if (seen != expected) throw ISAError(key + ": syntax placeholders must match operands");
        }
        return std::move(model_);
    }
};

namespace detail {
struct Cursor {
    const std::vector<Token> &tokens;
    std::size_t index{};
    bool eof() const { return index >= tokens.size(); }
    const Token &peek() const {
        if (eof()) throw ISAError("unexpected end of ISA file");
        return tokens[index];
    }
    bool at(std::string_view word) const { return !eof() && peek().text == word && !peek().quoted; }
    Token take() { auto t = peek(); ++index; return t; }
    bool consume(std::string_view word) { if (!at(word)) return false; ++index; return true; }
    void expect(std::string_view word) {
        if (!consume(word)) throw ISAError("expected '" + std::string(word) + "' at token " + std::to_string(index));
    }
    std::string identifier() {
        auto t = take();
        if (t.quoted || t.text.empty() || !ident_start(t.text[0]))
            throw ISAError("expected identifier at token " + std::to_string(index - 1));
        return t.text;
    }
    using Assignment = std::pair<std::string, std::vector<Token>>;
    Assignment assignment() {
        std::string key;
        while (!at("=")) {
            if (eof() || at(";") || at("}")) throw ISAError("invalid assignment at token " + std::to_string(index));
            key += take().text;
        }
        expect("=");
        std::vector<Token> value;
        while (!at(";")) {
            if (eof() || at("}")) throw ISAError("missing semicolon at token " + std::to_string(index));
            value.push_back(take());
        }
        expect(";");
        if (key.empty()) throw ISAError("empty assignment key");
        return {key, std::move(value)};
    }
    std::map<std::string, std::vector<Token>> block() {
        expect("{");
        std::map<std::string, std::vector<Token>> props;
        while (!at("}")) {
            auto [key, value] = assignment();
            if (!props.emplace(key, std::move(value)).second) throw ISAError("duplicate property " + key);
        }
        expect("}");
        return props;
    }
};
inline std::string compact(const std::vector<Token> &tokens) {
    std::string out;
    for (const auto &t : tokens) out += t.text;
    return out;
}
inline std::string get(std::map<std::string, std::vector<Token>> &props, const std::string &key) {
    auto it = props.find(key);
    if (it == props.end()) throw ISAError("missing property " + key);
    auto value = compact(it->second);
    props.erase(it);
    return value;
}
inline void empty(const std::map<std::string, std::vector<Token>> &props, const std::string &name) {
    if (!props.empty()) throw ISAError(name + ": unknown property " + props.begin()->first);
}
} // namespace detail

inline ISA parse(std::string_view source) {
    auto tokens = detail::lex(source);
    detail::Cursor c{tokens};
    SourceDSL builder;
    c.expect("arch");
    auto arch = c.identifier();
    auto ap = c.block();
    auto word = detail::small(detail::get(ap, "word_size"));
    auto addr = detail::small(detail::get(ap, "addr_size"));
    auto endian = detail::get(ap, "endian");
    auto alignment = detail::small(detail::get(ap, "align"));
    detail::empty(ap, arch);
    builder.architecture(arch, word, addr, endian, alignment);
    while (!c.eof()) {
        if (c.consume("regclass")) {
            auto cls = c.identifier();
            c.expect("{");
            while (!c.at("}")) {
                auto name = c.identifier();
                c.expect("(");
                auto width = detail::small(c.take().text);
                c.expect(")");
                c.expect("=");
                auto number = detail::small(c.take().text);
                builder.reg(name, cls, width, number);
                if (!c.at("}")) c.expect(",");
            }
            c.expect("}");
        } else if (c.consume("alias")) {
            auto name = c.identifier();
            c.expect("=");
            auto target = c.identifier();
            c.expect(";");
            builder.alias(name, target);
        } else if (c.consume("encoding")) {
            Encoding e;
            e.name = c.identifier();
            auto props = c.block();
            auto width = detail::get(props, "width");
            if (width == "16") e.width = Width::bits16;
            else if (width == "32") e.width = Width::bits32;
            else if (width == "48") e.width = Width::bits48;
            else if (width == "64") e.width = Width::bits64;
            else if (width == "variable") e.width = Width::variable;
            else if (width == "stream") e.width = Width::stream;
            else if (width == "text") e.width = Width::text;
            else throw ISAError(e.name + ": unsupported width " + width);
            if (props.contains("base")) e.base = detail::number(detail::get(props, "base"));
            for (const auto &[key, tokens] : props) {
                auto value = detail::compact(tokens);
                auto colon = value.find(':');
                if (colon != std::string::npos &&
                    key != "sequence" && key != "modrm" && key != "alignment") {
                    Field f;
                    auto open = key.find('[');
                    f.source = key.substr(0, open);
                    if (open != std::string::npos) {
                        auto inside_colon = key.find(':', open), close = key.find(']', open);
                        if (inside_colon == std::string::npos || close != key.size() - 1 ||
                            inside_colon >= close) throw ISAError(e.name + ": invalid slice " + key);
                        auto hi = detail::small(std::string_view(key).substr(open + 1, inside_colon - open - 1));
                        f.source_low = detail::small(std::string_view(key).substr(inside_colon + 1, close - inside_colon - 1));
                        if (hi < f.source_low) throw ISAError(e.name + ": inverted slice " + key);
                        f.bits = hi - f.source_low + 1;
                    }
                    f.low = detail::small(std::string_view(value).substr(0, colon));
                    auto width_value = detail::small(std::string_view(value).substr(colon + 1));
                    if (open == std::string::npos) f.bits = width_value;
                    else if (f.bits != width_value) throw ISAError(e.name + ": slice width mismatch " + key);
                    if (f.source.empty()) throw ISAError(e.name + ": empty field name");
                    e.fields.push_back(std::move(f));
                } else {
                    if (key.find('[') != std::string::npos || value.empty())
                        throw ISAError(e.name + ": invalid directive " + key);
                    e.directives.emplace(key, std::move(value));
                }
            }
            builder.encoding(std::move(e));
        } else if (c.consume("op")) {
            Operation op;
            op.name = c.identifier();
            auto props = c.block();
            op.category = detail::get(props, "class");
            op.encoding = detail::get(props, "encoding");
            auto args = detail::get(props, "operands");
            auto syntax = props.find("syntax");
            if (syntax == props.end() || syntax->second.size() != 1 || !syntax->second[0].quoted)
                throw ISAError(op.name + ": syntax must be a quoted string");
            op.syntax = detail::get(props, "syntax");
            if (props.contains("sp"))
                op.stack_operands = detail::split(detail::get(props, "sp"), ',');
            detail::empty(props, op.name);
            std::size_t start = 0;
            while (start < args.size()) {
                auto comma = args.find(',', start);
                auto item = args.substr(start, comma == std::string::npos ? comma : comma - start);
                auto colon = item.find(':');
                if (colon == std::string::npos || !colon || colon == item.size() - 1)
                    throw ISAError(op.name + ": invalid operand " + item);
                op.operands.emplace_back(detail::trim(item.substr(0, colon)),
                                         detail::trim(item.substr(colon + 1)));
                if (comma == std::string::npos) break;
                start = comma + 1;
                if (start == args.size()) throw ISAError(op.name + ": trailing operand comma");
            }
            builder.form(std::move(op));
        } else throw ISAError("unexpected token '" + c.peek().text + "' at token " + std::to_string(c.index));
    }
    return builder.finish();
}

inline ISA load_file(const std::filesystem::path &path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw ISAError("cannot open '" + path.string() + "'");
    std::ostringstream buf;
    buf << in.rdbuf();
    if (in.bad()) throw ISAError("cannot read '" + path.string() + "'");
    try { return parse(buf.str()); }
    catch (const ISAError &e) { throw ISAError(path.string() + ": " + e.what()); }
}

inline std::map<std::string, std::filesystem::path> available(
    const std::filesystem::path &directory = std::filesystem::path(__FILE__).parent_path() / "simple-isa") {
    std::map<std::string, std::filesystem::path> found;
    for (const auto &entry : std::filesystem::directory_iterator(directory)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".isa") continue;
        std::ifstream in(entry.path());
        std::string line;
        while (std::getline(in, line)) {
            const auto first = line.find_first_not_of(" \t\r");
            if (first == std::string::npos || line[first] == '#') continue;
            if (line.compare(first, 5, "arch ") == 0) found.emplace(entry.path().stem().string(), entry.path());
            break;
        }
    }
    return found;
}
inline ISA load_arch(const std::string &name,
    const std::filesystem::path &directory = std::filesystem::path(__FILE__).parent_path() / "simple-isa") {
    auto paths = available(directory);
    auto it = paths.find(name);
    if (it == paths.end()) throw ISAError("no populated simple ISA named '" + name + "'");
    return load_file(it->second);
}
} // namespace qarch::simple_isa
