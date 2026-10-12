// Sandstone encoding functions: an Algol-like language (emit8, emit_modrm,
// emit_relocation, require, if/else, ...) parsed with the built-in lexer
// below, then interpreted with step/output/relocation limits.
#include "sandstone.hpp"

#include <cctype>

namespace domsymas::encoding {
namespace {

// ---- hand-written lexer (no third-party parser dependency) ----
static void skip_ws_comments(const std::string &src, size_t &pos) {
    while (pos < src.size()) {
        char c = src[pos];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') { pos++; continue; }
        if (c == '/' && pos + 1 < src.size() && src[pos + 1] == '/') {
            while (pos < src.size() && src[pos] != '\n') pos++;
            continue;
        }
        break;
    }
}

struct Tok {
    enum K { Ident, Num, Str, Sym, End } k = End;
    std::string text;
    int64_t num = 0;
};

// Two-character symbols are tried before one-character symbols.
static const char *const kSym2[] = {"==", "!=", "<=", ">=", "&&",
                                    "||", "<<", ">>"};

static bool lex_one(const std::string &src, size_t &pos, Tok &out,
                    std::string &err) {
    skip_ws_comments(src, pos);
    if (pos >= src.size())
        return false; // end of input, not an error
    size_t save = pos;
    char c = src[pos];
    if (c == '"') {
        pos++;
        std::string text;
        while (pos < src.size() && src[pos] != '"') {
            char ch = src[pos++];
            if (ch == '\\' && pos < src.size()) {
                char e = src[pos++];
                if (e == 'n') text += '\n';
                else if (e == 't') text += '\t';
                else text += e;
            } else text += ch;
        }
        if (pos >= src.size()) {
            err = "unterminated string literal";
            return false;
        }
        pos++; // closing quote
        out.k = Tok::Str;
        out.text = text;
        return true;
    }
    if (isdigit((unsigned char)c)) {
        int base = 10;
        if (c == '0' && pos + 1 < src.size() &&
            (src[pos + 1] == 'x' || src[pos + 1] == 'X')) {
            base = 16;
            pos += 2;
        }
        size_t ds = pos;
        while (pos < src.size() &&
               (base == 16 ? (bool)isxdigit((unsigned char)src[pos])
                           : (bool)isdigit((unsigned char)src[pos])))
            pos++;
        if (pos == ds) {
            err = "expected digits in number";
            return false;
        }
        try {
            out.num = std::stoll(src.substr(save, pos - save), nullptr, base);
        } catch (const std::exception &) {
            err = "integer out of range";
            return false;
        }
        out.k = Tok::Num;
        return true;
    }
    if (isalpha((unsigned char)c) || c == '_') {
        while (pos < src.size() &&
               (isalnum((unsigned char)src[pos]) || src[pos] == '_'))
            pos++;
        out.k = Tok::Ident;
        out.text = src.substr(save, pos - save);
        return true;
    }
    for (const char *s : kSym2) {
        if (src.compare(pos, 2, s) == 0) {
            pos += 2;
            out.k = Tok::Sym;
            out.text = s;
            return true;
        }
    }
    if (std::string("(){};,=<>+-*/%!&|").find(c) != std::string::npos) {
        pos++;
        out.k = Tok::Sym;
        out.text = std::string(1, c);
        return true;
    }
    err = std::string("unexpected character '") + c + "'";
    return false;
}

// token = str | num | ident | multi-char-symbol | single-char-symbol
// (realized directly by lex_one above).

// ---- token-stream recursive descent over the hand-written lexer ----
struct Lexer {
    std::vector<Tok> toks;
    std::string err;
    bool lex(const std::string &src) {
        size_t pos = 0;
        while (true) {
            size_t save = pos;
            skip_ws_comments(src, pos);
            if (pos >= src.size())
                break;
            Tok t;
            std::string tok_err;
            if (!lex_one(src, pos, t, tok_err)) {
                if (tok_err.empty())
                    err = "lex error at byte " + std::to_string(save);
                else
                    err = tok_err + " at byte " + std::to_string(save);
                return false;
            }
            toks.push_back(std::move(t));
            if (pos == save) {
                err = "lexer stalled at byte " + std::to_string(save);
                return false;
            }
        }
        return true;
    }
};

struct Cursor {
    const std::vector<Tok> &t; size_t i = 0; std::string err;
    bool eof() const { return i >= t.size(); }
    const Tok &peek() const { return t[i]; }
    bool eat_sym(const std::string &s) {
        if (!eof() && t[i].k == Tok::Sym && t[i].text == s) { i++; return true; }
        return false;
    }
    bool eat_ident(std::string &o) {
        if (!eof() && t[i].k == Tok::Ident) { o = t[i++].text; return true; }
        return false;
    }
};

static bool parse_expr(Cursor &c, Expr &out);
static bool parse_primary(Cursor &c, Expr &out) {
    if (c.eof()) { c.err = "unexpected end in expression"; return false; }
    const Tok &t = c.peek();
    if (t.k == Tok::Num) { out.kind = Expr::Kind::Num; out.num = t.num; c.i++; return true; }
    if (t.k == Tok::Str) { out.kind = Expr::Kind::Str; out.text = t.text; c.i++; return true; }
    if (t.k == Tok::Ident) {
        std::string name = t.text; c.i++;
        if (!c.eof() && c.peek().k == Tok::Sym && c.peek().text == "(") {
            c.i++;
            Expr call; call.kind = Expr::Kind::Call; call.text = name;
            if (!c.eat_sym(")")) {
                while (true) {
                    Expr a;
                    if (!parse_expr(c, a)) return false;
                    call.args.push_back(std::move(a));
                    if (c.eat_sym(")")) break;
                    if (!c.eat_sym(",")) { c.err = "expected ',' or ')'"; return false; }
                }
            }
            out = std::move(call); return true;
        }
        out.kind = Expr::Kind::Var; out.text = name; return true;
    }
    if (t.k == Tok::Sym && t.text == "(") {
        c.i++;
        if (!parse_expr(c, out)) return false;
        if (!c.eat_sym(")")) { c.err = "expected ')'"; return false; }
        return true;
    }
    if (t.k == Tok::Sym && (t.text == "-" || t.text == "!")) {
        std::string op = t.text; c.i++;
        Expr a;
        if (!parse_primary(c, a)) return false;
        out.kind = op == "-" ? Expr::Kind::Neg : Expr::Kind::Not;
        out.args.push_back(std::move(a)); return true;
    }
    c.err = "unexpected token in expression";
    return false;
}
static bool parse_bin(Cursor &c, int prec, Expr &out) {
    if (!parse_primary(c, out)) return false;
    auto level = [](const std::string &op) {
        if (op == "||") return 1;
        if (op == "&&") return 2;
        if (op == "==" || op == "!=") return 3;
        if (op == "<" || op == "<=" || op == ">" || op == ">=") return 4;
        if (op == "+" || op == "-") return 5;
        if (op == "*" || op == "/" || op == "%") return 6;
        return 0;
    };
    while (!c.eof() && c.peek().k == Tok::Sym) {
        int lv = level(c.peek().text);
        if (lv < prec) break;
        std::string op = c.peek().text; c.i++;
        Expr rhs, node;
        if (!parse_bin(c, lv + 1, rhs)) return false;
        if (op == "+") node.kind = Expr::Kind::Add;
        else if (op == "-") node.kind = Expr::Kind::Sub;
        else if (op == "*") node.kind = Expr::Kind::Mul;
        else if (op == "/") node.kind = Expr::Kind::Div;
        else if (op == "%") node.kind = Expr::Kind::Mod;
        else if (op == "==") node.kind = Expr::Kind::Eq;
        else if (op == "!=") node.kind = Expr::Kind::Ne;
        else if (op == "<") node.kind = Expr::Kind::Lt;
        else if (op == "<=") node.kind = Expr::Kind::Le;
        else if (op == ">") node.kind = Expr::Kind::Gt;
        else if (op == ">=") node.kind = Expr::Kind::Ge;
        else if (op == "&&") node.kind = Expr::Kind::And;
        else node.kind = Expr::Kind::Or;
        node.args.push_back(std::move(out));
        node.args.push_back(std::move(rhs));
        out = std::move(node);
    }
    return true;
}
static bool parse_expr(Cursor &c, Expr &out) { return parse_bin(c, 1, out); }

static bool parse_stmt(Cursor &c, Stmt &out);
static bool parse_block(Cursor &c, std::vector<Stmt> &body) {
    if (!c.eat_sym("{")) { c.err = "expected '{'"; return false; }
    while (!c.eof() && !(c.peek().k == Tok::Sym && c.peek().text == "}")) {
        Stmt s;
        if (!parse_stmt(c, s)) return false;
        body.push_back(std::move(s));
    }
    if (!c.eat_sym("}")) { c.err = "expected '}'"; return false; }
    return true;
}
static bool parse_stmt(Cursor &c, Stmt &out) {
    if (c.eof()) { c.err = "unexpected end in statement"; return false; }
    if (c.peek().k == Tok::Sym && c.peek().text == "{") {
        out.kind = Stmt::Kind::Block;
        return parse_block(c, out.body);
    }
    if (c.peek().k == Tok::Ident && c.peek().text == "if") {
        c.i++;
        if (!c.eat_sym("(")) { c.err = "expected '(' after if"; return false; }
        out.kind = Stmt::Kind::If;
        if (!parse_expr(c, out.cond)) return false;
        if (!c.eat_sym(")")) { c.err = "expected ')' after condition"; return false; }
        Stmt th;
        if (!parse_stmt(c, th)) return false;
        out.body.push_back(std::move(th));
        if (!c.eof() && c.peek().k == Tok::Ident && c.peek().text == "else") {
            c.i++;
            Stmt el;
            if (!parse_stmt(c, el)) return false;
            out.els.push_back(std::move(el));
        }
        return true;
    }
    if (c.peek().k == Tok::Ident && c.peek().text == "require") {
        c.i++;
        if (!c.eat_sym("(")) { c.err = "expected '(' after require"; return false; }
        out.kind = Stmt::Kind::Require;
        if (!parse_expr(c, out.cond)) return false;
        if (!c.eat_sym(")")) { c.err = "expected ')'"; return false; }
        if (!c.eat_sym(";")) { c.err = "expected ';'"; return false; }
        return true;
    }
    // call statement: ident '(' args ')' ';'
    std::string name;
    size_t save = c.i;
    if (c.eat_ident(name) && !c.eof() && c.peek().k == Tok::Sym && c.peek().text == "(") {
        c.i++;
        out.kind = Stmt::Kind::Call; out.callee = name;
        if (!c.eat_sym(")")) {
            while (true) {
                Expr a;
                if (!parse_expr(c, a)) return false;
                out.args.push_back(std::move(a));
                if (c.eat_sym(")")) break;
                if (!c.eat_sym(",")) { c.err = "expected ',' or ')' in call"; return false; }
            }
        }
        if (!c.eat_sym(";")) { c.err = "expected ';' after call"; return false; }
        return true;
    }
    c.i = save;
    c.err = "expected statement";
    return false;
}

} // namespace

bool parse(const std::string &source, Program &out, std::string &diag) {
    Lexer lx;
    if (!lx.lex(source)) { diag = lx.err; return false; }
    Cursor c{lx.toks, 0, ""};
    Program p;
    while (!c.eof()) {
        Stmt s;
        if (!parse_stmt(c, s)) { diag = c.err + " at token " + std::to_string(c.i); return false; }
        p.stmts.push_back(std::move(s));
    }
    out = std::move(p);
    return true;
}

std::vector<uint8_t> literal_prefix(const Program &p, size_t *nstmts) {
    std::vector<uint8_t> out;
    size_t n = 0;
    for (auto &s : p.stmts) {
        if (s.kind != Stmt::Kind::Call) break;
        auto is_const = [&](const Expr &e, int64_t &v) {
            if (e.kind == Expr::Kind::Num) { v = e.num; return true; }
            return false;
        };
        int64_t v = 0;
        if (s.callee == "emit8" && s.args.size() == 1 && is_const(s.args[0], v)) {
            out.push_back((uint8_t)(v & 0xFF));
        } else if ((s.callee == "emit16le" || s.callee == "emit16be") && s.args.size() == 1 &&
                   is_const(s.args[0], v)) {
            if (s.callee == "emit16le") { out.push_back((uint8_t)(v & 0xFF)); out.push_back((uint8_t)((v >> 8) & 0xFF)); }
            else { out.push_back((uint8_t)((v >> 8) & 0xFF)); out.push_back((uint8_t)(v & 0xFF)); }
        } else break;
        n++;
    }
    if (nstmts) *nstmts = n;
    return out;
}

namespace {

struct Interp {
    const std::map<std::string, Value> *ops = nullptr;
    Limits lim;
    Outcome *out = nullptr;
    size_t steps = 0;
    std::string err;

    bool emit(uint8_t b) {
        if (out->bytes.size() >= lim.max_bytes) { err = "output size limit exceeded"; return false; }
        out->bytes.push_back(b); return true;
    }
    bool num_of(const Expr &e, int64_t &v) {
        Value val;
        if (!eval(e, val)) return false;
        if (val.is_str) { err = "expected integer, got string"; return false; }
        v = val.num; return true;
    }
    bool str_of(const Expr &e, std::string &s) {
        Value val;
        if (!eval(e, val)) return false;
        if (!val.is_str) { err = "expected string/symbol, got integer"; return false; }
        s = val.str; return true;
    }
    bool eval(const Expr &e, Value &v) {
        if (++steps > lim.max_steps) { err = "execution step limit exceeded"; return false; }
        switch (e.kind) {
        case Expr::Kind::Num: v.is_str = false; v.num = e.num; return true;
        case Expr::Kind::Str: v.is_str = true; v.str = e.text; return true;
        case Expr::Kind::Var: {
            auto it = ops->find(e.text);
            if (it == ops->end()) { err = "unknown operand '" + e.text + "'"; return false; }
            v = it->second; return true;
        }
        case Expr::Kind::Neg: {
            int64_t a;
            if (!num_of(e.args[0], a)) return false;
            if (a == INT64_MIN) { err = "integer overflow"; return false; }
            v.is_str = false; v.num = -a; return true;
        }
        case Expr::Kind::Not: { int64_t a; if (!num_of(e.args[0], a)) return false; v.is_str = false; v.num = !a; return true; }
        default: break;
        }
        if (e.kind == Expr::Kind::Call) {
            // builtin pure functions
            if (e.text == "select") {
                if (e.args.size() != 3) { err = "select needs 3 args"; return false; }
                int64_t c; if (!num_of(e.args[0], c)) return false;
                return eval(c ? e.args[1] : e.args[2], v);
            }
            if (e.text == "regnum" || e.text == "value_of") {
                if (e.args.size() != 1) { err = e.text + " needs 1 arg"; return false; }
                int64_t a; if (!num_of(e.args[0], a)) return false;
                v.is_str = false; v.num = a & 0xFF; return true;
            }
            err = "unknown function '" + e.text + "'";
            return false;
        }
        int64_t a, b;
        if (e.kind == Expr::Kind::And || e.kind == Expr::Kind::Or) {
            if (!num_of(e.args[0], a)) return false;
            if ((e.kind == Expr::Kind::And && !a) || (e.kind == Expr::Kind::Or && a)) {
                v.is_str = false; v.num = e.kind == Expr::Kind::Or; return true;
            }
        }
        if (e.kind != Expr::Kind::And && e.kind != Expr::Kind::Or &&
            !num_of(e.args[0], a)) return false;
        if (!num_of(e.args[1], b)) return false;
        v.is_str = false;
        switch (e.kind) {
        case Expr::Kind::Add:
            if (__builtin_add_overflow(a, b, &v.num)) { err = "integer overflow"; return false; }
            break;
        case Expr::Kind::Sub:
            if (__builtin_sub_overflow(a, b, &v.num)) { err = "integer overflow"; return false; }
            break;
        case Expr::Kind::Mul:
            if (__builtin_mul_overflow(a, b, &v.num)) { err = "integer overflow"; return false; }
            break;
        case Expr::Kind::Div:
            if (!b || (a == INT64_MIN && b == -1)) { err = "invalid division"; return false; }
            v.num = a / b; break;
        case Expr::Kind::Mod:
            if (!b || (a == INT64_MIN && b == -1)) { err = "invalid modulo"; return false; }
            v.num = a % b; break;
        case Expr::Kind::Eq: v.num = (a == b); break;
        case Expr::Kind::Ne: v.num = (a != b); break;
        case Expr::Kind::Lt: v.num = (a < b); break;
        case Expr::Kind::Le: v.num = (a <= b); break;
        case Expr::Kind::Gt: v.num = (a > b); break;
        case Expr::Kind::Ge: v.num = (a >= b); break;
        case Expr::Kind::And: v.num = (a && b); break;
        case Expr::Kind::Or: v.num = (a || b); break;
        default: err = "bad expression"; return false;
        }
        return true;
    }

    bool exec_list(const std::vector<Stmt> &ss) {
        for (auto &s : ss) if (!exec(s)) return false;
        return true;
    }
    bool exec(const Stmt &s) {
        if (++steps > lim.max_steps) { err = "execution step limit exceeded"; return false; }
        switch (s.kind) {
        case Stmt::Kind::Block: return exec_list(s.body);
        case Stmt::Kind::Require: {
            int64_t c; if (!num_of(s.cond, c)) return false;
            if (!c) { err = "require() constraint failed"; return false; }
            return true;
        }
        case Stmt::Kind::If: {
            int64_t c; if (!num_of(s.cond, c)) return false;
            return exec_list(c ? s.body : s.els);
        }
        case Stmt::Kind::Call: return call(s);
        }
        return false;
    }
    bool emit_le(uint64_t v, int n) {
        for (int i = 0; i < n; i++)
            if (!emit((uint8_t)((v >> (i * 8)) & 0xFF))) return false;
        return true;
    }
    bool emit_be(uint64_t v, int n) {
        for (int i = n - 1; i >= 0; i--)
            if (!emit((uint8_t)((v >> (i * 8)) & 0xFF))) return false;
        return true;
    }
    bool call(const Stmt &s) {
        auto need = [&](size_t n) {
            if (s.args.size() != n) { err = s.callee + " needs " + std::to_string(n) + " args"; return false; }
            return true;
        };
        if (s.callee == "emit8") {
            if (!need(1)) return false;
            int64_t v; if (!num_of(s.args[0], v)) return false; return emit((uint8_t)v);
        }
        if (s.callee == "emit16le" || s.callee == "emit16be" || s.callee == "emit32le" ||
            s.callee == "emit32be" || s.callee == "emit64le" || s.callee == "emit64be" ||
            s.callee == "emit_imm" || s.callee == "emit_disp") {
            int width = 0; size_t ai = 0; bool be = false;
            if (s.callee == "emit_imm" || s.callee == "emit_disp") {
                if (s.args.size() != 2) { err = s.callee + " needs (width, value)"; return false; }
                int64_t w; if (!num_of(s.args[0], w)) return false;
                if (w != 1 && w != 2 && w != 4 && w != 8) { err = "bad width"; return false; }
                width = (int)w; ai = 1; be = false;
            } else {
                width = (s.callee[4] == '1') ? 2 : (s.callee[4] == '3' ? 4 : 8);
                be = s.callee.back() == 'e' && s.callee[s.callee.size() - 2] == 'b';
                // emit16le/be, emit32le/be, emit64le/be
                if (s.callee == "emit16be" || s.callee == "emit32be" || s.callee == "emit64be") be = true;
                else if (s.callee == "emit16le" || s.callee == "emit32le" || s.callee == "emit64le") be = false;
                if (!need(1)) return false;
            }
            int64_t v; if (!num_of(s.args[ai], v)) return false;
            return be ? emit_be((uint64_t)v, width) : emit_le((uint64_t)v, width);
        }
        if (s.callee == "emit_modrm") {
            if (!need(2)) return false;
            int64_t d, sr; if (!num_of(s.args[0], d) || !num_of(s.args[1], sr)) return false;
            return emit((uint8_t)(0xC0 | ((d & 7) << 3) | (sr & 7)));
        }
        if (s.callee == "emit_sib") {
            if (!need(3)) return false;
            int64_t a, b, c; if (!num_of(s.args[0], a) || !num_of(s.args[1], b) || !num_of(s.args[2], c)) return false;
            return emit((uint8_t)(((a & 3) << 6) | ((b & 7) << 3) | (c & 7)));
        }
        if (s.callee == "emit_relocation" || s.callee == "emit_reloc") {
            if (!need(2)) return false;
            std::string sym, type;
            if (!str_of(s.args[0], sym)) return false;
            // type may be ident (reloc name) or string
            if (s.args[1].kind == Expr::Kind::Var) type = s.args[1].text;
            else if (s.args[1].kind == Expr::Kind::Num) type = std::to_string(s.args[1].num);
            else if (!str_of(s.args[1], type)) return false;
            if (out->relocs.size() >= lim.max_relocs) { err = "relocation limit exceeded"; return false; }
            // type id resolved by caller (Databank::encode rewrites name->id);
            // here stash numeric id if the type expr was numeric.
            int64_t tid = 0; bool numeric = false;
            if (s.args[1].kind == Expr::Kind::Num) { numeric = true; tid = s.args[1].num; }
            RelocOut r;
            r.offset = (uint32_t)out->bytes.size();
            r.symbol = sym;
            r.type_name = type;
            r.type = numeric ? (uint32_t)tid : 0xFFFFFFFFu;
            r.addend = 0;
            out->relocs.push_back(std::move(r));
            return true;
        }
        if (s.callee == "fail") {
            std::string m = "explicit fail()";
            if (!s.args.empty() && s.args[0].kind == Expr::Kind::Str) m = s.args[0].text;
            err = m; return false;
        }
        err = "unknown builtin '" + s.callee + "'";
        return false;
    }
};

} // namespace

bool execute(const Program &p, const std::map<std::string, Value> &operands,
             const Limits &lim, Outcome &out, std::string &diag) {
    Interp ip;
    ip.ops = &operands; ip.lim = lim; ip.out = &out;
    if (!ip.exec_list(p.stmts)) { diag = ip.err; return false; }
    return true;
}

} // namespace domsymas::encoding
