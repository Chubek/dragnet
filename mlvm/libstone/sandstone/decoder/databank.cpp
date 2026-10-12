// DomSymAs databank: payload assembly/parsing, Databank load/save, Builder.
#include "detail.hpp"

#include <cstdio>

namespace domsymas {
namespace {

struct DirEnt { Section id; uint16_t flags = 0; uint32_t offset = 0, size = 0; uint32_t align = 1; uint32_t checksum = 0; };

void write_strings(detail::Writer &w, const std::vector<std::string> &t) {
    detail::write_section_prefix(w, (uint32_t)t.size());
    for (const auto &s : t) w.str(s);
}
bool read_strings(detail::Reader &r, size_t size, std::vector<std::string> &t,
                  std::string &diag) {
    size_t start = r.pos();
    uint32_t n = 0;
    if (!detail::read_section_prefix(r, size, n, diag)) return false;
    t.clear();
    for (uint32_t i = 0; i < n; i++) {
        std::string s;
        if (!r.str(s)) { diag = "truncated string table"; return false; }
        if (r.pos() - start > size) { diag = "string table overruns section"; return false; }
        t.push_back(s);
    }
    return true;
}

} // namespace

// ---- section serializers ----
namespace secs {

static void w_target(detail::Writer &w, const DatabankData &d, std::vector<std::string> &st) {
    detail::write_section_prefix(w, (uint32_t)d.targets.size());
    for (auto &t : d.targets) {
        w.u32(t.id);
        w.u32(detail::intern(st, t.arch)); w.u32(detail::intern(st, t.subarch));
        w.u32(t.addr_size); w.u32(t.default_op_size); w.u32(t.default_addr_size);
        w.u32(t.instr_align); w.u32(t.byte_order);
        w.u32(detail::intern(st, t.obj_format)); w.u32(detail::intern(st, t.abi));
        w.u32(detail::intern(st, t.calling_conv));
        detail::write_u32vec(w, t.features);
    }
}
static void w_features(detail::Writer &w, const DatabankData &d, std::vector<std::string> &st) {
    detail::write_section_prefix(w, (uint32_t)d.features.size());
    for (auto &f : d.features) {
        w.u32(f.id); w.u32(detail::intern(st, f.name)); w.u32(f.flags);
        w.u32(detail::intern(st, f.desc));
    }
}
static void w_regs(detail::Writer &w, const DatabankData &d, std::vector<std::string> &st) {
    detail::write_section_prefix(w, (uint32_t)d.registers.size());
    for (auto &r : d.registers) {
        w.u32(r.id); w.u32(detail::intern(st, r.name)); w.u32(r.regclass);
        w.u32(r.width); w.u32(r.enc_num); w.u32(r.parent);
        w.u32(r.sub_lo); w.u32(r.sub_hi); w.u32(r.flags);
    }
}
static void w_mnemonics(detail::Writer &w, const DatabankData &d, std::vector<std::string> &st) {
    detail::write_section_prefix(w, (uint32_t)d.mnemonics.size());
    for (auto &m : d.mnemonics) {
        w.u32(m.id); w.u32(detail::intern(st, m.canonical)); w.u32(m.flags);
        std::vector<uint32_t> al;
        for (auto &a : m.aliases) al.push_back(detail::intern(st, a));
        detail::write_u32vec(w, al);
        detail::write_u32vec(w, m.forms);
    }
}
static void w_forms(detail::Writer &w, const DatabankData &d) {
    detail::write_section_prefix(w, (uint32_t)d.forms.size());
    for (auto &f : d.forms) {
        w.u32(f.id); w.u32(f.mnemonic); w.u32(f.operand_count); w.u32(f.flags);
        w.u32(f.encoder);
        detail::write_u32vec(w, f.required);
        detail::write_u32vec(w, f.forbidden);
        detail::write_u32vec(w, f.syntaxes);
    }
}
static void w_operands(detail::Writer &w, const DatabankData &d, std::vector<std::string> &st) {
    detail::write_section_prefix(w, (uint32_t)d.operands.size());
    for (auto &o : d.operands) {
        w.u32(o.id); w.u32(o.form); w.u32(o.position); w.u32(o.kind);
        w.u32(o.width); w.u32(o.regclass); w.i64(o.imm_min); w.i64(o.imm_max);
        w.u32(o.is_signed); w.u32(o.enc_role); w.u32(o.access); w.u32(o.hidden);
        w.u32(detail::intern(st, o.constraint));
    }
}
static void w_encoders(detail::Writer &w, const DatabankData &d, std::vector<std::string> &st) {
    detail::write_section_prefix(w, (uint32_t)d.encoders.size());
    for (auto &e : d.encoders) {
        w.u32(e.id); w.u32(detail::intern(st, e.name));
        w.u32(detail::intern(st, e.source)); w.u32(e.flags);
        w.u32(e.max_bytes); w.u32(e.max_relocs);
    }
}
static void w_syntax(detail::Writer &w, const DatabankData &d, std::vector<std::string> &st) {
    detail::write_section_prefix(w, (uint32_t)d.syntaxes.size());
    for (auto &s : d.syntaxes) {
        w.u32(s.id); w.u32(detail::intern(st, s.name)); w.u32(s.operand_order);
        w.u32(detail::intern(st, s.reg_prefix)); w.u32(detail::intern(st, s.imm_prefix));
        w.u32(detail::intern(st, s.comment)); w.u32(detail::intern(st, s.separator));
        w.u32(s.case_sensitive); w.u32(detail::intern(st, s.tmpl));
        detail::write_u32vec(w, s.forms);
    }
}
static void w_sections(detail::Writer &w, const DatabankData &d, std::vector<std::string> &st) {
    detail::write_section_prefix(w, (uint32_t)d.sections.size());
    for (auto &s : d.sections) {
        w.u32(s.id); w.u32(detail::intern(st, s.name)); w.u32(s.flags);
        w.u32(s.align); w.u32(s.addrspace); w.u32(s.content);
    }
}
static void w_linking(detail::Writer &w, const DatabankData &d, std::vector<std::string> &st) {
    detail::write_section_prefix(w, (uint32_t)d.linkages.size());
    for (auto &l : d.linkages) {
        w.u32(l.id); w.u32(detail::intern(st, l.name)); w.u32(l.binding);
        w.u32(l.visibility); w.u32(l.symtype); w.u32(l.flags);
    }
}
static void w_relocs(detail::Writer &w, const DatabankData &d, std::vector<std::string> &st) {
    detail::write_section_prefix(w, (uint32_t)d.relocs.size());
    for (auto &r : d.relocs) {
        w.u32(r.id); w.u32(detail::intern(st, r.name)); w.u32(r.width);
        w.u32(r.offset); w.u32(r.is_signed); w.u32(r.pcrel); w.u32(r.overflow);
        w.u32(r.addend); w.u32(detail::intern(st, r.expr));
    }
}
static void w_index(detail::Writer &w, const DatabankData &d, std::vector<std::string> &st) {
    detail::write_section_prefix(w, (uint32_t)d.index.size());
    for (auto &ix : d.index) {
        w.u32(ix.id); w.u32(ix.kind); w.u32(detail::intern(st, ix.key)); w.u32(ix.record);
    }
}
static void w_debug(detail::Writer &w, const DatabankData &d, std::vector<std::string> &st) {
    detail::write_section_prefix(w, (uint32_t)d.debug.size());
    for (auto &g : d.debug) {
        w.u32(g.id); w.u32(detail::intern(st, g.msg)); w.u32(g.ref_kind); w.u32(g.ref_id);
    }
}
static void w_isel(detail::Writer &w, const DatabankData &d, std::vector<std::string> &st) {
    detail::write_section_prefix(w, (uint32_t)d.isels.size());
    for (auto &r : d.isels) {
        w.u32(r.id); w.u32(r.kind); w.u32(detail::intern(st, r.node)); w.u32(r.form);
        w.u32(r.cost); w.u32(r.complexity); w.u32(r.priority); w.u32(r.flags);
        detail::write_u32vec(w, r.required);
        detail::write_u32vec(w, r.forbidden);
        detail::write_u32vec(w, r.alts);
        w.u32(detail::intern(st, r.pattern));
    }
}
static void w_sched(detail::Writer &w, const DatabankData &d, std::vector<std::string> &st) {
    detail::write_section_prefix(w, (uint32_t)d.scheds.size());
    for (auto &r : d.scheds) {
        w.u32(r.id); w.u32(r.form); w.u32(detail::intern(st, r.name));
        w.u32(r.latency); w.u32(r.uops); w.u32(r.cycles); w.u32(r.flags);
        detail::write_u32vec(w, r.writes);
        detail::write_u32vec(w, r.reads);
    }
}
static void w_units(detail::Writer &w, const DatabankData &d, std::vector<std::string> &st) {
    detail::write_section_prefix(w, (uint32_t)d.units.size());
    for (auto &r : d.units) {
        w.u32(r.id); w.u32(r.model); w.u32(detail::intern(st, r.name)); w.u32(r.kind);
        w.u32(r.count); w.u32(r.latency); w.u32(r.throughput); w.u32(r.flags);
    }
}
static void w_models(detail::Writer &w, const DatabankData &d, std::vector<std::string> &st) {
    detail::write_section_prefix(w, (uint32_t)d.models.size());
    for (auto &r : d.models) {
        w.u32(r.id); w.u32(detail::intern(st, r.name)); w.u32(detail::intern(st, r.vendor));
        w.u32(r.issue_width); w.u32(r.micro_op_buffer); w.u32(r.loop_buffer);
        w.u32(r.load_latency); w.u32(r.mispredict_penalty);
        w.u32(r.high_latency); w.u32(r.high_uops); w.u32(r.flags);
        detail::write_u32vec(w, r.features);
        detail::write_u32vec(w, r.tune_features);
        w.u32(detail::intern(st, r.desc));
    }
}
static void w_tuning(detail::Writer &w, const DatabankData &d, std::vector<std::string> &st) {
    detail::write_section_prefix(w, (uint32_t)d.tunes.size());
    for (auto &r : d.tunes) {
        w.u32(r.id); w.u32(r.model); w.u32(r.kind); w.u32(detail::intern(st, r.name));
        w.u32(r.form); w.u32(r.form2); w.i64(r.value); w.u32(r.flags);
        detail::write_u32vec(w, r.features);
        w.u32(detail::intern(st, r.desc));
    }
}
static void w_fusion(detail::Writer &w, const DatabankData &d, std::vector<std::string> &st) {
    detail::write_section_prefix(w, (uint32_t)d.fusions.size());
    for (auto &r : d.fusions) {
        w.u32(r.id); w.u32(detail::intern(st, r.name)); w.u32(r.a); w.u32(r.b);
        w.u32(r.kind); w.u32(r.latency); w.u32(r.flags);
        detail::write_u32vec(w, r.features);
        w.u32(detail::intern(st, r.pattern));
    }
}

} // namespace secs

static std::vector<uint8_t> encode_section(Section id, const DatabankData &d,
                                           std::vector<std::string> &st, bool be) {
    detail::Writer w(be);
    switch (id) {
    case Section::Target: secs::w_target(w, d, st); break;
    case Section::Features: secs::w_features(w, d, st); break;
    case Section::Registers: secs::w_regs(w, d, st); break;
    case Section::Mnemonics: secs::w_mnemonics(w, d, st); break;
    case Section::Opcodes: secs::w_forms(w, d); break;
    case Section::Operands: secs::w_operands(w, d, st); break;
    case Section::Encoders: secs::w_encoders(w, d, st); break;
    case Section::Syntax: secs::w_syntax(w, d, st); break;
    case Section::Sections: secs::w_sections(w, d, st); break;
    case Section::Linking: secs::w_linking(w, d, st); break;
    case Section::Relocations: secs::w_relocs(w, d, st); break;
    case Section::Index: secs::w_index(w, d, st); break;
    case Section::Debug: secs::w_debug(w, d, st); break;
    case Section::Isel: secs::w_isel(w, d, st); break;
    case Section::Sched: secs::w_sched(w, d, st); break;
    case Section::Units: secs::w_units(w, d, st); break;
    case Section::Models: secs::w_models(w, d, st); break;
    case Section::Tuning: secs::w_tuning(w, d, st); break;
    case Section::Fusion: secs::w_fusion(w, d, st); break;
    case Section::Strings: break; // handled separately
    }
    return w.buf;
}

// ---- section parsers ----
namespace parse {

static bool str_of(detail::Reader &r, const std::vector<std::string> &t,
                   std::string &out, std::string &diag) {
    uint32_t i = 0;
    if (!r.u32(i)) { diag = "truncated string ref"; return false; }
    if (!detail::fetch_str(t, i, out)) { diag = "string ref out of range"; return false; }
    return true;
}
static bool u32vec(detail::Reader &r, std::vector<uint32_t> &v, std::string &diag) {
    return detail::read_u32vec(r, 100000, v, diag);
}

static bool target(detail::Reader &r, DatabankData &d, std::string &dg) {
    uint32_t n = 0; if (!detail::read_section_prefix(r, r.left(), n, dg)) return false;
    for (uint32_t i = 0; i < n; i++) {
        TargetRec t; uint32_t a, b, f, ab, cv;
        if (!r.u32(t.id) || !r.u32(a) || !r.u32(b) || !r.u32(t.addr_size) ||
            !r.u32(t.default_op_size) || !r.u32(t.default_addr_size) ||
            !r.u32(t.instr_align) || !r.u32(t.byte_order) || !r.u32(f) ||
            !r.u32(ab) || !r.u32(cv) || !u32vec(r, t.features, dg)) {
            dg = "truncated TARGET record"; return false;
        }
        if (!detail::fetch_str(d.strings, a, t.arch) ||
            !detail::fetch_str(d.strings, b, t.subarch) ||
            !detail::fetch_str(d.strings, f, t.obj_format) ||
            !detail::fetch_str(d.strings, ab, t.abi) ||
            !detail::fetch_str(d.strings, cv, t.calling_conv)) {
            dg = "TARGET string ref out of range"; return false;
        }
        d.targets.push_back(std::move(t));
    }
    return true;
}
static bool features(detail::Reader &r, DatabankData &d, std::string &dg) {
    uint32_t n = 0; if (!detail::read_section_prefix(r, r.left(), n, dg)) return false;
    for (uint32_t i = 0; i < n; i++) {
        FeatureRec f; uint32_t nm, ds;
        if (!r.u32(f.id) || !r.u32(nm) || !r.u32(f.flags) || !r.u32(ds)) {
            dg = "truncated FEATURES record"; return false;
        }
        if (!detail::fetch_str(d.strings, nm, f.name) ||
            !detail::fetch_str(d.strings, ds, f.desc)) {
            dg = "FEATURES string ref out of range"; return false;
        }
        d.features.push_back(std::move(f));
    }
    return true;
}
static bool regs(detail::Reader &r, DatabankData &d, std::string &dg) {
    uint32_t n = 0; if (!detail::read_section_prefix(r, r.left(), n, dg)) return false;
    for (uint32_t i = 0; i < n; i++) {
        RegisterRec g; uint32_t nm;
        if (!r.u32(g.id) || !r.u32(nm) || !r.u32(g.regclass) || !r.u32(g.width) ||
            !r.u32(g.enc_num) || !r.u32(g.parent) || !r.u32(g.sub_lo) ||
            !r.u32(g.sub_hi) || !r.u32(g.flags)) {
            dg = "truncated REGISTERS record"; return false;
        }
        if (!detail::fetch_str(d.strings, nm, g.name)) {
            dg = "REGISTERS string ref out of range"; return false;
        }
        d.registers.push_back(std::move(g));
    }
    return true;
}
static bool mnemonics(detail::Reader &r, DatabankData &d, std::string &dg) {
    uint32_t n = 0; if (!detail::read_section_prefix(r, r.left(), n, dg)) return false;
    for (uint32_t i = 0; i < n; i++) {
        MnemonicRec m; std::vector<uint32_t> al;
        if (!r.u32(m.id) || !str_of(r, d.strings, m.canonical, dg) ||
            !r.u32(m.flags) || !u32vec(r, al, dg) || !u32vec(r, m.forms, dg)) {
            if (dg.empty()) dg = "truncated MNEMONICS record";
            return false;
        }
        for (uint32_t a : al) {
            std::string s;
            if (!detail::fetch_str(d.strings, a, s)) {
                dg = "MNEMONICS alias ref out of range"; return false;
            }
            m.aliases.push_back(s);
        }
        d.mnemonics.push_back(std::move(m));
    }
    return true;
}
static bool forms(detail::Reader &r, DatabankData &d, std::string &dg) {
    uint32_t n = 0; if (!detail::read_section_prefix(r, r.left(), n, dg)) return false;
    for (uint32_t i = 0; i < n; i++) {
        FormRec f;
        if (!r.u32(f.id) || !r.u32(f.mnemonic) || !r.u32(f.operand_count) ||
            !r.u32(f.flags) || !r.u32(f.encoder) || !u32vec(r, f.required, dg) ||
            !u32vec(r, f.forbidden, dg) || !u32vec(r, f.syntaxes, dg)) {
            if (dg.empty()) dg = "truncated OPCODES record";
            return false;
        }
        d.forms.push_back(std::move(f));
    }
    return true;
}
static bool operands(detail::Reader &r, DatabankData &d, std::string &dg) {
    uint32_t n = 0; if (!detail::read_section_prefix(r, r.left(), n, dg)) return false;
    for (uint32_t i = 0; i < n; i++) {
        OperandRec o; uint32_t cx;
        if (!r.u32(o.id) || !r.u32(o.form) || !r.u32(o.position) || !r.u32(o.kind) ||
            !r.u32(o.width) || !r.u32(o.regclass) || !r.i64(o.imm_min) ||
            !r.i64(o.imm_max) || !r.u32(o.is_signed) || !r.u32(o.enc_role) ||
            !r.u32(o.access) || !r.u32(o.hidden) || !r.u32(cx)) {
            dg = "truncated OPERANDS record"; return false;
        }
        if (!detail::fetch_str(d.strings, cx, o.constraint)) {
            dg = "OPERANDS string ref out of range"; return false;
        }
        d.operands.push_back(std::move(o));
    }
    return true;
}
static bool encoders(detail::Reader &r, DatabankData &d, std::string &dg) {
    uint32_t n = 0; if (!detail::read_section_prefix(r, r.left(), n, dg)) return false;
    for (uint32_t i = 0; i < n; i++) {
        EncoderRec e;
        if (!r.u32(e.id) || !str_of(r, d.strings, e.name, dg) ||
            !str_of(r, d.strings, e.source, dg) || !r.u32(e.flags) ||
            !r.u32(e.max_bytes) || !r.u32(e.max_relocs)) {
            if (dg.empty()) dg = "truncated ENCODERS record";
            return false;
        }
        d.encoders.push_back(std::move(e));
    }
    return true;
}
static bool syntax(detail::Reader &r, DatabankData &d, std::string &dg) {
    uint32_t n = 0; if (!detail::read_section_prefix(r, r.left(), n, dg)) return false;
    for (uint32_t i = 0; i < n; i++) {
        SyntaxRec s;
        if (!r.u32(s.id) || !str_of(r, d.strings, s.name, dg) || !r.u32(s.operand_order) ||
            !str_of(r, d.strings, s.reg_prefix, dg) || !str_of(r, d.strings, s.imm_prefix, dg) ||
            !str_of(r, d.strings, s.comment, dg) || !str_of(r, d.strings, s.separator, dg) ||
            !r.u32(s.case_sensitive) || !str_of(r, d.strings, s.tmpl, dg) ||
            !u32vec(r, s.forms, dg)) {
            if (dg.empty()) dg = "truncated SYNTAX record";
            return false;
        }
        d.syntaxes.push_back(std::move(s));
    }
    return true;
}
static bool sections(detail::Reader &r, DatabankData &d, std::string &dg) {
    uint32_t n = 0; if (!detail::read_section_prefix(r, r.left(), n, dg)) return false;
    for (uint32_t i = 0; i < n; i++) {
        SectionRec s;
        if (!r.u32(s.id) || !str_of(r, d.strings, s.name, dg) || !r.u32(s.flags) ||
            !r.u32(s.align) || !r.u32(s.addrspace) || !r.u32(s.content)) {
            if (dg.empty()) dg = "truncated SECTIONS record";
            return false;
        }
        d.sections.push_back(std::move(s));
    }
    return true;
}
static bool linking(detail::Reader &r, DatabankData &d, std::string &dg) {
    uint32_t n = 0; if (!detail::read_section_prefix(r, r.left(), n, dg)) return false;
    for (uint32_t i = 0; i < n; i++) {
        LinkageRec l;
        if (!r.u32(l.id) || !str_of(r, d.strings, l.name, dg) || !r.u32(l.binding) ||
            !r.u32(l.visibility) || !r.u32(l.symtype) || !r.u32(l.flags)) {
            if (dg.empty()) dg = "truncated LINKING record";
            return false;
        }
        d.linkages.push_back(std::move(l));
    }
    return true;
}
static bool relocs(detail::Reader &r, DatabankData &d, std::string &dg) {
    uint32_t n = 0; if (!detail::read_section_prefix(r, r.left(), n, dg)) return false;
    for (uint32_t i = 0; i < n; i++) {
        RelocRec x;
        if (!r.u32(x.id) || !str_of(r, d.strings, x.name, dg) || !r.u32(x.width) ||
            !r.u32(x.offset) || !r.u32(x.is_signed) || !r.u32(x.pcrel) ||
            !r.u32(x.overflow) || !r.u32(x.addend) || !str_of(r, d.strings, x.expr, dg)) {
            if (dg.empty()) dg = "truncated RELOCATIONS record";
            return false;
        }
        d.relocs.push_back(std::move(x));
    }
    return true;
}
static bool index(detail::Reader &r, DatabankData &d, std::string &dg) {
    uint32_t n = 0; if (!detail::read_section_prefix(r, r.left(), n, dg)) return false;
    for (uint32_t i = 0; i < n; i++) {
        IndexRec ix;
        if (!r.u32(ix.id) || !r.u32(ix.kind) || !str_of(r, d.strings, ix.key, dg) ||
            !r.u32(ix.record)) {
            if (dg.empty()) dg = "truncated INDEX record";
            return false;
        }
        d.index.push_back(std::move(ix));
    }
    return true;
}
static bool debug(detail::Reader &r, DatabankData &d, std::string &dg) {
    uint32_t n = 0; if (!detail::read_section_prefix(r, r.left(), n, dg)) return false;
    for (uint32_t i = 0; i < n; i++) {
        DebugRec g;
        if (!r.u32(g.id) || !str_of(r, d.strings, g.msg, dg) || !r.u32(g.ref_kind) ||
            !r.u32(g.ref_id)) {
            if (dg.empty()) dg = "truncated DEBUG record";
            return false;
        }
        d.debug.push_back(std::move(g));
    }
    return true;
}
static bool isels(detail::Reader &r, DatabankData &d, std::string &dg) {
    uint32_t n = 0; if (!detail::read_section_prefix(r, r.left(), n, dg)) return false;
    for (uint32_t i = 0; i < n; i++) {
        IselRec v;
        if (!r.u32(v.id) || !r.u32(v.kind) || !str_of(r, d.strings, v.node, dg) ||
            !r.u32(v.form) || !r.u32(v.cost) || !r.u32(v.complexity) ||
            !r.u32(v.priority) || !r.u32(v.flags) || !u32vec(r, v.required, dg) ||
            !u32vec(r, v.forbidden, dg) || !u32vec(r, v.alts, dg) ||
            !str_of(r, d.strings, v.pattern, dg)) {
            if (dg.empty()) dg = "truncated ISEL record";
            return false;
        }
        d.isels.push_back(std::move(v));
    }
    return true;
}
static bool scheds(detail::Reader &r, DatabankData &d, std::string &dg) {
    uint32_t n = 0; if (!detail::read_section_prefix(r, r.left(), n, dg)) return false;
    for (uint32_t i = 0; i < n; i++) {
        SchedRec v;
        if (!r.u32(v.id) || !r.u32(v.form) || !str_of(r, d.strings, v.name, dg) ||
            !r.u32(v.latency) || !r.u32(v.uops) || !r.u32(v.cycles) || !r.u32(v.flags) ||
            !u32vec(r, v.writes, dg) || !u32vec(r, v.reads, dg)) {
            if (dg.empty()) dg = "truncated SCHED record";
            return false;
        }
        d.scheds.push_back(std::move(v));
    }
    return true;
}
static bool units(detail::Reader &r, DatabankData &d, std::string &dg) {
    uint32_t n = 0; if (!detail::read_section_prefix(r, r.left(), n, dg)) return false;
    for (uint32_t i = 0; i < n; i++) {
        UnitRec v;
        if (!r.u32(v.id) || !r.u32(v.model) || !str_of(r, d.strings, v.name, dg) ||
            !r.u32(v.kind) || !r.u32(v.count) || !r.u32(v.latency) ||
            !r.u32(v.throughput) || !r.u32(v.flags)) {
            if (dg.empty()) dg = "truncated UNITS record";
            return false;
        }
        d.units.push_back(std::move(v));
    }
    return true;
}
static bool models(detail::Reader &r, DatabankData &d, std::string &dg) {
    uint32_t n = 0; if (!detail::read_section_prefix(r, r.left(), n, dg)) return false;
    for (uint32_t i = 0; i < n; i++) {
        ModelRec v;
        if (!r.u32(v.id) || !str_of(r, d.strings, v.name, dg) ||
            !str_of(r, d.strings, v.vendor, dg) || !r.u32(v.issue_width) ||
            !r.u32(v.micro_op_buffer) || !r.u32(v.loop_buffer) ||
            !r.u32(v.load_latency) || !r.u32(v.mispredict_penalty) ||
            !r.u32(v.high_latency) || !r.u32(v.high_uops) || !r.u32(v.flags) ||
            !u32vec(r, v.features, dg) || !u32vec(r, v.tune_features, dg) ||
            !str_of(r, d.strings, v.desc, dg)) {
            if (dg.empty()) dg = "truncated MODELS record";
            return false;
        }
        d.models.push_back(std::move(v));
    }
    return true;
}
static bool tunes(detail::Reader &r, DatabankData &d, std::string &dg) {
    uint32_t n = 0; if (!detail::read_section_prefix(r, r.left(), n, dg)) return false;
    for (uint32_t i = 0; i < n; i++) {
        TuneRec v;
        if (!r.u32(v.id) || !r.u32(v.model) || !r.u32(v.kind) ||
            !str_of(r, d.strings, v.name, dg) || !r.u32(v.form) || !r.u32(v.form2) ||
            !r.i64(v.value) || !r.u32(v.flags) || !u32vec(r, v.features, dg) ||
            !str_of(r, d.strings, v.desc, dg)) {
            if (dg.empty()) dg = "truncated TUNING record";
            return false;
        }
        d.tunes.push_back(std::move(v));
    }
    return true;
}
static bool fusions(detail::Reader &r, DatabankData &d, std::string &dg) {
    uint32_t n = 0; if (!detail::read_section_prefix(r, r.left(), n, dg)) return false;
    for (uint32_t i = 0; i < n; i++) {
        FusionRec v;
        if (!r.u32(v.id) || !str_of(r, d.strings, v.name, dg) || !r.u32(v.a) ||
            !r.u32(v.b) || !r.u32(v.kind) || !r.u32(v.latency) || !r.u32(v.flags) ||
            !u32vec(r, v.features, dg) || !str_of(r, d.strings, v.pattern, dg)) {
            if (dg.empty()) dg = "truncated FUSION record";
            return false;
        }
        d.fusions.push_back(std::move(v));
    }
    return true;
}

} // namespace parse

// ---- payload assembly ----
static Error build_payload(const DatabankData &d, bool be, std::vector<uint8_t> &payload) {
    // String table first (intern everything in deterministic order).
    std::vector<std::string> st;
    st.push_back("");
    auto snapshot = [&](const DatabankData &dd) {
        for (auto &t : dd.targets) {
            detail::intern(st, t.arch); detail::intern(st, t.subarch);
            detail::intern(st, t.obj_format); detail::intern(st, t.abi);
            detail::intern(st, t.calling_conv);
        }
        for (auto &f : dd.features) { detail::intern(st, f.name); detail::intern(st, f.desc); }
        for (auto &r : dd.registers) detail::intern(st, r.name);
        for (auto &m : dd.mnemonics) {
            detail::intern(st, m.canonical);
            for (auto &a : m.aliases) detail::intern(st, a);
        }
        for (auto &o : dd.operands) detail::intern(st, o.constraint);
        for (auto &e : dd.encoders) { detail::intern(st, e.name); detail::intern(st, e.source); }
        for (auto &s : dd.syntaxes) {
            detail::intern(st, s.name); detail::intern(st, s.reg_prefix);
            detail::intern(st, s.imm_prefix); detail::intern(st, s.comment);
            detail::intern(st, s.separator); detail::intern(st, s.tmpl);
        }
        for (auto &s : dd.sections) detail::intern(st, s.name);
        for (auto &l : dd.linkages) detail::intern(st, l.name);
        for (auto &r2 : dd.relocs) { detail::intern(st, r2.name); detail::intern(st, r2.expr); }
        for (auto &ix : dd.index) detail::intern(st, ix.key);
        for (auto &g : dd.debug) detail::intern(st, g.msg);
        for (auto &v : dd.isels) { detail::intern(st, v.node); detail::intern(st, v.pattern); }
        for (auto &v : dd.scheds) detail::intern(st, v.name);
        for (auto &v : dd.units) detail::intern(st, v.name);
        for (auto &v : dd.models) {
            detail::intern(st, v.name); detail::intern(st, v.vendor);
            detail::intern(st, v.desc);
        }
        for (auto &v : dd.tunes) { detail::intern(st, v.name); detail::intern(st, v.desc); }
        for (auto &v : dd.fusions) { detail::intern(st, v.name); detail::intern(st, v.pattern); }
    };
    snapshot(d);

    // Re-intern during section encoding may append late strings; to keep the
    // table stable, pre-encode sections against a scratch copy, then freeze.
    std::vector<std::string> work = st;
    const Section order[] = {Section::Target, Section::Features, Section::Registers,
        Section::Mnemonics, Section::Opcodes, Section::Operands, Section::Encoders,
        Section::Syntax, Section::Sections, Section::Linking, Section::Relocations,
        Section::Isel, Section::Sched, Section::Units, Section::Models, Section::Tuning,
        Section::Fusion, Section::Index, Section::Debug};
    std::vector<std::pair<Section, std::vector<uint8_t>>> bodies;
    for (Section s : order) bodies.emplace_back(s, encode_section(s, d, work, be));
    // Rebuild string bytes from the final table (sections reference it by index).
    st = work;
    detail::Writer sw(be);
    write_strings(sw, st);

    struct Out { Section id; std::vector<uint8_t> bytes; uint32_t align; };
    std::vector<Out> outs;
    outs.push_back({Section::Strings, sw.buf, 1});
    for (auto &b : bodies) outs.push_back({b.first, b.second, 4});

    size_t dir_n = outs.size();
    std::vector<DirEnt> dir;
    // Mirror the emission loop below: running offset with alignment padding.
    size_t off = 8 + dir_n * kDirEntrySize;
    auto next_off = [&](uint32_t align, size_t sz) {
        while ((off % align) != 0) off++;
        uint32_t at = (uint32_t)off;
        off += sz;
        return at;
    };
    for (auto &o : outs) {
        DirEnt e; e.id = o.id; e.size = (uint32_t)o.bytes.size();
        e.align = o.align; e.checksum = crc32(o.bytes.data(), o.bytes.size());
        e.offset = next_off(o.align, o.bytes.size());
        if (o.id == Section::Index) e.flags = DOMSYMAS_DIRFLAG_INDEX;
        // Optimization sections are skippable by a reader that predates them.
        if ((uint16_t)o.id >= kFirstOptionalSection && o.id != Section::Index)
            e.flags |= DOMSYMAS_DIRFLAG_OPTIONAL;
        dir.push_back(e);
        if (off > (64u << 20)) return Error{DOMSYMAS_ERR_OVERFLOW, "payload exceeds 64 MiB", off};
    }
    detail::Writer w(be);
    w.u32(kDbMagic); w.u16(kVersion); w.u16((uint16_t)dir_n);
    for (auto &e : dir) {
        w.u16((uint16_t)e.id); w.u16(e.flags); w.u32(e.offset); w.u32(e.size);
        w.u32(e.align); w.u32(e.checksum);
    }
    payload = w.buf;
    for (auto &o : outs) {
        // Pad to the section's alignment (padding is not part of the section).
        while ((payload.size() % o.align) != 0) payload.push_back(0);
        // dir entry was computed with the same padding rule; sanity check.
        payload.insert(payload.end(), o.bytes.begin(), o.bytes.end());
    }
    return Error{};
}

// ---- payload parsing ----
static Error parse_payload(const std::vector<uint8_t> &p, bool be, DatabankData &d) {
    if (p.size() < 8) return Error{DOMSYMAS_ERR_TRUNCATED, "payload smaller than databank header", 0};
    detail::Reader r(p.data(), p.size(), be);
    uint32_t magic = 0; uint16_t ver = 0, nsec = 0;
    if (!r.u32(magic) || !r.u16(ver) || !r.u16(nsec))
        return Error{DOMSYMAS_ERR_TRUNCATED, "truncated databank header", 0};
    if (magic != kDbMagic) return Error{DOMSYMAS_ERR_BAD_MAGIC, "bad databank magic", 0};
    if (ver != kVersion)
        return Error{DOMSYMAS_ERR_BAD_VERSION, "unsupported databank version", 4};
    if (nsec > 64) return Error{DOMSYMAS_ERR_BAD_FLAGS, "absurd section count", 6};
    const size_t directory_end = 8 + static_cast<size_t>(nsec) * kDirEntrySize;
    if (directory_end > p.size())
        return Error{DOMSYMAS_ERR_TRUNCATED, "truncated section directory", 8};
    struct E { uint16_t id, flags; uint32_t off, size, align, crc; };
    std::vector<E> dir;
    for (uint16_t i = 0; i < nsec; i++) {
        E e;
        if (!r.u16(e.id) || !r.u16(e.flags) || !r.u32(e.off) || !r.u32(e.size) ||
            !r.u32(e.align) || !r.u32(e.crc))
            return Error{DOMSYMAS_ERR_TRUNCATED, "truncated section directory", r.pos()};
        if (e.id < 1 || e.id > kSectionMax) {
            // A reader that does not know an id may skip it only when the
            // producer marked the section optional.
            if (!(e.flags & DOMSYMAS_DIRFLAG_OPTIONAL))
                return Error{DOMSYMAS_ERR_BAD_FLAGS, "unknown mandatory section id", r.pos()};
        }
        if (e.align != 1 && e.align != 2 && e.align != 4 && e.align != 8 && e.align != 16)
            return Error{DOMSYMAS_ERR_BAD_ALIGN, "invalid section alignment", r.pos()};
        uint64_t end = (uint64_t)e.off + e.size;
        if (e.off < directory_end || end > p.size() || end < e.off)
            return Error{DOMSYMAS_ERR_BAD_BOUNDS, "section out of bounds", r.pos()};
        if ((e.off % e.align) != 0)
            return Error{DOMSYMAS_ERR_BAD_ALIGN, "section misaligned", e.off};
        dir.push_back(e);
    }
    for (size_t i = 0; i < dir.size(); ++i) {
        for (size_t j = i + 1; j < dir.size(); ++j) {
            if (dir[i].id == dir[j].id)
                return Error{DOMSYMAS_ERR_BAD_BOUNDS, "duplicate section id", dir[j].off};
            if (dir[i].size && dir[j].size && dir[i].off < (uint64_t)dir[j].off + dir[j].size &&
                dir[j].off < (uint64_t)dir[i].off + dir[i].size)
                return Error{DOMSYMAS_ERR_BAD_BOUNDS, "overlapping sections", dir[j].off};
        }
    }
    // STRINGS must come first logically (parse it before other sections).
    const E *strs = nullptr;
    for (auto &e : dir) if (e.id == (uint16_t)Section::Strings) { strs = &e; break; }
    if (!strs) return Error{DOMSYMAS_ERR_BAD_BOUNDS, "missing STRINGS section", 8};
    auto checksum_ok = [&](const E &e) {
        if (e.crc == 0) return true;
        return crc32(p.data() + e.off, e.size) == e.crc;
    };
    if (!checksum_ok(*strs)) return Error{DOMSYMAS_ERR_BAD_CHECKSUM, "STRINGS checksum mismatch", strs->off};
    {
        detail::Reader sr(p.data() + strs->off, strs->size, be);
        std::string dg;
        if (!read_strings(sr, strs->size, d.strings, dg))
            return Error{DOMSYMAS_ERR_BAD_STRING_REF, dg, strs->off};
        if (d.strings.empty() || !d.strings[0].empty())
            return Error{DOMSYMAS_ERR_BAD_STRING_REF, "string table must start with empty string",
                         strs->off};
        if (sr.left()) return Error{DOMSYMAS_ERR_BAD_BOUNDS, "trailing STRINGS data", strs->off + sr.pos()};
    }
    std::string dg;
    for (auto &e : dir) {
        if (e.id == (uint16_t)Section::Strings) continue;
        if (e.id > kSectionMax) continue; // optional section of a newer producer
        if (!checksum_ok(e))
            return Error{DOMSYMAS_ERR_BAD_CHECKSUM, "section checksum mismatch", e.off};
        detail::Reader sr(p.data() + e.off, e.size, be);
        bool ok = false;
        switch ((Section)e.id) {
        case Section::Target: ok = parse::target(sr, d, dg); break;
        case Section::Features: ok = parse::features(sr, d, dg); break;
        case Section::Registers: ok = parse::regs(sr, d, dg); break;
        case Section::Mnemonics: ok = parse::mnemonics(sr, d, dg); break;
        case Section::Opcodes: ok = parse::forms(sr, d, dg); break;
        case Section::Operands: ok = parse::operands(sr, d, dg); break;
        case Section::Encoders: ok = parse::encoders(sr, d, dg); break;
        case Section::Syntax: ok = parse::syntax(sr, d, dg); break;
        case Section::Sections: ok = parse::sections(sr, d, dg); break;
        case Section::Linking: ok = parse::linking(sr, d, dg); break;
        case Section::Relocations: ok = parse::relocs(sr, d, dg); break;
        case Section::Index: ok = parse::index(sr, d, dg); break;
        case Section::Debug: ok = parse::debug(sr, d, dg); break;
        case Section::Isel: ok = parse::isels(sr, d, dg); break;
        case Section::Sched: ok = parse::scheds(sr, d, dg); break;
        case Section::Units: ok = parse::units(sr, d, dg); break;
        case Section::Models: ok = parse::models(sr, d, dg); break;
        case Section::Tuning: ok = parse::tunes(sr, d, dg); break;
        case Section::Fusion: ok = parse::fusions(sr, d, dg); break;
        case Section::Strings: ok = true; break;
        }
        if (!ok) return Error{DOMSYMAS_ERR_BAD_RECORD_REF, dg, e.off};
        if (sr.left()) return Error{DOMSYMAS_ERR_BAD_BOUNDS, "trailing section data", e.off + sr.pos()};
    }
    return Error{};
}

// ---- Databank ----
Error Databank::load_memory(const uint8_t *data, size_t len) {
    DatabankData d;
    Error err;
    FileHeader h;
    if (!read_file_header(data, len, h, err)) { diag_ = err.message; return err; }
    std::vector<uint8_t> payload;
    Error ce = decompress_payload(data + kFileHeaderSize, h.compressed_size,
                                 h.compression, h.uncompressed_size, payload);
    if (!ce.ok()) { diag_ = ce.message; return ce; }
    if ((h.flags & DOMSYMAS_FLAG_CHECKSUM) && crc32(payload.data(), payload.size()) != h.checksum) {
        diag_ = "file checksum mismatch";
        return Error{DOMSYMAS_ERR_BAD_CHECKSUM, diag_, 0};
    }
    d.file_header = h;
    d.big_endian = h.big_endian;
    Error pe = parse_payload(payload, h.big_endian, d);
    if (!pe.ok()) { diag_ = pe.message; return pe; }
    data_ = std::move(d);
    diag_.clear();
    return Error{};
}

Error Databank::load_file(const std::string &path) {
    FILE *f = fopen(path.c_str(), "rb");
    if (!f) return Error{DOMSYMAS_ERR_IO, "cannot open " + path, 0};
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n < 0 || (size_t)n < kFileHeaderSize) {
        fclose(f);
        return Error{DOMSYMAS_ERR_TRUNCATED, "file too small", 0};
    }
    if (n > (long)(65u << 20)) { fclose(f); return Error{DOMSYMAS_ERR_OVERFLOW, "file too large", 0}; }
    std::vector<uint8_t> buf((size_t)n);
    if (fread(buf.data(), 1, buf.size(), f) != buf.size()) {
        fclose(f);
        return Error{DOMSYMAS_ERR_IO, "read failed", 0};
    }
    fclose(f);
    return load_memory(buf.data(), buf.size());
}

Error Databank::to_bytes(std::vector<uint8_t> &out) const {
    std::vector<uint8_t> payload;
    Error be = build_payload(data_, data_.big_endian, payload);
    if (!be.ok()) return be;
    FileHeader h = data_.file_header;
    h.version = kVersion;
    h.big_endian = data_.big_endian;
    h.uncompressed_size = (uint32_t)payload.size();
    h.checksum = crc32(payload.data(), payload.size());
    h.flags |= (uint32_t)(DOMSYMAS_FLAG_BODY_RELATIVE | DOMSYMAS_FLAG_SHARED_STRINGS |
                          DOMSYMAS_FLAG_CHECKSUM);
    Compression want = data_.file_header.compression;
    std::vector<uint8_t> comp;
    Error ce = compress_payload(payload, want, comp);
    if (!ce.ok()) return ce;
    // compress_payload stores uncompressed on fallback (zstd w/o runtime);
    // zlib output never byte-equals its input, so equality implies fallback.
    h.compression = (want != Compression::None && comp == payload) ? Compression::None : want;
    h.compressed_size = (uint32_t)comp.size();
    out.clear();
    write_file_header(h, out);
    out.insert(out.end(), comp.begin(), comp.end());
    return Error{};
}

Error Databank::save_file(const std::string &path) const {
    std::vector<uint8_t> buf;
    Error e = to_bytes(buf);
    if (!e.ok()) return e;
    FILE *f = fopen(path.c_str(), "wb");
    if (!f) return Error{DOMSYMAS_ERR_IO, "cannot write " + path, 0};
    bool ok = fwrite(buf.data(), 1, buf.size(), f) == buf.size();
    fclose(f);
    if (!ok) return Error{DOMSYMAS_ERR_IO, "write failed", 0};
    return Error{};
}

// ---- Builder ----
Builder::Builder(bool little_endian) : little_endian_(little_endian) {
    data_.strings.push_back("");
    data_.file_header.compression = Compression::Zlib;
    data_.big_endian = !little_endian;
}
uint32_t Builder::add_target(TargetRec t) {
    t.id = (uint32_t)data_.targets.size();
    data_.targets.push_back(std::move(t));
    return t.id;
}
uint32_t Builder::add_feature(const std::string &n, uint32_t f, const std::string &ds) {
    FeatureRec r; r.id = (uint32_t)data_.features.size(); r.name = n; r.flags = f; r.desc = ds;
    data_.features.push_back(std::move(r));
    return r.id;
}
uint32_t Builder::add_register(RegisterRec r) {
    r.id = (uint32_t)data_.registers.size();
    data_.registers.push_back(std::move(r));
    return r.id;
}
uint32_t Builder::add_mnemonic(const std::string &c, const std::vector<std::string> &a, uint32_t f) {
    MnemonicRec m; m.id = (uint32_t)data_.mnemonics.size();
    m.canonical = c; m.aliases = a; m.flags = f;
    data_.mnemonics.push_back(std::move(m));
    return m.id;
}
uint32_t Builder::add_form(uint32_t mn, uint32_t oc, uint32_t fl, uint32_t enc,
                           const std::vector<uint32_t> &rq, const std::vector<uint32_t> &fb,
                           const std::vector<uint32_t> &sy) {
    FormRec f; f.id = (uint32_t)data_.forms.size();
    f.mnemonic = mn; f.operand_count = oc; f.flags = fl; f.encoder = enc;
    f.required = rq; f.forbidden = fb; f.syntaxes = sy;
    data_.forms.push_back(std::move(f));
    return f.id;
}
uint32_t Builder::add_operand(OperandRec o) {
    o.id = (uint32_t)data_.operands.size();
    data_.operands.push_back(std::move(o));
    return o.id;
}
uint32_t Builder::add_encoder(const std::string &n, const std::string &s,
                              uint32_t f, uint32_t mb, uint32_t mr) {
    EncoderRec e; e.id = (uint32_t)data_.encoders.size();
    e.name = n; e.source = s; e.flags = f; e.max_bytes = mb; e.max_relocs = mr;
    data_.encoders.push_back(std::move(e));
    return e.id;
}
uint32_t Builder::add_syntax(SyntaxRec s) {
    s.id = (uint32_t)data_.syntaxes.size();
    data_.syntaxes.push_back(std::move(s));
    return s.id;
}
void Builder::link_syntax_form(uint32_t sy, uint32_t f) {
    if (sy < data_.syntaxes.size()) data_.syntaxes[sy].forms.push_back(f);
    if (f < data_.forms.size()) data_.forms[f].syntaxes.push_back(sy);
}
void Builder::link_mnemonic_form(uint32_t mn, uint32_t f) {
    if (mn < data_.mnemonics.size()) data_.mnemonics[mn].forms.push_back(f);
}
uint32_t Builder::add_section(const std::string &n, uint32_t f, uint32_t a,
                              uint32_t as, uint32_t c) {
    SectionRec s; s.id = (uint32_t)data_.sections.size();
    s.name = n; s.flags = f; s.align = a; s.addrspace = as; s.content = c;
    data_.sections.push_back(std::move(s));
    return s.id;
}
uint32_t Builder::add_linkage(const std::string &n, uint32_t b, uint32_t v,
                              uint32_t t, uint32_t f) {
    LinkageRec l; l.id = (uint32_t)data_.linkages.size();
    l.name = n; l.binding = b; l.visibility = v; l.symtype = t; l.flags = f;
    data_.linkages.push_back(std::move(l));
    return l.id;
}
uint32_t Builder::add_reloc(const std::string &n, uint32_t w, uint32_t o,
                            uint32_t sg, uint32_t pc, uint32_t ov, uint32_t ad,
                            const std::string &e) {
    RelocRec r; r.id = (uint32_t)data_.relocs.size();
    r.name = n; r.width = w; r.offset = o; r.is_signed = sg; r.pcrel = pc;
    r.overflow = ov; r.addend = ad; r.expr = e;
    data_.relocs.push_back(std::move(r));
    return r.id;
}
uint32_t Builder::add_debug(const std::string &m, uint32_t k, uint32_t id) {
    DebugRec g; g.id = (uint32_t)data_.debug.size(); g.msg = m; g.ref_kind = k; g.ref_id = id;
    data_.debug.push_back(std::move(g));
    return g.id;
}
uint32_t Builder::add_isel(IselRec r) {
    r.id = (uint32_t)data_.isels.size();
    data_.isels.push_back(std::move(r));
    return data_.isels.back().id;
}
uint32_t Builder::add_sched(SchedRec r) {
    r.id = (uint32_t)data_.scheds.size();
    data_.scheds.push_back(std::move(r));
    return data_.scheds.back().id;
}
uint32_t Builder::add_unit(UnitRec r) {
    r.id = (uint32_t)data_.units.size();
    data_.units.push_back(std::move(r));
    return data_.units.back().id;
}
uint32_t Builder::add_model(ModelRec r) {
    r.id = (uint32_t)data_.models.size();
    data_.models.push_back(std::move(r));
    return data_.models.back().id;
}
uint32_t Builder::add_tune(TuneRec r) {
    r.id = (uint32_t)data_.tunes.size();
    data_.tunes.push_back(std::move(r));
    return data_.tunes.back().id;
}
uint32_t Builder::add_fusion(FusionRec r) {
    r.id = (uint32_t)data_.fusions.size();
    data_.fusions.push_back(std::move(r));
    return data_.fusions.back().id;
}
Error Builder::build(Databank &out) const {
    DatabankData d = data_;
    d.big_endian = !little_endian_;
    d.file_header.big_endian = d.big_endian;
    d.file_header.compression = compression_;
    // Auto-build INDEX from names (optimization only; never changes meaning).
    d.index.clear();
    uint32_t seq = 0;
    auto put = [&](uint32_t kind, const std::string &key, uint32_t rec) {
        d.index.push_back(IndexRec{seq++, kind, key, rec});
    };
    for (auto &m : d.mnemonics) {
        put(0, m.canonical, m.id);
        for (auto &a : m.aliases) put(0, a, m.id);
    }
    for (auto &r : d.registers) put(1, r.name, r.id);
    for (auto &f : d.features) put(2, f.name, f.id);
    for (auto &s : d.syntaxes) put(3, s.name, s.id);
    for (auto &r : d.relocs) put(4, r.name, r.id);
    for (auto &s : d.sections) put(5, s.name, s.id);
    for (auto &m : d.models) put(6, m.name, m.id);
    for (auto &v : d.isels) if (!v.node.empty()) put(7, v.node, v.id);
    for (auto &v : d.units) if (!v.name.empty()) put(8, v.name, v.id);
    for (auto &v : d.scheds) if (!v.name.empty()) put(9, v.name, v.id);
    for (auto &v : d.fusions) if (!v.name.empty()) put(10, v.name, v.id);
    if (!d.index.empty())
        d.file_header.flags |= DOMSYMAS_FLAG_HAS_INDEX;
    std::string dg;
    Databank tmp;
    tmp.mutable_data() = d;
    Error ve = tmp.validate(true, &dg);
    if (!ve.ok()) { ve.message = dg.empty() ? ve.message : dg; return ve; }
    out.mutable_data() = std::move(d);
    return Error{};
}
Error Builder::save_file(const std::string &path) const {
    Databank b;
    Error e = build(b);
    if (!e.ok()) return e;
    return b.save_file(path);
}

} // namespace domsymas
