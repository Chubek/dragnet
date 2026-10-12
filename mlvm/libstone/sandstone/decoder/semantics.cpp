// DomSymAs disassembler: candidate matching via encoder literal prefixes,
// operand decoding for modrm/imm/disp emitters, syntax-driven rendering.
#include "sandstone.hpp"

#include <functional>

namespace domsymas {

namespace {
std::string to_hex(uint64_t v, int bytes) {
    static const char *h = "0123456789abcdef";
    std::string o;
    for (int i = bytes - 1; i >= 0; i--) {
        o += h[(v >> (i * 8 + 4)) & 0xF];
        o += h[(v >> (i * 8)) & 0xF];
    }
    return o;
}
} // namespace

Error Databank::disassemble(const uint8_t *code, size_t len, uint32_t syntax,
                            std::vector<DisCandidate> &out) const {
    out.clear();
    if (!len) return Error{DOMSYMAS_ERR_TRUNCATED, "no input bytes", 0};
    if (!code) return Error{DOMSYMAS_ERR_TRUNCATED, "null input bytes", 0};
    for (auto &f : data_.forms) {
        const EncoderRec *e = find_encoder(f.encoder);
        if (!e) continue;
        encoding::Program prog;
        std::string dg;
        if (!encoding::parse(e->source, prog, dg)) continue;
        size_t npre = 0;
        std::vector<uint8_t> pre = encoding::literal_prefix(prog, &npre);
        if (pre.empty() || pre.size() > len) continue;
        bool match = true;
        for (size_t i = 0; i < pre.size(); i++)
            if (code[i] != pre[i]) { match = false; break; }
        if (!match) continue;
        // Decode operands: walk statements, tracking cursor past the prefix.
        size_t cur = pre.size();
        std::map<std::string, std::string> op_text;
        uint32_t op_idx = 0;
        auto put = [&](const std::string &v) {
            op_text["op" + std::to_string(op_idx++)] = v;
        };
        bool decodable = true;
        std::function<void(const std::vector<encoding::Stmt> &)> walk =
            [&](const std::vector<encoding::Stmt> &ss) {
                size_t idx = 0;
                for (auto &s : ss) {
                    // Statements already matched as the literal prefix are
                    // skipped: `cur` starts past the prefix bytes.
                    if (idx++ < npre && &ss == &prog.stmts) continue;
                    if (s.kind != encoding::Stmt::Kind::Call) { decodable = false; return; }
                    if (s.callee == "emit_modrm") {
                        if (cur >= len || (code[cur] & 0xC0) != 0xC0) { decodable = false; return; }
                        uint8_t b = code[cur++];
                        // Convention: emit_modrm(first, second) places the
                        // first operand in the reg field, the second in r/m.
                        op_text["op" + std::to_string(op_idx)] =
                            "reg" + std::to_string((b >> 3) & 7);
                        op_idx++;
                        op_text["op" + std::to_string(op_idx)] =
                            "rm" + std::to_string(b & 7) +
                            "(mod=" + std::to_string((b >> 6) & 3) + ")";
                        op_idx++;
                    } else if (s.callee == "emit_sib") {
                        if (cur >= len) { decodable = false; return; }
                        uint8_t b = code[cur++];
                        put("sib(" + to_hex(b, 1) + ")");
                    } else if ((s.callee == "emit_imm" || s.callee == "emit_disp" ||
                                s.callee == "emit16le" || s.callee == "emit16be" ||
                                s.callee == "emit32le" || s.callee == "emit32be" ||
                                s.callee == "emit64le" || s.callee == "emit64be")) {
                        int w = 0; bool be = false;
                        if (s.callee == "emit_imm" || s.callee == "emit_disp") {
                            if (!s.args.empty() && s.args[0].kind == encoding::Expr::Kind::Num)
                                w = (int)s.args[0].num;
                        } else {
                            w = (s.callee[4] == '1') ? 2 : (s.callee[4] == '3' ? 4 : 8);
                            be = s.callee[s.callee.size() - 2] == 'b';
                        }
                        if (w != 1 && w != 2 && w != 4 && w != 8) { decodable = false; return; }
                        if ((size_t)w > len - cur) { decodable = false; return; }
                        uint64_t v = 0;
                        if (be) { for (int i = 0; i < w; i++) v = (v << 8) | code[cur++]; }
                        else { for (int i = 0; i < w; i++) v |= (uint64_t)code[cur++] << (i * 8); }
                        if (s.args.back().kind == encoding::Expr::Kind::Num) {
                            if (v != (static_cast<uint64_t>(s.args.back().num) &
                                      (w == 8 ? UINT64_MAX : ((uint64_t{1} << (w * 8)) - 1)))) {
                                decodable = false; return;
                            }
                        } else {
                            put("0x" + to_hex(v, w));
                        }
                    } else if (s.callee == "emit8") {
                        if (cur >= len) { decodable = false; return; }
                        if (s.args.size() == 1 && s.args[0].kind == encoding::Expr::Kind::Num &&
                            code[cur] != static_cast<uint8_t>(s.args[0].num)) { decodable = false; return; }
                        if (s.args.size() != 1) { decodable = false; return; }
                        if (s.args[0].kind != encoding::Expr::Kind::Num)
                            put("0x" + to_hex(code[cur], 1));
                        ++cur;
                    } else if (s.callee == "emit_relocation" || s.callee == "emit_reloc") {
                        put("sym");
                    } else { decodable = false; return; }
                }
            };
        walk(prog.stmts);
        if (!decodable) continue;
        const MnemonicRec *m = nullptr;
        for (auto &mm : data_.mnemonics)
            if (mm.id == f.mnemonic) { m = &mm; break; }
        DisCandidate c;
        c.form = f.id;
        c.mnemonic = m ? m->canonical : "";
        c.consumed = (uint32_t)cur;
        c.text = format_form(f, syntax, op_text);
        out.push_back(std::move(c));
    }
    return Error{};
}

} // namespace domsymas
