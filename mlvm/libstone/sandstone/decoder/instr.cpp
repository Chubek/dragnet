// DomSymAs instruction model: lookups, feature-aware resolution, encoding.
#include "sandstone.hpp"

namespace domsymas {

namespace {
// local lowercase without the silly indirection above
std::string tolower_s(const std::string &s) {
    std::string o = s;
    for (auto &c : o) c = (char)tolower((unsigned char)c);
    return o;
}
} // namespace

std::vector<uint32_t> Databank::forms_for_mnemonic(const std::string &name) const {
    if (const MnemonicRec *m = find_mnemonic(name)) return m->forms;
    return {};
}

const MnemonicRec *Databank::find_mnemonic(const std::string &name) const {
    // Index fast path (optimization only): fall back to full scan.
    for (auto &ix : data_.index) {
        if (ix.kind != 0) continue;
        for (auto &m : data_.mnemonics) {
            if (m.id != ix.record) continue;
            bool ci = (m.flags & 1) != 0;
            auto eq = [&](const std::string &a) {
                return ci ? tolower_s(a) == tolower_s(name) : a == name;
            };
            if (ix.key == (ci ? tolower_s(name) : name) && eq(m.canonical)) return &m;
        }
    }
    for (auto &m : data_.mnemonics) {
        bool ci = (m.flags & 1) != 0;
        auto eq = [&](const std::string &a) {
            return ci ? tolower_s(a) == tolower_s(name) : a == name;
        };
        if (eq(m.canonical)) return &m;
        for (auto &a : m.aliases)
            if (eq(a)) return &m;
    }
    return nullptr;
}

const FormRec *Databank::find_form(uint32_t id) const {
    for (auto &f : data_.forms)
        if (f.id == id) return &f;
    return nullptr;
}

const RegisterRec *Databank::find_register(const std::string &name) const {
    for (auto &r : data_.registers)
        if (r.name == name) return &r;
    return nullptr;
}

const FeatureRec *Databank::find_feature(const std::string &name) const {
    for (auto &f : data_.features)
        if (f.name == name) return &f;
    return nullptr;
}

const EncoderRec *Databank::find_encoder(uint32_t id) const {
    for (auto &e : data_.encoders)
        if (e.id == id) return &e;
    return nullptr;
}

std::vector<const OperandRec *> Databank::operands_for_form(uint32_t form) const {
    std::vector<const OperandRec *> out;
    for (auto &o : data_.operands)
        if (o.form == form) out.push_back(&o);
    return out;
}

bool Databank::index_lookup(uint32_t kind, const std::string &key, uint32_t &record) const {
    for (auto &ix : data_.index)
        if (ix.kind == kind && ix.key == key) { record = ix.record; return true; }
    return false;
}

std::vector<uint32_t> Databank::resolve(const std::string &mnemonic, uint32_t op_count,
                                        const std::vector<uint32_t> &enabled) const {
    std::vector<uint32_t> out;
    const MnemonicRec *m = find_mnemonic(mnemonic);
    if (!m) return out;
    auto has = [&](uint32_t f) {
        for (uint32_t e : enabled) if (e == f) return true;
        return false;
    };
    for (uint32_t fid : m->forms) {
        const FormRec *f = find_form(fid);
        if (!f || f->operand_count != op_count) continue;
        bool ok = true;
        for (uint32_t r : f->required) if (!has(r)) { ok = false; break; }
        for (uint32_t x : f->forbidden) if (has(x)) { ok = false; break; }
        if (ok) out.push_back(fid);
    }
    return out;
}

Error Databank::encode(uint32_t form, const std::map<std::string, encoding::Value> &ops,
                       EncodeResult &out) const {
    const FormRec *f = find_form(form);
    if (!f) return Error{DOMSYMAS_ERR_BAD_FORM_REF, "unknown form id", form};
    const EncoderRec *e = find_encoder(f->encoder);
    if (!e) return Error{DOMSYMAS_ERR_BAD_ENCODER_REF, "form references missing encoder", form};
    encoding::Program prog;
    std::string diag;
    if (!encoding::parse(e->source, prog, diag))
        return Error{DOMSYMAS_ERR_ENCODE, "encoder parse failed: " + diag, form};
    encoding::Limits lim;
    lim.max_bytes = e->max_bytes ? e->max_bytes : 256;
    lim.max_relocs = e->max_relocs ? e->max_relocs : 32;
    encoding::Outcome oc;
    if (!encoding::execute(prog, ops, lim, oc, diag))
        return Error{DOMSYMAS_ERR_ENCODE, "encoder failed: " + diag, form};
    EncodeResult r;
    r.bytes = std::move(oc.bytes);
    for (auto &rl : oc.relocs) {
        RelocOut o;
        o.offset = rl.offset; o.symbol = rl.symbol; o.addend = rl.addend;
        if (rl.type == 0xFFFFFFFFu) {
            const RelocRec *rr = find_reloc(rl.type_name);
            if (!rr)
                return Error{DOMSYMAS_ERR_BAD_RELOC,
                             "unknown relocation type '" + rl.type_name + "'", form};
            o.type = rr->id;
        } else {
            o.type = rl.type;
        }
        r.relocs.push_back(std::move(o));
    }
    out = std::move(r);
    return Error{};
}

} // namespace domsymas
