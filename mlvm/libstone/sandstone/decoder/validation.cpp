// DomSymAs validation: structural (container, directory, bounds, string
// refs, overflow) + semantic (cross-references, reloc widths, alignment,
// encoder parseability).
#include "sandstone.hpp"
#include <set>
#include <unordered_set>

namespace domsymas {

Error Databank::validate(bool semantic, std::string *diag) const {
    auto fail = [&](DomSymAs_Error c, const std::string &m) -> Error {
        if (diag) *diag = m;
        return Error{c, m, 0};
    };
    auto unique_names = [&](const auto &items, const char *what,
                            auto get_name) -> Error {
        std::unordered_set<std::string> seen;
        for (const auto &item : items) {
            const std::string &name = get_name(item);
            if (name.empty())
                return fail(DOMSYMAS_ERR_BAD_RECORD_REF,
                            std::string(what) + " has empty name");
            if (!seen.insert(name).second)
                return fail(DOMSYMAS_ERR_BAD_RECORD_REF,
                            std::string(what) + " has duplicate name '" + name + "'");
        }
        return Error{};
    };
    const DatabankData &d = data_;
    if (d.strings.empty() || !d.strings[0].empty())
        return fail(DOMSYMAS_ERR_BAD_STRING_REF, "string table must start with empty string");
    if (d.targets.size() > 256)
        return fail(DOMSYMAS_ERR_BAD_BOUNDS, "too many targets");
    for (auto &t : d.targets) {
        if (t.addr_size != 8 && t.addr_size != 16 && t.addr_size != 32 && t.addr_size != 64)
            return fail(DOMSYMAS_ERR_BAD_RECORD_REF, "target has invalid address size");
        if (t.instr_align == 0 || (t.instr_align & (t.instr_align - 1)))
            return fail(DOMSYMAS_ERR_BAD_ALIGN, "target instruction alignment not a power of two");
        for (uint32_t f : t.features) {
            bool ok = false;
            for (auto &fe : d.features) if (fe.id == f) { ok = true; break; }
            if (!ok) return fail(DOMSYMAS_ERR_BAD_FEATURE_REF, "target references missing feature");
        }
    }
    if (!semantic) return Error{};
    for (const auto &r : {unique_names(d.features, "feature", [](const auto &x) -> const std::string & { return x.name; }),
                          unique_names(d.registers, "register", [](const auto &x) -> const std::string & { return x.name; }),
                          unique_names(d.mnemonics, "mnemonic", [](const auto &x) -> const std::string & { return x.canonical; }),
                          unique_names(d.encoders, "encoder", [](const auto &x) -> const std::string & { return x.name; }),
                          unique_names(d.syntaxes, "syntax", [](const auto &x) -> const std::string & { return x.name; }),
                          unique_names(d.sections, "section", [](const auto &x) -> const std::string & { return x.name; }),
                          unique_names(d.linkages, "linkage", [](const auto &x) -> const std::string & { return x.name; }),
                          unique_names(d.relocs, "relocation", [](const auto &x) -> const std::string & { return x.name; }),
                          unique_names(d.models, "model", [](const auto &x) -> const std::string & { return x.name; }),
                          unique_names(d.units, "unit", [](const auto &x) -> const std::string & { return x.name; }),
                          unique_names(d.fusions, "fusion", [](const auto &x) -> const std::string & { return x.name; })})
        if (!r.ok()) return r;
    auto has_feature = [&](uint32_t id) {
        for (auto &f : d.features) if (f.id == id) return true;
        return false;
    };
    auto has_encoder = [&](uint32_t id) {
        for (auto &e : d.encoders) if (e.id == id) return true;
        return false;
    };
    auto has_mnemonic = [&](uint32_t id) {
        for (auto &m : d.mnemonics) if (m.id == id) return true;
        return false;
    };
    auto has_form = [&](uint32_t id) {
        for (auto &f : d.forms) if (f.id == id) return true;
        return false;
    };
    auto has_syntax = [&](uint32_t id) {
        for (const auto &s : d.syntaxes) if (s.id == id) return true;
        return false;
    };
    for (auto &r : d.registers) {
        if (r.width != 8 && r.width != 16 && r.width != 32 && r.width != 64 &&
            r.width != 80 && r.width != 128 && r.width != 256 && r.width != 512)
            return fail(DOMSYMAS_ERR_BAD_RECORD_REF, "register '" + r.name + "' has invalid width");
        if (r.parent != kNoParent) {
            bool ok = false;
            for (auto &p : d.registers) if (p.id == r.parent) { ok = true; break; }
            if (!ok) return fail(DOMSYMAS_ERR_BAD_RECORD_REF,
                                 "register '" + r.name + "' has missing parent");
            if (r.sub_lo > r.sub_hi || r.sub_hi >= 512)
                return fail(DOMSYMAS_ERR_BAD_RECORD_REF, "register subrange invalid");
        }
    }
    for (auto &m : d.mnemonics) {
        if (m.canonical.empty())
            return fail(DOMSYMAS_ERR_BAD_RECORD_REF, "mnemonic has empty canonical name");
        for (uint32_t fid : m.forms)
            if (!has_form(fid))
                return fail(DOMSYMAS_ERR_BAD_FORM_REF, "mnemonic '" + m.canonical +
                                                       "' references missing form");
    }
    for (auto &f : d.forms) {
        if (!has_mnemonic(f.mnemonic))
            return fail(DOMSYMAS_ERR_BAD_RECORD_REF, "form references missing mnemonic");
        if (!has_encoder(f.encoder))
            return fail(DOMSYMAS_ERR_BAD_ENCODER_REF, "form references missing encoder");
        for (uint32_t x : f.required)
            if (!has_feature(x))
                return fail(DOMSYMAS_ERR_BAD_FEATURE_REF, "form references missing required feature");
        for (uint32_t x : f.forbidden)
            if (!has_feature(x))
                return fail(DOMSYMAS_ERR_BAD_FEATURE_REF, "form references missing forbidden feature");
        for (uint32_t x : f.syntaxes)
            if (!has_syntax(x))
                return fail(DOMSYMAS_ERR_BAD_RECORD_REF, "form references missing syntax");
        uint32_t exp = 0;
        std::set<uint32_t> positions;
        for (auto &o : d.operands) if (o.form == f.id) exp++;
        if (exp != f.operand_count)
            return fail(DOMSYMAS_ERR_BAD_FORM_REF, "form operand count disagrees with descriptors");
        for (auto &o : d.operands) {
            if (o.form != f.id) continue;
            if (o.position >= f.operand_count || !positions.insert(o.position).second)
                return fail(DOMSYMAS_ERR_BAD_FORM_REF, "form has missing or duplicate operand position");
            if (o.kind > 9)
                return fail(DOMSYMAS_ERR_BAD_RECORD_REF, "operand has unknown kind");
            if (o.is_signed ? (o.imm_min > o.imm_max) : ((uint64_t)o.imm_max < (uint64_t)o.imm_min))
                return fail(DOMSYMAS_ERR_BAD_RECORD_REF, "operand immediate range inverted");
        }
    }
    for (const auto &o : d.operands)
        if (!has_form(o.form))
            return fail(DOMSYMAS_ERR_BAD_FORM_REF, "operand references missing form");
    for (const auto &s : d.syntaxes)
        for (uint32_t fid : s.forms)
            if (!has_form(fid))
                return fail(DOMSYMAS_ERR_BAD_FORM_REF, "syntax references missing form");
    for (auto &e : d.encoders) {
        encoding::Program p;
        std::string dg;
        if (!encoding::parse(e.source, p, dg))
            return fail(DOMSYMAS_ERR_BAD_ENCODER_REF,
                        "encoder '" + e.name + "' does not parse: " + dg);
    }
    for (auto &s : d.sections) {
        if (s.align == 0 || (s.align & (s.align - 1)) || s.align > 4096)
            return fail(DOMSYMAS_ERR_BAD_ALIGN, "section '" + s.name + "' has invalid alignment");
    }
    for (auto &r : d.relocs) {
        if (r.width == 0 || r.width > 64)
            return fail(DOMSYMAS_ERR_BAD_RELOC, "relocation '" + r.name + "' has invalid width");
        if (r.offset + r.width > 64)
            return fail(DOMSYMAS_ERR_BAD_RELOC, "relocation field exceeds 64 bits");
        if (r.addend > 2)
            return fail(DOMSYMAS_ERR_BAD_RELOC, "relocation has invalid addend kind");
    }
    // INDEX is an optimization: entries must resolve, but readers must also
    // work when it is absent or stale-free (stale entries are an error here
    // because the writer regenerates them; unknown kinds are skipped).
    for (auto &ix : d.index) {
        if (ix.kind > kIdxFusion) continue;
        bool ok = false;
        if (ix.kind == kIdxMnemonic) for (auto &m : d.mnemonics) if (m.id == ix.record) ok = true;
        if (ix.kind == kIdxRegister) for (auto &r : d.registers) if (r.id == ix.record) ok = true;
        if (ix.kind == kIdxFeature) for (auto &f : d.features) if (f.id == ix.record) ok = true;
        if (ix.kind == kIdxSyntax) for (auto &s : d.syntaxes) if (s.id == ix.record) ok = true;
        if (ix.kind == kIdxReloc) for (auto &r : d.relocs) if (r.id == ix.record) ok = true;
        if (ix.kind == kIdxSection) for (auto &s : d.sections) if (s.id == ix.record) ok = true;
        if (ix.kind == kIdxModel) for (auto &m : d.models) if (m.id == ix.record) ok = true;
        if (ix.kind == kIdxIselNode) for (auto &v : d.isels) if (v.id == ix.record) ok = true;
        if (ix.kind == kIdxUnit) for (auto &v : d.units) if (v.id == ix.record) ok = true;
        if (ix.kind == kIdxSched) for (auto &v : d.scheds) if (v.id == ix.record) ok = true;
        if (ix.kind == kIdxFusion) for (auto &v : d.fusions) if (v.id == ix.record) ok = true;
        if (!ok) return fail(DOMSYMAS_ERR_BAD_RECORD_REF, "INDEX entry dangles");
    }

    // ---- Optimization sections (ISEL/SCHED/UNITS/MODELS/TUNING/FUSION) ----
    //
    // These are advisory: a bank may omit them entirely. What must hold is
    // referential integrity, because a dangling form id would silently make
    // a selection or scheduling answer meaningless.
    auto has_model = [&](uint32_t id) {
        for (auto &m : d.models) if (m.id == id) return true;
        return false;
    };
    auto has_unit = [&](uint32_t id) {
        for (auto &u : d.units) if (u.id == id) return true;
        return false;
    };
    auto has_features = [&](const std::vector<uint32_t> &v) {
        for (uint32_t x : v) if (!has_feature(x)) return false;
        return true;
    };
    for (auto &m : d.models) {
        if (m.name.empty())
            return fail(DOMSYMAS_ERR_BAD_RECORD_REF, "model has empty name");
        if (m.issue_width == 0 || m.issue_width > 64)
            return fail(DOMSYMAS_ERR_BAD_RECORD_REF,
                        "model '" + m.name + "' has invalid issue width");
        if (!has_features(m.features))
            return fail(DOMSYMAS_ERR_BAD_FEATURE_REF,
                        "model '" + m.name + "' references missing feature");
        if (!has_features(m.tune_features))
            return fail(DOMSYMAS_ERR_BAD_FEATURE_REF,
                        "model '" + m.name + "' references missing tuning feature");
    }
    for (auto &u : d.units) {
        if (u.name.empty())
            return fail(DOMSYMAS_ERR_BAD_RECORD_REF, "unit has empty name");
        if (u.model != kNoId && !has_model(u.model))
            return fail(DOMSYMAS_ERR_BAD_RECORD_REF,
                        "unit '" + u.name + "' references missing model");
        if (u.kind > (uint32_t)UnitKind::System)
            return fail(DOMSYMAS_ERR_BAD_RECORD_REF,
                        "unit '" + u.name + "' has unknown kind");
    }
    for (auto &v : d.isels) {
        if (v.node.empty())
            return fail(DOMSYMAS_ERR_BAD_RECORD_REF, "instruction-selection record has empty node");
        if (v.form == kNoId || !has_form(v.form))
            return fail(DOMSYMAS_ERR_BAD_FORM_REF,
                        "instruction-selection record '" + v.node + "' references missing form");
        if (v.kind > (uint32_t)IselKind::Aggressive)
            return fail(DOMSYMAS_ERR_BAD_RECORD_REF,
                        "instruction-selection record '" + v.node + "' has unknown kind");
        if (!has_features(v.required))
            return fail(DOMSYMAS_ERR_BAD_FEATURE_REF,
                        "instruction-selection record '" + v.node + "' references missing feature");
        if (!has_features(v.forbidden))
            return fail(DOMSYMAS_ERR_BAD_FEATURE_REF,
                        "instruction-selection record '" + v.node + "' references missing feature");
        for (uint32_t a : v.alts)
            if (!has_form(a))
                return fail(DOMSYMAS_ERR_BAD_FORM_REF,
                            "instruction-selection record '" + v.node +
                                "' alternative references missing form");
    }
    // One scheduling record per form at most: consumers pick "the" record.
    for (size_t i = 0; i < d.scheds.size(); ++i) {
        const SchedRec &s = d.scheds[i];
        if (s.form == kNoId || !has_form(s.form))
            return fail(DOMSYMAS_ERR_BAD_FORM_REF, "scheduling record references missing form");
        if (s.uops == 0 || s.uops > 1024)
            return fail(DOMSYMAS_ERR_BAD_RECORD_REF, "scheduling record has invalid micro-op count");
        if (s.cycles > 256)
            return fail(DOMSYMAS_ERR_BAD_RECORD_REF, "scheduling record has invalid cycle count");
        if (s.latency != kVariadicLatency && s.latency > 4096)
            return fail(DOMSYMAS_ERR_BAD_RECORD_REF, "scheduling record has invalid latency");
        for (uint32_t w : s.writes)
            if (!has_unit(w))
                return fail(DOMSYMAS_ERR_BAD_RECORD_REF,
                            "scheduling record references missing write unit");
        for (uint32_t rr : s.reads)
            if (!has_unit(rr))
                return fail(DOMSYMAS_ERR_BAD_RECORD_REF,
                            "scheduling record references missing read unit");
        for (size_t j = i + 1; j < d.scheds.size(); ++j)
            if (d.scheds[j].form == s.form)
                return fail(DOMSYMAS_ERR_BAD_FORM_REF, "duplicate scheduling record for one form");
    }
    for (auto &t : d.tunes) {
        if (t.kind > (uint32_t)TuneKind::Split)
            return fail(DOMSYMAS_ERR_BAD_RECORD_REF,
                        "tuning record '" + t.name + "' has unknown kind");
        if (t.model != kNoId && !has_model(t.model))
            return fail(DOMSYMAS_ERR_BAD_RECORD_REF,
                        "tuning record '" + t.name + "' references missing model");
        if (t.form != kNoId && !has_form(t.form))
            return fail(DOMSYMAS_ERR_BAD_FORM_REF,
                        "tuning record '" + t.name + "' references missing form");
        if (t.form2 != kNoId && !has_form(t.form2))
            return fail(DOMSYMAS_ERR_BAD_FORM_REF,
                        "tuning record '" + t.name + "' references missing second form");
        if (!has_features(t.features))
            return fail(DOMSYMAS_ERR_BAD_FEATURE_REF,
                        "tuning record '" + t.name + "' references missing feature");
    }
    for (auto &f : d.fusions) {
        if (f.name.empty())
            return fail(DOMSYMAS_ERR_BAD_RECORD_REF, "fusion record has empty name");
        if (f.a == kNoId || !has_form(f.a) || f.b == kNoId || !has_form(f.b))
            return fail(DOMSYMAS_ERR_BAD_FORM_REF,
                        "fusion record '" + f.name + "' references missing form");
        if (f.kind > (uint32_t)FusionKind::Target)
            return fail(DOMSYMAS_ERR_BAD_RECORD_REF,
                        "fusion record '" + f.name + "' has unknown kind");
        if (!has_features(f.features))
            return fail(DOMSYMAS_ERR_BAD_FEATURE_REF,
                        "fusion record '" + f.name + "' references missing feature");
    }
    return Error{};
}

} // namespace domsymas
