// DomSymAsCC — minimal, dependency-free JSON model + parser.
//
// Used to carry the structured databank description emitted by the embedded
// DomMune (Lua) DSL across the process boundary into the C++ compiler. The
// schema is documented in `stdspec/README` and consumed by `spec_document`.
//
// The parser is intentionally small and strict: it rejects trailing garbage,
// malformed escapes, and out-of-range numbers rather than silently coercing.
#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace domsymas::cc::json {

enum class Type { Null, Bool, Number, String, Array, Object };

struct Value;
using Array = std::vector<Value>;
using Object = std::vector<std::pair<std::string, Value>>;

struct Value {
    Type type = Type::Null;
    bool boolean = false;
    double number = 0.0;      // lossless for integers up to 2^53
    int64_t integer = 0;      // preferred when the literal is integral
    bool integral = false;
    std::string string;
    std::shared_ptr<Array> array;
    std::shared_ptr<Object> object;

    static Value null() { return {}; }
    static Value boolean_value(bool b) { Value v; v.type = Type::Bool; v.boolean = b; return v; }
    static Value number_value(double d) {
        Value v; v.type = Type::Number; v.number = d;
        v.integral = (d == (double)(int64_t)d);
        v.integer = v.integral ? (int64_t)d : 0;
        return v;
    }
    static Value integer_value(int64_t i) {
        Value v; v.type = Type::Number; v.number = (double)i; v.integer = i; v.integral = true; return v;
    }
    static Value string_value(std::string s) { Value v; v.type = Type::String; v.string = std::move(s); return v; }
    static Value array_value() { Value v; v.type = Type::Array; v.array = std::make_shared<Array>(); return v; }
    static Value object_value() { Value v; v.type = Type::Object; v.object = std::make_shared<Object>(); return v; }

    // Convenience accessors with soft failure semantics.
    bool is_null() const { return type == Type::Null; }
    bool is_bool() const { return type == Type::Bool; }
    bool is_number() const { return type == Type::Number; }
    bool is_string() const { return type == Type::String; }
    bool is_array() const { return type == Type::Array; }
    bool is_object() const { return type == Type::Object; }

    int64_t as_int(int64_t dflt = 0) const { return integral ? integer : (type == Type::Number ? (int64_t)number : dflt); }
    double as_number(double dflt = 0.0) const { return type == Type::Number ? number : dflt; }
    bool as_bool(bool dflt = false) const { return type == Type::Bool ? boolean : dflt; }
    const std::string &as_string() const { return string; }

    const Value *find(const char *key) const {
        if (type != Type::Object || !object) return nullptr;
        for (const auto &kv : *object) if (kv.first == key) return &kv.second;
        return nullptr;
    }
    Value *find(const char *key) {
        if (type != Type::Object || !object) return nullptr;
        for (auto &kv : *object) if (kv.first == key) return &kv.second;
        return nullptr;
    }
};

// Parse `text` in full. Throws std::runtime_error on malformed input.
Value parse(const char *text, size_t len);
inline Value parse(const std::string &s) { return parse(s.data(), s.size()); }

// Serialize a value back to canonical JSON (no whitespace).
void dump(const Value &v, std::string &out);
inline std::string dump(const Value &v) { std::string s; dump(v, s); return s; }

namespace detail {
class Parser {
  public:
    Parser(const char *t, size_t n) : t_(t), n_(n) {}
    Value run() {
        skip();
        Value v = value();
        skip();
        if (pos_ != n_) fail("trailing characters");
        return v;
    }

  private:
    const char *t_;
    size_t n_;
    size_t pos_ = 0;

    [[noreturn]] void fail(const std::string &w) { throw std::runtime_error("json: " + w); }
    char peek() { if (pos_ >= n_) fail("unexpected end of input"); return t_[pos_]; }
    void skip() { while (pos_ < n_ && (t_[pos_] == ' ' || t_[pos_] == '\t' || t_[pos_] == '\n' || t_[pos_] == '\r')) ++pos_; }
    void expect(char c) { if (pos_ >= n_ || t_[pos_] != c) fail("expected character"); ++pos_; }
    bool consume(char c) { if (pos_ < n_ && t_[pos_] == c) { ++pos_; return true; } return false; }

    Value value() {
        char c = peek();
        if (c == '{') return object();
        if (c == '[') return array();
        if (c == '"') return string();
        if (c == 't') { expect_lit("true"); return Value::boolean_value(true); }
        if (c == 'f') { expect_lit("false"); return Value::boolean_value(false); }
        if (c == 'n') { expect_lit("null"); return Value::null(); }
        if (c == '-' || (c >= '0' && c <= '9')) return number();
        fail("unexpected character");
    }

    void expect_lit(const char *lit) {
        size_t l = std::strlen(lit);
        if (pos_ + l > n_ || std::memcmp(t_ + pos_, lit, l) != 0) fail("bad literal");
        pos_ += l;
    }

    Value object() {
        Value o = Value::object_value();
        expect('{'); skip();
        if (consume('}')) return o;
        while (true) {
            skip();
            if (peek() != '"') fail("expected object key");
            std::string k = string().string;
            skip(); expect(':');
            o.object->emplace_back(std::move(k), value());
            skip();
            if (consume('}')) break;
            if (!consume(',')) fail("expected ',' or '}' in object");
        }
        return o;
    }

    Value array() {
        Value a = Value::array_value();
        expect('['); skip();
        if (consume(']')) return a;
        while (true) {
            a.array->push_back(value());
            skip();
            if (consume(']')) break;
            if (!consume(',')) fail("expected ',' or ']' in array");
        }
        return a;
    }

    Value string() {
        expect('"');
        std::string o;
        while (true) {
            if (pos_ >= n_) fail("unterminated string");
            char c = t_[pos_++];
            if (c == '"') break;
            if (c == '\\') {
                if (pos_ >= n_) fail("bad escape");
                char e = t_[pos_++];
                switch (e) {
                case '"': o += '"'; break;
                case '\\': o += '\\'; break;
                case '/': o += '/'; break;
                case 'b': o += '\b'; break;
                case 'f': o += '\f'; break;
                case 'n': o += '\n'; break;
                case 'r': o += '\r'; break;
                case 't': o += '\t'; break;
                case 'u': {
                    if (pos_ + 4 > n_) fail("bad unicode escape");
                    unsigned cp = 0;
                    for (int i = 0; i < 4; ++i) cp = cp * 16 + hex(t_[pos_++]);
                    // Basic Multilingual Plane only (no surrogate pairs needed
                    // for assembler identifiers, which are ASCII).
                    if (cp < 0x80) o += (char)cp;
                    else if (cp < 0x800) { o += (char)(0xC0 | (cp >> 6)); o += (char)(0x80 | (cp & 63)); }
                    else { o += (char)(0xE0 | (cp >> 12)); o += (char)(0x80 | ((cp >> 6) & 63)); o += (char)(0x80 | (cp & 63)); }
                    break;
                }
                default: fail("bad escape");
                }
            } else if ((unsigned char)c < 0x20) {
                fail("unescaped control character in string");
            } else {
                o += c;
            }
        }
        return Value::string_value(std::move(o));
    }

    static unsigned hex(char c) {
        if (c >= '0' && c <= '9') return (unsigned)(c - '0');
        if (c >= 'a' && c <= 'f') return (unsigned)(c - 'a' + 10);
        if (c >= 'A' && c <= 'F') return (unsigned)(c - 'A' + 10);
        throw std::runtime_error("json: bad hex digit");
    }

    Value number() {
        size_t s = pos_;
        if (consume('-')) {}
        while (pos_ < n_ && t_[pos_] >= '0' && t_[pos_] <= '9') ++pos_;
        bool is_float = false;
        if (pos_ < n_ && t_[pos_] == '.') { is_float = true; ++pos_; while (pos_ < n_ && t_[pos_] >= '0' && t_[pos_] <= '9') ++pos_; }
        if (pos_ < n_ && (t_[pos_] == 'e' || t_[pos_] == 'E')) {
            is_float = true; ++pos_;
            if (pos_ < n_ && (t_[pos_] == '+' || t_[pos_] == '-')) ++pos_;
            while (pos_ < n_ && t_[pos_] >= '0' && t_[pos_] <= '9') ++pos_;
        }
        std::string tok(t_ + s, pos_ - s);
        if (tok.empty() || tok == "-") fail("bad number");
        char *end = nullptr;
        double d = std::strtod(tok.c_str(), &end);
        if (end && *end != '\0') fail("bad number");
        if (!is_float) {
            // integral literal: keep exact integer form.
            char *e2 = nullptr;
            long long iv = std::strtoll(tok.c_str(), &e2, 10);
            if (e2 && *e2 == '\0') return Value::integer_value(iv);
        }
        return Value::number_value(d);
    }
};
} // namespace detail

inline Value parse(const char *text, size_t len) {
    detail::Parser p(text, len);
    return p.run();
}

inline void dump(const Value &v, std::string &out) {
    switch (v.type) {
    case Type::Null: out += "null"; break;
    case Type::Bool: out += v.boolean ? "true" : "false"; break;
    case Type::Number: {
        if (v.integral) { char b[32]; std::snprintf(b, sizeof b, "%lld", (long long)v.integer); out += b; }
        else { char b[40]; std::snprintf(b, sizeof b, "%.17g", v.number); out += b; }
        break;
    }
    case Type::String: {
        out += '"';
        for (unsigned char c : v.string) {
            switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\t': out += "\\t"; break;
            case '\r': out += "\\r"; break;
            default:
                if (c < 0x20) { char b[8]; std::snprintf(b, sizeof b, "\\u%04x", c); out += b; }
                else out += (char)c;
            }
        }
        out += '"';
        break;
    }
    case Type::Array:
        out += '[';
        for (size_t i = 0; i < v.array->size(); ++i) { if (i) out += ','; dump((*v.array)[i], out); }
        out += ']';
        break;
    case Type::Object:
        out += '{';
        for (size_t i = 0; i < v.object->size(); ++i) {
            if (i) out += ',';
            out += '"';
            for (unsigned char c : v.object->at(i).first) {
                if (c == '"' || c == '\\') out += '\\';
                out += (char)c;
            }
            out += "\":";
            dump(v.object->at(i).second, out);
        }
        out += '}';
        break;
    }
}

} // namespace domsymas::cc::json
