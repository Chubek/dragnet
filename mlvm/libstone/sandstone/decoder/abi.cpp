// DomSymAs C ABI: explicit ownership per sandstone.h.
// Borrowed record views are cached beside the Databank and valid until close.
#include "sandstone.h"
#include "sandstone.hpp"

#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

struct DomSymAs_Bank {
    domsymas::Databank bank;
    std::string last_error;
    // Cached C views (borrowed pointers, valid until close).
    std::vector<DomSymAs_Target> targets;
    std::vector<DomSymAs_Feature> features;
    std::vector<DomSymAs_Register> registers;
    std::vector<DomSymAs_Mnemonic> mnemonics;
    std::vector<const char *> alias_ptrs;
    std::vector<uint32_t> id_store; // concatenated id arrays
    std::vector<DomSymAs_Form> forms;
    std::vector<std::vector<DomSymAs_Operand>> operands; // per form
    std::vector<DomSymAs_Encoder> encoders;
    std::vector<DomSymAs_Syntax> syntaxes;
    std::vector<DomSymAs_SectionInfo> sections;
    std::vector<DomSymAs_Linkage> linkages;
    std::vector<DomSymAs_Reloc> relocs;
    // Optional optimization sections (format 1.1+).
    std::vector<DomSymAs_Isel> isels;
    std::vector<DomSymAs_Sched> scheds;
    std::vector<DomSymAs_Unit> units;
    std::vector<DomSymAs_Model> models;
    std::vector<DomSymAs_Tune> tunes;
    std::vector<DomSymAs_Fusion> fusions;
};

struct DomSymAs_Builder {
    domsymas::Builder impl;
    explicit DomSymAs_Builder(bool le) : impl(le) {}
};

static void rebuild_cache(DomSymAs_Bank *b) {
    const domsymas::DatabankData &d = b->bank.data();
    b->targets.clear(); b->features.clear(); b->registers.clear();
    b->mnemonics.clear(); b->alias_ptrs.clear(); b->id_store.clear();
    b->forms.clear(); b->operands.clear(); b->encoders.clear();
    b->syntaxes.clear(); b->sections.clear(); b->linkages.clear(); b->relocs.clear();
    b->isels.clear(); b->scheds.clear(); b->units.clear(); b->models.clear();
    b->tunes.clear(); b->fusions.clear();
    size_t ids = 0, aliases = 0;
    for (const auto &t : d.targets) ids += t.features.size();
    for (const auto &m : d.mnemonics) { ids += m.forms.size(); aliases += m.aliases.size(); }
    for (const auto &f : d.forms)
        ids += f.required.size() + f.forbidden.size() + f.syntaxes.size();
    for (const auto &s : d.syntaxes) ids += s.forms.size();
    for (const auto &v : d.isels) ids += v.required.size() + v.forbidden.size() + v.alts.size();
    for (const auto &v : d.scheds) ids += v.writes.size() + v.reads.size();
    for (const auto &v : d.models) ids += v.features.size() + v.tune_features.size();
    for (const auto &v : d.tunes) ids += v.features.size();
    for (const auto &v : d.fusions) ids += v.features.size();
    b->id_store.reserve(ids);
    b->alias_ptrs.reserve(aliases);
    for (auto &t : d.targets) {
        DomSymAs_Target c{};
        c.id = t.id; c.arch = t.arch.c_str(); c.subarch = t.subarch.c_str();
        c.addr_size = t.addr_size; c.default_op_size = t.default_op_size;
        c.default_addr_size = t.default_addr_size; c.instr_align = t.instr_align;
        c.byte_order = t.byte_order; c.obj_format = t.obj_format.c_str();
        c.abi = t.abi.c_str(); c.calling_conv = t.calling_conv.c_str();
        size_t base = b->id_store.size();
        b->id_store.insert(b->id_store.end(), t.features.begin(), t.features.end());
        c.features = t.features.empty() ? nullptr : &b->id_store[base];
        c.feature_count = (uint32_t)t.features.size();
        b->targets.push_back(c);
    }
    for (auto &f : d.features) {
        DomSymAs_Feature c{};
        c.id = f.id; c.name = f.name.c_str(); c.flags = f.flags; c.desc = f.desc.c_str();
        b->features.push_back(c);
    }
    for (auto &r : d.registers) {
        DomSymAs_Register c{};
        c.id = r.id; c.name = r.name.c_str(); c.regclass = r.regclass;
        c.width = r.width; c.enc_num = r.enc_num; c.parent = r.parent;
        c.sub_lo = r.sub_lo; c.sub_hi = r.sub_hi; c.flags = r.flags;
        b->registers.push_back(c);
    }
    for (auto &m : d.mnemonics) {
        DomSymAs_Mnemonic c{};
        c.id = m.id; c.canonical = m.canonical.c_str(); c.flags = m.flags;
        size_t ab = b->alias_ptrs.size();
        for (auto &a : m.aliases) b->alias_ptrs.push_back(a.c_str());
        c.aliases = m.aliases.empty() ? nullptr : &b->alias_ptrs[ab];
        c.alias_count = (uint32_t)m.aliases.size();
        size_t fb = b->id_store.size();
        b->id_store.insert(b->id_store.end(), m.forms.begin(), m.forms.end());
        c.forms = m.forms.empty() ? nullptr : &b->id_store[fb];
        c.form_count = (uint32_t)m.forms.size();
        b->mnemonics.push_back(c);
    }
    for (auto &f : d.forms) {
        DomSymAs_Form c{};
        c.id = f.id; c.mnemonic = f.mnemonic; c.operand_count = f.operand_count;
        c.flags = f.flags; c.encoder = f.encoder;
        size_t b0 = b->id_store.size();
        b->id_store.insert(b->id_store.end(), f.required.begin(), f.required.end());
        c.required_features = f.required.empty() ? nullptr : &b->id_store[b0];
        c.required_count = (uint32_t)f.required.size();
        b0 = b->id_store.size();
        b->id_store.insert(b->id_store.end(), f.forbidden.begin(), f.forbidden.end());
        c.forbidden_features = f.forbidden.empty() ? nullptr : &b->id_store[b0];
        c.forbidden_count = (uint32_t)f.forbidden.size();
        b0 = b->id_store.size();
        b->id_store.insert(b->id_store.end(), f.syntaxes.begin(), f.syntaxes.end());
        c.syntaxes = f.syntaxes.empty() ? nullptr : &b->id_store[b0];
        c.syntax_count = (uint32_t)f.syntaxes.size();
        b->forms.push_back(c);
        std::vector<DomSymAs_Operand> ops;
        for (auto *o : b->bank.operands_for_form(f.id)) {
            DomSymAs_Operand c2{};
            c2.id = o->id; c2.form = o->form; c2.position = o->position;
            c2.kind = o->kind; c2.width = o->width; c2.regclass = o->regclass;
            c2.imm_min = o->imm_min; c2.imm_max = o->imm_max;
            c2.is_signed = o->is_signed; c2.enc_role = o->enc_role;
            c2.access = o->access; c2.hidden = o->hidden;
            c2.constraint = o->constraint.c_str();
            ops.push_back(c2);
        }
        b->operands.push_back(std::move(ops));
    }
    for (auto &e : d.encoders) {
        DomSymAs_Encoder c{};
        c.id = e.id; c.name = e.name.c_str(); c.source = e.source.c_str();
        c.flags = e.flags; c.max_bytes = e.max_bytes; c.max_relocs = e.max_relocs;
        b->encoders.push_back(c);
    }
    for (auto &s : d.syntaxes) {
        DomSymAs_Syntax c{};
        c.id = s.id; c.name = s.name.c_str(); c.operand_order = s.operand_order;
        c.reg_prefix = s.reg_prefix.c_str(); c.imm_prefix = s.imm_prefix.c_str();
        c.comment = s.comment.c_str(); c.separator = s.separator.c_str();
        c.case_sensitive = s.case_sensitive; c.tmpl = s.tmpl.c_str();
        size_t fb = b->id_store.size();
        b->id_store.insert(b->id_store.end(), s.forms.begin(), s.forms.end());
        c.forms = s.forms.empty() ? nullptr : &b->id_store[fb];
        c.form_count = (uint32_t)s.forms.size();
        b->syntaxes.push_back(c);
    }
    for (auto &s : d.sections) {
        DomSymAs_SectionInfo c{};
        c.id = s.id; c.name = s.name.c_str(); c.flags = s.flags;
        c.align = s.align; c.addrspace = s.addrspace; c.content = s.content;
        b->sections.push_back(c);
    }
    for (auto &l : d.linkages) {
        DomSymAs_Linkage c{};
        c.id = l.id; c.name = l.name.c_str(); c.binding = l.binding;
        c.visibility = l.visibility; c.symtype = l.symtype; c.flags = l.flags;
        b->linkages.push_back(c);
    }
    for (auto &r : d.relocs) {
        DomSymAs_Reloc c{};
        c.id = r.id; c.name = r.name.c_str(); c.width = r.width;
        c.offset = r.offset; c.is_signed = r.is_signed; c.pcrel = r.pcrel;
        c.overflow = r.overflow; c.addend = r.addend; c.expr = r.expr.c_str();
        b->relocs.push_back(c);
    }
    for (auto &v : d.isels) {
        DomSymAs_Isel c{};
        c.id = v.id; c.kind = v.kind; c.node = v.node.c_str(); c.form = v.form;
        c.cost = v.cost; c.complexity = v.complexity; c.priority = v.priority;
        c.flags = v.flags;
        size_t b0 = b->id_store.size();
        b->id_store.insert(b->id_store.end(), v.required.begin(), v.required.end());
        c.required_features = v.required.empty() ? nullptr : &b->id_store[b0];
        c.required_count = (uint32_t)v.required.size();
        b0 = b->id_store.size();
        b->id_store.insert(b->id_store.end(), v.forbidden.begin(), v.forbidden.end());
        c.forbidden_features = v.forbidden.empty() ? nullptr : &b->id_store[b0];
        c.forbidden_count = (uint32_t)v.forbidden.size();
        b0 = b->id_store.size();
        b->id_store.insert(b->id_store.end(), v.alts.begin(), v.alts.end());
        c.alts = v.alts.empty() ? nullptr : &b->id_store[b0];
        c.alt_count = (uint32_t)v.alts.size();
        c.pattern = v.pattern.c_str();
        b->isels.push_back(c);
    }
    for (auto &v : d.scheds) {
        DomSymAs_Sched c{};
        c.id = v.id; c.form = v.form; c.name = v.name.c_str();
        c.latency = v.latency; c.uops = v.uops; c.cycles = v.cycles; c.flags = v.flags;
        size_t b0 = b->id_store.size();
        b->id_store.insert(b->id_store.end(), v.writes.begin(), v.writes.end());
        c.writes = v.writes.empty() ? nullptr : &b->id_store[b0];
        c.write_count = (uint32_t)v.writes.size();
        b0 = b->id_store.size();
        b->id_store.insert(b->id_store.end(), v.reads.begin(), v.reads.end());
        c.reads = v.reads.empty() ? nullptr : &b->id_store[b0];
        c.read_count = (uint32_t)v.reads.size();
        b->scheds.push_back(c);
    }
    for (auto &v : d.units) {
        DomSymAs_Unit c{};
        c.id = v.id; c.model = v.model; c.name = v.name.c_str(); c.kind = v.kind;
        c.count = v.count; c.latency = v.latency; c.throughput = v.throughput;
        c.flags = v.flags;
        b->units.push_back(c);
    }
    for (auto &v : d.models) {
        DomSymAs_Model c{};
        c.id = v.id; c.name = v.name.c_str(); c.vendor = v.vendor.c_str();
        c.issue_width = v.issue_width; c.micro_op_buffer = v.micro_op_buffer;
        c.loop_buffer = v.loop_buffer; c.load_latency = v.load_latency;
        c.mispredict_penalty = v.mispredict_penalty; c.high_latency = v.high_latency;
        c.high_uops = v.high_uops; c.flags = v.flags;
        size_t b0 = b->id_store.size();
        b->id_store.insert(b->id_store.end(), v.features.begin(), v.features.end());
        c.features = v.features.empty() ? nullptr : &b->id_store[b0];
        c.feature_count = (uint32_t)v.features.size();
        b0 = b->id_store.size();
        b->id_store.insert(b->id_store.end(), v.tune_features.begin(), v.tune_features.end());
        c.tune_features = v.tune_features.empty() ? nullptr : &b->id_store[b0];
        c.tune_feature_count = (uint32_t)v.tune_features.size();
        c.desc = v.desc.c_str();
        b->models.push_back(c);
    }
    for (auto &v : d.tunes) {
        DomSymAs_Tune c{};
        c.id = v.id; c.model = v.model; c.kind = v.kind; c.name = v.name.c_str();
        c.form = v.form; c.form2 = v.form2; c.value = v.value; c.flags = v.flags;
        size_t b0 = b->id_store.size();
        b->id_store.insert(b->id_store.end(), v.features.begin(), v.features.end());
        c.features = v.features.empty() ? nullptr : &b->id_store[b0];
        c.feature_count = (uint32_t)v.features.size();
        c.desc = v.desc.c_str();
        b->tunes.push_back(c);
    }
    for (auto &v : d.fusions) {
        DomSymAs_Fusion c{};
        c.id = v.id; c.name = v.name.c_str(); c.a = v.a; c.b = v.b;
        c.kind = v.kind; c.latency = v.latency; c.flags = v.flags;
        size_t b0 = b->id_store.size();
        b->id_store.insert(b->id_store.end(), v.features.begin(), v.features.end());
        c.features = v.features.empty() ? nullptr : &b->id_store[b0];
        c.feature_count = (uint32_t)v.features.size();
        c.pattern = v.pattern.c_str();
        b->fusions.push_back(c);
    }
}

extern "C" {

const char *domsymas_version(void) { return "1.1.0"; }
const char *domsymas_error_string(DomSymAs_Error e) { return domsymas::error_string(e); }
uint32_t domsymas_magic(void) { return DOMSYMAS_MAGIC; }

DomSymAs_Error domsymas_open_file(const char *path, DomSymAs_Bank **out) {
    if (!path || !out) return DOMSYMAS_ERR_IO;
    DomSymAs_Bank *b = new (std::nothrow) DomSymAs_Bank();
    if (!b) return DOMSYMAS_ERR_NOMEM;
    domsymas::Error e = b->bank.load_file(path);
    if (!e.ok()) { b->last_error = e.message; delete b; return e.code; }
    rebuild_cache(b);
    *out = b;
    return DOMSYMAS_OK;
}
DomSymAs_Error domsymas_open_memory(const uint8_t *data, size_t len, DomSymAs_Bank **out) {
    if (!data || !out) return DOMSYMAS_ERR_IO;
    DomSymAs_Bank *b = new (std::nothrow) DomSymAs_Bank();
    if (!b) return DOMSYMAS_ERR_NOMEM;
    domsymas::Error e = b->bank.load_memory(data, len);
    if (!e.ok()) { b->last_error = e.message; delete b; return e.code; }
    rebuild_cache(b);
    *out = b;
    return DOMSYMAS_OK;
}
void domsymas_close(DomSymAs_Bank *bank) { delete bank; }
const char *domsymas_last_error(const DomSymAs_Bank *bank) {
    if (!bank) return "null bank";
    if (!bank->last_error.empty()) return bank->last_error.c_str();
    return domsymas::error_string(DOMSYMAS_OK);
}
DomSymAs_Error domsymas_validate(DomSymAs_Bank *bank, int semantic) {
    if (!bank) return DOMSYMAS_ERR_IO;
    std::string dg;
    domsymas::Error e = bank->bank.validate(semantic != 0, &dg);
    bank->last_error = dg;
    return e.code;
}
DomSymAs_Error domsymas_header_info(const DomSymAs_Bank *bank, uint16_t *ver,
                                    uint16_t *flags, uint8_t *comp,
                                    uint32_t *uncomp, uint32_t *comp_sz) {
    if (!bank) return DOMSYMAS_ERR_IO;
    const domsymas::FileHeader &h = bank->bank.data().file_header;
    if (ver) *ver = h.version;
    if (flags) *flags = h.flags;
    if (comp) *comp = (uint8_t)h.compression;
    if (uncomp) *uncomp = h.uncompressed_size;
    if (comp_sz) *comp_sz = h.compressed_size;
    return DOMSYMAS_OK;
}
uint32_t domsymas_string_count(const DomSymAs_Bank *bank) {
    return bank ? (uint32_t)bank->bank.data().strings.size() : 0;
}
const char *domsymas_string_at(const DomSymAs_Bank *bank, uint32_t i) {
    if (!bank || i >= bank->bank.data().strings.size()) return nullptr;
    return bank->bank.data().strings[i].c_str();
}
uint32_t domsymas_target_count(const DomSymAs_Bank *bank) {
    return bank ? (uint32_t)bank->targets.size() : 0;
}
const DomSymAs_Target *domsymas_target_at(const DomSymAs_Bank *bank, uint32_t i) {
    return (bank && i < bank->targets.size()) ? &bank->targets[i] : nullptr;
}
uint32_t domsymas_feature_count(const DomSymAs_Bank *bank) {
    return bank ? (uint32_t)bank->features.size() : 0;
}
const DomSymAs_Feature *domsymas_feature_at(const DomSymAs_Bank *bank, uint32_t i) {
    return (bank && i < bank->features.size()) ? &bank->features[i] : nullptr;
}
const DomSymAs_Feature *domsymas_find_feature(const DomSymAs_Bank *bank, const char *n) {
    if (!bank || !n) return nullptr;
    const domsymas::FeatureRec *f = bank->bank.find_feature(n);
    if (!f) return nullptr;
    for (auto &c : bank->features) if (c.id == f->id) return &c;
    return nullptr;
}
uint32_t domsymas_register_count(const DomSymAs_Bank *bank) {
    return bank ? (uint32_t)bank->registers.size() : 0;
}
const DomSymAs_Register *domsymas_register_at(const DomSymAs_Bank *bank, uint32_t i) {
    return (bank && i < bank->registers.size()) ? &bank->registers[i] : nullptr;
}
const DomSymAs_Register *domsymas_find_register(const DomSymAs_Bank *bank, const char *n) {
    if (!bank || !n) return nullptr;
    const domsymas::RegisterRec *r = bank->bank.find_register(n);
    if (!r) return nullptr;
    for (auto &c : bank->registers) if (c.id == r->id) return &c;
    return nullptr;
}
uint32_t domsymas_mnemonic_count(const DomSymAs_Bank *bank) {
    return bank ? (uint32_t)bank->mnemonics.size() : 0;
}
const DomSymAs_Mnemonic *domsymas_mnemonic_at(const DomSymAs_Bank *bank, uint32_t i) {
    return (bank && i < bank->mnemonics.size()) ? &bank->mnemonics[i] : nullptr;
}
DomSymAs_Error domsymas_find_mnemonic(const DomSymAs_Bank *bank, const char *name,
                                      uint32_t *out, uint32_t cap, uint32_t *out_n) {
    if (!bank || !name || !out_n || (cap && !out)) return DOMSYMAS_ERR_IO;
    std::vector<uint32_t> ids = bank->bank.forms_for_mnemonic(name);
    // forms_for_mnemonic returns form ids; for the C API return the forms too,
    // but ALSO handle direct id lookup through the mnemonic record:
    *out_n = 0;
    if (ids.empty()) return DOMSYMAS_ERR_NOT_FOUND;
    uint32_t n = ids.size() < cap ? (uint32_t)ids.size() : cap;
    for (uint32_t i = 0; i < n; i++) out[i] = ids[i];
    *out_n = n;
    return DOMSYMAS_OK;
}
uint32_t domsymas_form_count(const DomSymAs_Bank *bank) {
    return bank ? (uint32_t)bank->forms.size() : 0;
}
const DomSymAs_Form *domsymas_form_at(const DomSymAs_Bank *bank, uint32_t i) {
    return (bank && i < bank->forms.size()) ? &bank->forms[i] : nullptr;
}
const DomSymAs_Form *domsymas_find_form(const DomSymAs_Bank *bank, uint32_t id) {
    if (!bank) return nullptr;
    for (auto &f : bank->forms) if (f.id == id) return &f;
    return nullptr;
}
uint32_t domsymas_operand_count(const DomSymAs_Bank *bank, uint32_t form_id) {
    if (!bank) return 0;
    for (auto &f : bank->forms)
        if (f.id == form_id) {
            for (auto &v : bank->operands)
                if (!v.empty() && v[0].form == form_id) return (uint32_t)v.size();
            return 0;
        }
    return 0;
}
const DomSymAs_Operand *domsymas_operand_at(const DomSymAs_Bank *bank, uint32_t form_id,
                                            uint32_t i) {
    if (!bank) return nullptr;
    for (auto &v : bank->operands)
        if (!v.empty() && v[0].form == form_id && i < v.size()) return &v[i];
    return nullptr;
}
const DomSymAs_Encoder *domsymas_find_encoder(const DomSymAs_Bank *bank, uint32_t id) {
    if (!bank) return nullptr;
    for (auto &e : bank->encoders) if (e.id == id) return &e;
    return nullptr;
}
const char *domsymas_encoder_source(const DomSymAs_Bank *bank, uint32_t id) {
    const DomSymAs_Encoder *e = domsymas_find_encoder(bank, id);
    return e ? e->source : nullptr;
}
uint32_t domsymas_syntax_count(const DomSymAs_Bank *bank) {
    return bank ? (uint32_t)bank->syntaxes.size() : 0;
}
const DomSymAs_Syntax *domsymas_syntax_at(const DomSymAs_Bank *bank, uint32_t i) {
    return (bank && i < bank->syntaxes.size()) ? &bank->syntaxes[i] : nullptr;
}
const DomSymAs_Syntax *domsymas_find_syntax(const DomSymAs_Bank *bank, const char *n) {
    if (!bank || !n) return nullptr;
    const domsymas::SyntaxRec *s = bank->bank.find_syntax(n);
    if (!s) return nullptr;
    for (auto &c : bank->syntaxes) if (c.id == s->id) return &c;
    return nullptr;
}
uint32_t domsymas_section_count(const DomSymAs_Bank *bank) {
    return bank ? (uint32_t)bank->sections.size() : 0;
}
const DomSymAs_SectionInfo *domsymas_section_at(const DomSymAs_Bank *bank, uint32_t i) {
    return (bank && i < bank->sections.size()) ? &bank->sections[i] : nullptr;
}
const DomSymAs_SectionInfo *domsymas_find_section(const DomSymAs_Bank *bank, const char *n) {
    if (!bank || !n) return nullptr;
    for (auto &s : bank->sections) if (s.name && !strcmp(s.name, n)) return &s;
    return nullptr;
}
uint32_t domsymas_linkage_count(const DomSymAs_Bank *bank) {
    return bank ? (uint32_t)bank->linkages.size() : 0;
}
const DomSymAs_Linkage *domsymas_linkage_at(const DomSymAs_Bank *bank, uint32_t i) {
    return (bank && i < bank->linkages.size()) ? &bank->linkages[i] : nullptr;
}
uint32_t domsymas_reloc_count(const DomSymAs_Bank *bank) {
    return bank ? (uint32_t)bank->relocs.size() : 0;
}
const DomSymAs_Reloc *domsymas_reloc_at(const DomSymAs_Bank *bank, uint32_t i) {
    return (bank && i < bank->relocs.size()) ? &bank->relocs[i] : nullptr;
}
const DomSymAs_Reloc *domsymas_find_reloc(const DomSymAs_Bank *bank, const char *n) {
    if (!bank || !n) return nullptr;
    for (auto &r : bank->relocs) if (r.name && !strcmp(r.name, n)) return &r;
    return nullptr;
}
DomSymAs_Error domsymas_index_lookup(const DomSymAs_Bank *bank, uint32_t kind,
                                     const char *key, uint32_t *record_id) {
    if (!bank || !key || !record_id) return DOMSYMAS_ERR_IO;
    uint32_t r = 0;
    if (!bank->bank.index_lookup(kind, key, r)) return DOMSYMAS_ERR_NOT_FOUND;
    *record_id = r;
    return DOMSYMAS_OK;
}
DomSymAs_Error domsymas_resolve(const DomSymAs_Bank *bank, const char *mnemonic,
                                uint32_t op_count, const uint32_t *enabled,
                                uint32_t enabled_n, uint32_t *out, uint32_t cap,
                                uint32_t *out_n) {
    if (!bank || !mnemonic || !out_n || (enabled_n && !enabled) || (cap && !out)) return DOMSYMAS_ERR_IO;
    std::vector<uint32_t> en;
    for (uint32_t i = 0; i < enabled_n; i++) en.push_back(enabled[i]);
    std::vector<uint32_t> ids = bank->bank.resolve(mnemonic, op_count, en);
    if (ids.empty()) return DOMSYMAS_ERR_NOT_FOUND;
    uint32_t n = ids.size() < cap ? (uint32_t)ids.size() : cap;
    for (uint32_t i = 0; i < n; i++) out[i] = ids[i];
    *out_n = n;
    return DOMSYMAS_OK;
}
DomSymAs_Error domsymas_encode(const DomSymAs_Bank *bank, uint32_t form_id,
                               const DomSymAs_OpVal *ops, uint32_t op_n,
                               DomSymAs_Bytes *out_bytes,
                               DomSymAs_RelocOut **out_relocs, uint32_t *out_reloc_n) {
    if (!bank || !out_bytes || !out_relocs || !out_reloc_n || (op_n && !ops)) return DOMSYMAS_ERR_IO;
    std::map<std::string, domsymas::encoding::Value> m;
    for (uint32_t i = 0; i < op_n; i++) {
        domsymas::encoding::Value v;
        if (ops[i].is_symbol) { v.is_str = true; v.str = ops[i].sval ? ops[i].sval : ""; }
        else { v.is_str = false; v.num = ops[i].ival; }
        m[ops[i].name ? ops[i].name : ""] = v;
    }
    domsymas::Databank::EncodeResult r;
    domsymas::Error e = bank->bank.encode(form_id, m, r);
    if (!e.ok()) return e.code;
    out_bytes->data = nullptr; out_bytes->len = 0;
    if (!r.bytes.empty()) {
        out_bytes->data = (uint8_t *)malloc(r.bytes.size());
        if (!out_bytes->data) return DOMSYMAS_ERR_NOMEM;
        memcpy(out_bytes->data, r.bytes.data(), r.bytes.size());
        out_bytes->len = r.bytes.size();
    }
    *out_relocs = nullptr; *out_reloc_n = 0;
    if (!r.relocs.empty()) {
        DomSymAs_RelocOut *arr = (DomSymAs_RelocOut *)calloc(r.relocs.size(), sizeof(*arr));
        if (!arr) { free(out_bytes->data); out_bytes->data = nullptr; return DOMSYMAS_ERR_NOMEM; }
        for (size_t i = 0; i < r.relocs.size(); i++) {
            arr[i].type = r.relocs[i].type;
            arr[i].offset = r.relocs[i].offset;
            arr[i].addend = r.relocs[i].addend;
            arr[i].symbol = strdup(r.relocs[i].symbol.c_str());
            if (!arr[i].symbol) {
                domsymas_free_relocs(arr, (uint32_t)r.relocs.size());
                domsymas_free_bytes(out_bytes);
                return DOMSYMAS_ERR_NOMEM;
            }
        }
        *out_relocs = arr; *out_reloc_n = (uint32_t)r.relocs.size();
    }
    return DOMSYMAS_OK;
}
DomSymAs_Error domsymas_disassemble(const DomSymAs_Bank *bank, const uint8_t *code,
                                    size_t len, uint32_t syntax_id,
                                    DomSymAs_Candidate **out, uint32_t *out_n) {
    if (!bank || !code || !out || !out_n) return DOMSYMAS_ERR_IO;
    std::vector<domsymas::Databank::DisCandidate> c;
    domsymas::Error e = bank->bank.disassemble(code, len, syntax_id, c);
    if (!e.ok()) return e.code;
    *out = nullptr; *out_n = 0;
    if (c.empty()) return DOMSYMAS_ERR_NOT_FOUND;
    DomSymAs_Candidate *arr = (DomSymAs_Candidate *)calloc(c.size(), sizeof(*arr));
    if (!arr) return DOMSYMAS_ERR_NOMEM;
    for (size_t i = 0; i < c.size(); i++) {
        arr[i].form = c[i].form;
        // mnemonic text: look up canonical from cached forms/mnemonics
        arr[i].mnemonic = "";
        for (auto &m : bank->mnemonics)
            for (uint32_t k = 0; k < m.form_count; k++)
                if (m.forms && m.forms[k] == c[i].form) arr[i].mnemonic = m.canonical;
        arr[i].consumed = c[i].consumed;
        arr[i].text = strdup(c[i].text.c_str());
        if (!arr[i].text) {
            domsymas_free_candidates(arr, (uint32_t)c.size());
            return DOMSYMAS_ERR_NOMEM;
        }
    }
    *out = arr; *out_n = (uint32_t)c.size();
    return DOMSYMAS_OK;
}
// ---- Instruction selection (ISEL) ----
uint32_t domsymas_isel_count(const DomSymAs_Bank *bank) {
    return bank ? (uint32_t)bank->isels.size() : 0;
}
const DomSymAs_Isel *domsymas_isel_at(const DomSymAs_Bank *bank, uint32_t i) {
    return (bank && i < bank->isels.size()) ? &bank->isels[i] : nullptr;
}
DomSymAs_Error domsymas_find_isel(const DomSymAs_Bank *bank, const char *node,
                                  uint32_t *out, uint32_t cap, uint32_t *out_n) {
    if (!bank || !node || !out_n || (cap && !out)) return DOMSYMAS_ERR_IO;
    std::vector<const domsymas::IselRec *> recs = bank->bank.isel_for_node(node);
    *out_n = 0;
    if (recs.empty()) return DOMSYMAS_ERR_NOT_FOUND;
    uint32_t n = (uint32_t)recs.size() < cap ? (uint32_t)recs.size() : cap;
    for (uint32_t i = 0; i < n; i++) {
        // Only the selected form is meaningful per record; the record id is
        // reported in the low half so callers can look it up if they need it.
        out[i] = recs[i]->form;
    }
    *out_n = n;
    return DOMSYMAS_OK;
}
DomSymAs_Error domsymas_select_form(const DomSymAs_Bank *bank, const char *node,
                                    const uint32_t *enabled, uint32_t enabled_n,
                                    uint32_t model, uint32_t *out_form) {
    if (!bank || !node || !out_form || (enabled_n && !enabled)) return DOMSYMAS_ERR_IO;
    std::vector<uint32_t> en(enabled, enabled + enabled_n);
    uint32_t form = bank->bank.select_form(node, en, model);
    if (form == domsymas::kNoId) return DOMSYMAS_ERR_NOT_FOUND;
    *out_form = form;
    return DOMSYMAS_OK;
}

// ---- Scheduling (SCHED + TUNING + UNITS) ----
uint32_t domsymas_sched_count(const DomSymAs_Bank *bank) {
    return bank ? (uint32_t)bank->scheds.size() : 0;
}
const DomSymAs_Sched *domsymas_sched_at(const DomSymAs_Bank *bank, uint32_t i) {
    return (bank && i < bank->scheds.size()) ? &bank->scheds[i] : nullptr;
}
const DomSymAs_Sched *domsymas_sched_for_form(const DomSymAs_Bank *bank, uint32_t form) {
    if (!bank) return nullptr;
    const domsymas::SchedRec *s = bank->bank.find_sched(form);
    if (!s) return nullptr;
    for (auto &c : bank->scheds) if (c.id == s->id) return &c;
    return nullptr;
}
DomSymAs_Error domsymas_sched_info(const DomSymAs_Bank *bank, uint32_t form,
                                   uint32_t model, DomSymAs_SchedInfo *out) {
    if (!bank || !out) return DOMSYMAS_ERR_IO;
    domsymas::Databank::ScheduleInfo r = bank->bank.schedule_for_form(form, model);
    out->found = r.found ? 1 : 0;
    out->sched = r.sched;
    out->name = strdup(r.name.c_str());
    if (!out->name) return DOMSYMAS_ERR_NOMEM;
    out->latency = r.latency;
    out->uops = r.uops;
    out->cycles = r.cycles;
    out->flags = r.flags;
    out->write_count = (uint32_t)r.writes.size();
    out->writes = nullptr;
    if (out->write_count) {
        out->writes = (uint32_t *)calloc(r.writes.size(), sizeof(uint32_t));
        if (!out->writes) { free(out->name); return DOMSYMAS_ERR_NOMEM; }
        for (uint32_t i = 0; i < out->write_count; i++) out->writes[i] = r.writes[i];
    }
    out->read_count = (uint32_t)r.reads.size();
    out->reads = nullptr;
    if (out->read_count) {
        out->reads = (uint32_t *)calloc(r.reads.size(), sizeof(uint32_t));
        if (!out->reads) {
            free(out->name);
            free(out->writes);
            return DOMSYMAS_ERR_NOMEM;
        }
        for (uint32_t i = 0; i < out->read_count; i++) out->reads[i] = r.reads[i];
    }
    out->cost = r.cost;
    out->cost_known = r.cost_known ? 1 : 0;
    return DOMSYMAS_OK;
}
void domsymas_free_sched_info(DomSymAs_SchedInfo *si) {
    if (!si) return;
    free(si->name); si->name = nullptr;
    free(si->writes); si->writes = nullptr; si->write_count = 0;
    free(si->reads); si->reads = nullptr; si->read_count = 0;
}

// ---- Functional units (UNITS) ----
uint32_t domsymas_unit_count(const DomSymAs_Bank *bank) {
    return bank ? (uint32_t)bank->units.size() : 0;
}
const DomSymAs_Unit *domsymas_unit_at(const DomSymAs_Bank *bank, uint32_t i) {
    return (bank && i < bank->units.size()) ? &bank->units[i] : nullptr;
}
const DomSymAs_Unit *domsymas_find_unit(const DomSymAs_Bank *bank, const char *n) {
    if (!bank || !n) return nullptr;
    const domsymas::UnitRec *u = bank->bank.find_unit(n);
    if (!u) return nullptr;
    for (auto &c : bank->units) if (c.id == u->id) return &c;
    return nullptr;
}

// ---- Machine models (MODELS) ----
uint32_t domsymas_model_count(const DomSymAs_Bank *bank) {
    return bank ? (uint32_t)bank->models.size() : 0;
}
const DomSymAs_Model *domsymas_model_at(const DomSymAs_Bank *bank, uint32_t i) {
    return (bank && i < bank->models.size()) ? &bank->models[i] : nullptr;
}
const DomSymAs_Model *domsymas_find_model(const DomSymAs_Bank *bank, const char *n) {
    if (!bank || !n) return nullptr;
    const domsymas::ModelRec *m = bank->bank.find_model(n);
    if (!m) return nullptr;
    for (auto &c : bank->models) if (c.id == m->id) return &c;
    return nullptr;
}
uint32_t domsymas_default_model(const DomSymAs_Bank *bank) {
    return bank ? bank->bank.default_model() : DOMSYMAS_NO_MODEL;
}

// ---- Per-model optimization hints (TUNING) ----
uint32_t domsymas_tune_count(const DomSymAs_Bank *bank) {
    return bank ? (uint32_t)bank->tunes.size() : 0;
}
const DomSymAs_Tune *domsymas_tune_at(const DomSymAs_Bank *bank, uint32_t i) {
    return (bank && i < bank->tunes.size()) ? &bank->tunes[i] : nullptr;
}
DomSymAs_Error domsymas_tune_value(const DomSymAs_Bank *bank, uint32_t model,
                                   uint32_t form, uint32_t kind, int64_t *out) {
    if (!bank || !out) return DOMSYMAS_ERR_IO;
    if (!bank->bank.tune_value(model, form, kind, *out)) return DOMSYMAS_ERR_NOT_FOUND;
    return DOMSYMAS_OK;
}

// ---- Instruction-pair fusion (FUSION) ----
uint32_t domsymas_fusion_count(const DomSymAs_Bank *bank) {
    return bank ? (uint32_t)bank->fusions.size() : 0;
}
const DomSymAs_Fusion *domsymas_fusion_at(const DomSymAs_Bank *bank, uint32_t i) {
    return (bank && i < bank->fusions.size()) ? &bank->fusions[i] : nullptr;
}
const DomSymAs_Fusion *domsymas_find_fusion(const DomSymAs_Bank *bank, uint32_t a,
                                            uint32_t b, int bidirect) {
    if (!bank) return nullptr;
    const domsymas::FusionRec *f = bank->bank.find_fusion(a, b, bidirect != 0);
    if (!f) return nullptr;
    for (auto &c : bank->fusions) if (c.id == f->id) return &c;
    return nullptr;
}

void domsymas_free(void *ptr) { free(ptr); }void domsymas_free_bytes(DomSymAs_Bytes *b) {
    if (b && b->data) { free(b->data); b->data = nullptr; b->len = 0; }
}
void domsymas_free_relocs(DomSymAs_RelocOut *r, uint32_t n) {
    if (!r) return;
    for (uint32_t i = 0; i < n; i++) free(r[i].symbol);
    free(r);
}
void domsymas_free_candidates(DomSymAs_Candidate *c, uint32_t n) {
    if (!c) return;
    for (uint32_t i = 0; i < n; i++) free(c[i].text);
    free(c);
}

// ---- Builder ----
DomSymAs_Builder *domsymas_builder_new(int little_endian) {
    return new (std::nothrow) DomSymAs_Builder(little_endian != 0);
}
void domsymas_builder_free(DomSymAs_Builder *b) { delete b; }
DomSymAs_Error domsymas_builder_set_compression(DomSymAs_Builder *b, uint8_t comp) {
    if (!b || comp > DOMSYMAS_COMP_ZSTD) return DOMSYMAS_ERR_IO;
    b->impl.set_compression((domsymas::Compression)comp);
    return DOMSYMAS_OK;
}
DomSymAs_Error domsymas_builder_add_target(DomSymAs_Builder *b, const char *arch,
    const char *subarch, uint32_t addr_size, uint32_t op_size, uint32_t addr_def,
    uint32_t align, uint32_t byte_order, const char *objfmt, const char *abi,
    const char *conv, const uint32_t *features, uint32_t feature_n, uint32_t *out_id) {
    if (!b || (feature_n && !features)) return DOMSYMAS_ERR_IO;
    domsymas::TargetRec t;
    t.arch = arch ? arch : ""; t.subarch = subarch ? subarch : "";
    t.addr_size = addr_size; t.default_op_size = op_size; t.default_addr_size = addr_def;
    t.instr_align = align ? align : 1; t.byte_order = byte_order;
    t.obj_format = objfmt ? objfmt : ""; t.abi = abi ? abi : "";
    t.calling_conv = conv ? conv : "";
    for (uint32_t i = 0; i < feature_n; i++) t.features.push_back(features[i]);
    uint32_t id = b->impl.add_target(std::move(t));
    if (out_id) *out_id = id;
    return DOMSYMAS_OK;
}
DomSymAs_Error domsymas_builder_add_feature(DomSymAs_Builder *b, const char *name,
    uint32_t flags, const char *desc, uint32_t *out_id) {
    if (!b || !name) return DOMSYMAS_ERR_IO;
    uint32_t id = b->impl.add_feature(name, flags, desc ? desc : "");
    if (out_id) *out_id = id;
    return DOMSYMAS_OK;
}
DomSymAs_Error domsymas_builder_add_register(DomSymAs_Builder *b, const char *name,
    uint32_t regclass, uint32_t width, uint32_t enc_num, uint32_t parent,
    uint32_t sub_lo, uint32_t sub_hi, uint32_t flags, uint32_t *out_id) {
    if (!b || !name) return DOMSYMAS_ERR_IO;
    domsymas::RegisterRec r;
    r.name = name; r.regclass = regclass; r.width = width; r.enc_num = enc_num;
    r.parent = parent; r.sub_lo = sub_lo; r.sub_hi = sub_hi; r.flags = flags;
    uint32_t id = b->impl.add_register(std::move(r));
    if (out_id) *out_id = id;
    return DOMSYMAS_OK;
}
DomSymAs_Error domsymas_builder_add_mnemonic(DomSymAs_Builder *b, const char *canonical,
    const char *const *aliases, uint32_t alias_n, uint32_t flags, uint32_t *out_id) {
    if (!b || !canonical || (alias_n && !aliases)) return DOMSYMAS_ERR_IO;
    std::vector<std::string> al;
    for (uint32_t i = 0; i < alias_n; i++) al.push_back(aliases[i] ? aliases[i] : "");
    uint32_t id = b->impl.add_mnemonic(canonical, al, flags);
    if (out_id) *out_id = id;
    return DOMSYMAS_OK;
}
DomSymAs_Error domsymas_builder_add_form(DomSymAs_Builder *b, uint32_t mnemonic,
    uint32_t op_count, uint32_t flags, uint32_t encoder, const uint32_t *req,
    uint32_t req_n, const uint32_t *forb, uint32_t forb_n, const uint32_t *syn,
    uint32_t syn_n, uint32_t *out_id) {
    if (!b || (req_n && !req) || (forb_n && !forb) || (syn_n && !syn))
        return DOMSYMAS_ERR_IO;
    std::vector<uint32_t> rq, fb, sy;
    for (uint32_t i = 0; i < req_n; i++) rq.push_back(req[i]);
    for (uint32_t i = 0; i < forb_n; i++) fb.push_back(forb[i]);
    for (uint32_t i = 0; i < syn_n; i++) sy.push_back(syn[i]);
    uint32_t id = b->impl.add_form(mnemonic, op_count, flags, encoder, rq, fb, sy);
    if (out_id) *out_id = id;
    return DOMSYMAS_OK;
}
DomSymAs_Error domsymas_builder_add_operand(DomSymAs_Builder *b, uint32_t form,
    uint32_t pos, uint32_t kind, uint32_t width, uint32_t regclass, int64_t imm_min,
    int64_t imm_max, uint32_t is_signed, uint32_t enc_role, uint32_t access,
    uint32_t hidden, const char *constraint, uint32_t *out_id) {
    if (!b) return DOMSYMAS_ERR_IO;
    domsymas::OperandRec o;
    o.form = form; o.position = pos; o.kind = kind; o.width = width;
    o.regclass = regclass; o.imm_min = imm_min; o.imm_max = imm_max;
    o.is_signed = is_signed; o.enc_role = enc_role; o.access = access;
    o.hidden = hidden; o.constraint = constraint ? constraint : "";
    uint32_t id = b->impl.add_operand(std::move(o));
    if (out_id) *out_id = id;
    return DOMSYMAS_OK;
}
DomSymAs_Error domsymas_builder_add_encoder(DomSymAs_Builder *b, const char *name,
    const char *source, uint32_t flags, uint32_t max_bytes, uint32_t max_relocs,
    uint32_t *out_id) {
    if (!b || !name || !source) return DOMSYMAS_ERR_IO;
    uint32_t id = b->impl.add_encoder(name, source, flags,
                                      max_bytes ? max_bytes : 64,
                                      max_relocs ? max_relocs : 8);
    if (out_id) *out_id = id;
    return DOMSYMAS_OK;
}
DomSymAs_Error domsymas_builder_add_syntax(DomSymAs_Builder *b, const char *name,
    uint32_t order, const char *reg_prefix, const char *imm_prefix,
    const char *comment, const char *sep, uint32_t case_sensitive, const char *tmpl,
    uint32_t *out_id) {
    if (!b || !name) return DOMSYMAS_ERR_IO;
    domsymas::SyntaxRec s;
    s.name = name; s.operand_order = order;
    s.reg_prefix = reg_prefix ? reg_prefix : "";
    s.imm_prefix = imm_prefix ? imm_prefix : "";
    s.comment = comment ? comment : ""; s.separator = sep ? sep : ", ";
    s.case_sensitive = case_sensitive; s.tmpl = tmpl ? tmpl : "{mnemonic} {ops}";
    uint32_t id = b->impl.add_syntax(std::move(s));
    if (out_id) *out_id = id;
    return DOMSYMAS_OK;
}
DomSymAs_Error domsymas_builder_link_syntax_form(DomSymAs_Builder *b, uint32_t syn,
                                                 uint32_t form) {
    if (!b) return DOMSYMAS_ERR_IO;
    b->impl.link_syntax_form(syn, form);
    return DOMSYMAS_OK;
}
DomSymAs_Error domsymas_builder_link_mnemonic_form(DomSymAs_Builder *b, uint32_t mn,
                                                   uint32_t form) {
    if (!b) return DOMSYMAS_ERR_IO;
    b->impl.link_mnemonic_form(mn, form);
    return DOMSYMAS_OK;
}
DomSymAs_Error domsymas_builder_add_section(DomSymAs_Builder *b, const char *name,
    uint32_t flags, uint32_t align, uint32_t addrspace, uint32_t content, uint32_t *out_id) {
    if (!b || !name) return DOMSYMAS_ERR_IO;
    uint32_t id = b->impl.add_section(name, flags, align ? align : 1, addrspace, content);
    if (out_id) *out_id = id;
    return DOMSYMAS_OK;
}
DomSymAs_Error domsymas_builder_add_linkage(DomSymAs_Builder *b, const char *name,
    uint32_t binding, uint32_t visibility, uint32_t symtype, uint32_t flags,
    uint32_t *out_id) {
    if (!b || !name) return DOMSYMAS_ERR_IO;
    uint32_t id = b->impl.add_linkage(name, binding, visibility, symtype, flags);
    if (out_id) *out_id = id;
    return DOMSYMAS_OK;
}
DomSymAs_Error domsymas_builder_add_reloc(DomSymAs_Builder *b, const char *name,
    uint32_t width, uint32_t offset, uint32_t is_signed, uint32_t pcrel,
    uint32_t overflow, uint32_t addend, const char *expr, uint32_t *out_id) {
    if (!b || !name) return DOMSYMAS_ERR_IO;
    uint32_t id = b->impl.add_reloc(name, width, offset, is_signed, pcrel, overflow,
                                    addend, expr ? expr : "");
    if (out_id) *out_id = id;
    return DOMSYMAS_OK;
}
DomSymAs_Error domsymas_builder_add_debug(DomSymAs_Builder *b, const char *msg,
    uint32_t ref_kind, uint32_t ref_id, uint32_t *out_id) {
    if (!b || !msg) return DOMSYMAS_ERR_IO;
    uint32_t id = b->impl.add_debug(msg, ref_kind, ref_id);
    if (out_id) *out_id = id;
    return DOMSYMAS_OK;
}
DomSymAs_Error domsymas_builder_add_isel(DomSymAs_Builder *b, const char *node,
    uint32_t kind, uint32_t form, uint32_t cost, uint32_t complexity,
    uint32_t priority, uint32_t flags, const uint32_t *req, uint32_t req_n,
    const uint32_t *forb, uint32_t forb_n, const uint32_t *alts, uint32_t alt_n,
    const char *pattern, uint32_t *out_id) {
    if (!b || !node || (req_n && !req) || (forb_n && !forb) || (alt_n && !alts))
        return DOMSYMAS_ERR_IO;
    domsymas::IselRec r;
    r.node = node;
    r.kind = kind;
    r.form = form == DOMSYMAS_NO_FORM ? domsymas::kNoId : form;
    r.cost = cost;
    r.complexity = complexity;
    r.priority = priority;
    r.flags = flags;
    for (uint32_t i = 0; i < req_n; i++) r.required.push_back(req[i]);
    for (uint32_t i = 0; i < forb_n; i++) r.forbidden.push_back(forb[i]);
    for (uint32_t i = 0; i < alt_n; i++) r.alts.push_back(alts[i]);
    r.pattern = pattern ? pattern : "";
    uint32_t id = b->impl.add_isel(std::move(r));
    if (out_id) *out_id = id;
    return DOMSYMAS_OK;
}
DomSymAs_Error domsymas_builder_add_sched(DomSymAs_Builder *b, uint32_t form,
    const char *name, uint32_t latency, uint32_t uops, uint32_t cycles,
    uint32_t flags, const uint32_t *writes, uint32_t write_n,
    const uint32_t *reads, uint32_t read_n, uint32_t *out_id) {
    if (!b || (write_n && !writes) || (read_n && !reads)) return DOMSYMAS_ERR_IO;
    domsymas::SchedRec r;
    r.form = form;
    r.name = name ? name : "";
    r.latency = latency;
    r.uops = uops ? uops : 1;
    r.cycles = cycles;
    r.flags = flags;
    for (uint32_t i = 0; i < write_n; i++) r.writes.push_back(writes[i]);
    for (uint32_t i = 0; i < read_n; i++) r.reads.push_back(reads[i]);
    uint32_t id = b->impl.add_sched(std::move(r));
    if (out_id) *out_id = id;
    return DOMSYMAS_OK;
}
DomSymAs_Error domsymas_builder_add_unit(DomSymAs_Builder *b, uint32_t model,
    const char *name, uint32_t kind, uint32_t count, uint32_t latency,
    uint32_t throughput, uint32_t flags, uint32_t *out_id) {
    if (!b || !name) return DOMSYMAS_ERR_IO;
    domsymas::UnitRec r;
    r.model = model == DOMSYMAS_NO_MODEL ? domsymas::kNoId : model;
    r.name = name;
    r.kind = kind;
    r.count = count ? count : 1;
    r.latency = latency;
    r.throughput = throughput ? throughput : 1;
    r.flags = flags;
    uint32_t id = b->impl.add_unit(std::move(r));
    if (out_id) *out_id = id;
    return DOMSYMAS_OK;
}
DomSymAs_Error domsymas_builder_add_model(DomSymAs_Builder *b, const char *name,
    const char *vendor, uint32_t issue_width, uint32_t micro_op_buffer,
    uint32_t loop_buffer, uint32_t load_latency, uint32_t mispredict_penalty,
    uint32_t high_latency, uint32_t high_uops, uint32_t flags,
    const uint32_t *features, uint32_t feature_n, const uint32_t *tune,
    uint32_t tune_n, const char *desc, uint32_t *out_id) {
    if (!b || !name || (feature_n && !features)) return DOMSYMAS_ERR_IO;
    domsymas::ModelRec r;
    r.name = name;
    r.vendor = vendor ? vendor : "";
    r.issue_width = issue_width ? issue_width : 1;
    r.micro_op_buffer = micro_op_buffer;
    r.loop_buffer = loop_buffer;
    r.load_latency = load_latency;
    r.mispredict_penalty = mispredict_penalty;
    r.high_latency = high_latency;
    r.high_uops = high_uops;
    r.flags = flags;
    for (uint32_t i = 0; i < feature_n; i++) r.features.push_back(features[i]);
    for (uint32_t i = 0; i < tune_n; i++) r.tune_features.push_back(tune[i]);
    r.desc = desc ? desc : "";
    uint32_t id = b->impl.add_model(std::move(r));
    if (out_id) *out_id = id;
    return DOMSYMAS_OK;
}
DomSymAs_Error domsymas_builder_add_tune(DomSymAs_Builder *b, uint32_t model,
    uint32_t kind, const char *name, uint32_t form, uint32_t form2,
    int64_t value, uint32_t flags, const uint32_t *features, uint32_t feature_n,
    const char *desc, uint32_t *out_id) {
    if (!b || (feature_n && !features)) return DOMSYMAS_ERR_IO;
    domsymas::TuneRec r;
    r.model = model == DOMSYMAS_NO_MODEL ? domsymas::kNoId : model;
    r.kind = kind;
    r.name = name ? name : "";
    r.form = form == DOMSYMAS_NO_FORM ? domsymas::kNoId : form;
    r.form2 = form2 == DOMSYMAS_NO_FORM ? domsymas::kNoId : form2;
    r.value = value;
    r.flags = flags;
    for (uint32_t i = 0; i < feature_n; i++) r.features.push_back(features[i]);
    r.desc = desc ? desc : "";
    uint32_t id = b->impl.add_tune(std::move(r));
    if (out_id) *out_id = id;
    return DOMSYMAS_OK;
}
DomSymAs_Error domsymas_builder_add_fusion(DomSymAs_Builder *b, const char *name,
    uint32_t form_a, uint32_t form_b, uint32_t kind, uint32_t latency,
    uint32_t flags, const uint32_t *features, uint32_t feature_n,
    const char *pattern, uint32_t *out_id) {
    if (!b || !name || (feature_n && !features)) return DOMSYMAS_ERR_IO;
    domsymas::FusionRec r;
    r.name = name;
    r.a = form_a == DOMSYMAS_NO_FORM ? domsymas::kNoId : form_a;
    r.b = form_b == DOMSYMAS_NO_FORM ? domsymas::kNoId : form_b;
    r.kind = kind;
    r.latency = latency;
    r.flags = flags;
    for (uint32_t i = 0; i < feature_n; i++) r.features.push_back(features[i]);
    r.pattern = pattern ? pattern : "";
    uint32_t id = b->impl.add_fusion(std::move(r));
    if (out_id) *out_id = id;
    return DOMSYMAS_OK;
}
DomSymAs_Error domsymas_builder_build(DomSymAs_Builder *b, DomSymAs_Bank **out) {
    if (!b || !out) return DOMSYMAS_ERR_IO;
    DomSymAs_Bank *bank = new (std::nothrow) DomSymAs_Bank();
    if (!bank) return DOMSYMAS_ERR_NOMEM;
    domsymas::Error e = b->impl.build(bank->bank);
    if (!e.ok()) { bank->last_error = e.message; delete bank; return e.code; }
    rebuild_cache(bank);
    *out = bank;
    return DOMSYMAS_OK;
}
DomSymAs_Error domsymas_builder_save(DomSymAs_Builder *b, const char *path) {
    if (!b || !path) return DOMSYMAS_ERR_IO;
    domsymas::Error e = b->impl.save_file(path);
    return e.code;
}

} // extern "C"
