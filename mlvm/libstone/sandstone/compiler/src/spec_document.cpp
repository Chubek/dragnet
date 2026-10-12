// DomSymAsCC — JSON spec document -> domsymas::Builder mapping.
//
// Implements `compile_document` / `compile_document_to_bank`: parses the
// JSON emitted by the `.dsymspec` DSL (see stdspec/README.md) and populates
// a `domsymas::Builder`. Record references in the document are by *name*;
// this translation unit resolves them to the numeric ids the Builder assigns
// (which are the per-type record indices, assigned in list order).
//
// This is the DomMune-free half of the compiler: it only depends on the
// `json.hpp` model and the `domsymas` library, so it can be built and tested
// independently of the embedded interpreter.
#include "domsymascc/domsymascc.hpp"

#include <cstdlib>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace domsymas::cc {

namespace {

using json::Value;

// ---- small typed accessors over json::Value --------------------------------

static std::string str(const Value &v, const char *key) {
    const Value *p = v.find(key);
    return p ? p->as_string() : std::string();
}

static int64_t num(const Value &v, const char *key, int64_t dflt = 0) {
    const Value *p = v.find(key);
    return p ? p->as_int(dflt) : dflt;
}

static bool flag(const Value &v, const char *key) {
    const Value *p = v.find(key);
    return p ? p->as_bool(false) : false;
}

// ---- string-enum decoders (mirror domsymas.h constants) --------------------

static bool operand_kind(const std::string &s, uint32_t &out) {
    static const std::map<std::string, uint32_t> m = {
        {"reg", 0}, {"imm", 1}, {"disp", 2}, {"mem", 3}, {"abs", 4},
        {"rel", 5}, {"sym", 6}, {"cond", 7}, {"reglist", 8}, {"implicit", 9},
    };
    auto it = m.find(s);
    if (it == m.end()) return false;
    out = it->second;
    return true;
}

static bool reg_class(const std::string &s, uint32_t &out) {
    static const std::map<std::string, uint32_t> m = {
        {"gpr", 0}, {"seg", 1}, {"ctrl", 2}, {"debug", 3}, {"vec", 4},
        {"mask", 5}, {"fp", 6}, {"status", 7}, {"special", 8},
    };
    auto it = m.find(s);
    if (it == m.end()) return false;
    out = it->second;
    return true;
}

static bool byte_order(const std::string &s, uint32_t &out) {
    if (s == "little" || s == "le") { out = 0; return true; }
    if (s == "big" || s == "be") { out = 1; return true; }
    return false;
}

static bool binding(const std::string &s, uint32_t &out) {
    static const std::map<std::string, uint32_t> m = {
        {"local", 0}, {"global", 1}, {"weak", 2}, {"undef", 3},
        {"abs", 4}, {"common", 5}, {"tls", 6},
    };
    auto it = m.find(s);
    if (it == m.end()) return false;
    out = it->second;
    return true;
}

static bool visibility(const std::string &s, uint32_t &out) {
    if (s == "default") { out = 0; return true; }
    if (s == "hidden") { out = 1; return true; }
    if (s == "protected") { out = 2; return true; }
    return false;
}

static bool symtype(const std::string &s, uint32_t &out) {
    static const std::map<std::string, uint32_t> m = {
        {"none", 0}, {"object", 1}, {"func", 2}, {"section", 3},
    };
    auto it = m.find(s);
    if (it == m.end()) return false;
    out = it->second;
    return true;
}

static bool addend_kind(const std::string &s, uint32_t &out) {
    if (s == "explicit") { out = 0; return true; }
    if (s == "infield") { out = 1; return true; }
    if (s == "implied") { out = 2; return true; }
    return false;
}

static bool overflow_kind(const std::string &s, uint32_t &out) {
    if (s == "error") { out = 0; return true; }
    if (s == "truncate") { out = 1; return true; }
    return false;
}

static bool access_kind(const std::string &s, uint32_t &out) {
    if (s == "read") { out = 1; return true; }
    if (s == "write") { out = 2; return true; }
    if (s == "readwrite") { out = 3; return true; }
    return false;
}

static bool section_content(const std::string &s, uint32_t &out) {
    static const std::map<std::string, uint32_t> m = {
        {"unknown", 0}, {"code", 1}, {"data", 2}, {"meta", 3},
    };
    auto it = m.find(s);
    if (it == m.end()) return false;
    out = it->second;
    return true;
}

// Debug ref_kind strings map onto the SymAS section identifiers.
static bool debug_kind(const std::string &s, uint32_t &out) {
    static const std::map<std::string, uint32_t> m = {
        {"target", 2}, {"register", 3}, {"feature", 4}, {"mnemonic", 5},
        {"form", 6}, {"operand", 7}, {"encoder", 8}, {"syntax", 9},
        {"section", 10}, {"linkage", 11}, {"reloc", 12},
        {"isel", 15}, {"sched", 16}, {"unit", 17}, {"model", 18},
        {"tune", 19}, {"fusion", 20},
    };
    auto it = m.find(s);
    if (it == m.end()) return false;
    out = it->second;
    return true;
}

// ---- flag-list decoding ----------------------------------------------------

using FlagDecoder = bool (*)(const std::string &, uint32_t &);

static uint32_t decode_flags(const Value &v, const char *key, FlagDecoder fn,
                             std::string &diag, const std::string &what) {
    uint32_t out = 0;
    const Value *list = v.find(key);
    if (!list) return out;
    if (list->type != json::Type::Array) {
        diag = what + "." + key + " must be a list";
        return out;
    }
    for (const auto &item : *list->array) {
        if (item.type != json::Type::String) {
            diag = what + "." + key + " entries must be strings";
            return out;
        }
        uint32_t bit = 0;
        if (!fn(item.string, bit)) {
            diag = "unknown " + what + "." + key + " value '" + item.string + "'";
            return out;
        }
        out |= bit;
    }
    return out;
}

static bool form_flag(const std::string &s, uint32_t &out) {
    static const std::map<std::string, uint32_t> m = {
        {"privileged", 0x0001}, {"branch", 0x0002}, {"cond_branch", 0x0004},
        {"return", 0x0008}, {"terminator", 0x0010}, {"may_trap", 0x0020},
        {"reads_mem", 0x0040}, {"writes_mem", 0x0080}, {"delayed", 0x0100},
        {"implicit_ops", 0x0200}, {"reloc", 0x0400}, {"aliases", 0x0800},
    };
    auto it = m.find(s);
    if (it == m.end()) return false;
    out = it->second;
    return true;
}

static bool section_flag(const std::string &s, uint32_t &out) {
    static const std::map<std::string, uint32_t> m = {
        {"exec", 0x0001}, {"write", 0x0002}, {"alloc", 0x0004},
        {"readonly", 0x0008}, {"zerofill", 0x0010}, {"merge", 0x0020},
        {"discard", 0x0040}, {"tls", 0x0080}, {"debug", 0x0100},
        {"code", 0x0200}, {"data", 0x0400}, {"meta", 0x0800},
    };
    auto it = m.find(s);
    if (it == m.end()) return false;
    out = it->second;
    return true;
}

static bool reg_flag(const std::string &s, uint32_t &out) {
    static const std::map<std::string, uint32_t> m = {
        {"readable", 0x01}, {"writable", 0x02}, {"imp_used", 0x04},
        {"imp_mod", 0x08}, {"modedep", 0x10},
    };
    auto it = m.find(s);
    if (it == m.end()) return false;
    out = it->second;
    return true;
}

// ---- optimization-section enums (mirror domsymas.h) ------------------------

static bool isel_kind(const std::string &s, uint32_t &out) {
    static const std::map<std::string, uint32_t> m = {
        {"node", 0}, {"pattern", 1}, {"pseudo", 2}, {"expansion", 3},
        {"select", 4}, {"postisel", 5}, {"copy", 6}, {"aggressive", 7},
    };
    auto it = m.find(s);
    if (it == m.end()) return false;
    out = it->second;
    return true;
}

static bool isel_flag(const std::string &s, uint32_t &out) {
    static const std::map<std::string, uint32_t> m = {
        {"prefer", 0x0001}, {"expand", 0x0002}, {"needs_expand", 0x0004},
        {"variadic", 0x0008}, {"commutes", 0x0010}, {"chain", 0x0020},
        {"side_effects", 0x0040}, {"unprofitable", 0x0080},
        {"convergent", 0x0100}, {"tied", 0x0200},
    };
    auto it = m.find(s);
    if (it == m.end()) return false;
    out = it->second;
    return true;
}

static bool sched_flag(const std::string &s, uint32_t &out) {
    static const std::map<std::string, uint32_t> m = {
        {"variadic", 0x0001}, {"multicycle", 0x0002}, {"serializing", 0x0004},
        {"barrier", 0x0008}, {"may_fault", 0x0010}, {"side_effects", 0x0020},
        {"fused", 0x0040}, {"unmodeled", 0x0080}, {"nop", 0x0100},
    };
    auto it = m.find(s);
    if (it == m.end()) return false;
    out = it->second;
    return true;
}

static bool unit_kind(const std::string &s, uint32_t &out) {
    static const std::map<std::string, uint32_t> m = {
        {"other", 0}, {"int", 1}, {"fp", 2}, {"load", 3}, {"store", 4},
        {"branch", 5}, {"vector", 6}, {"mask", 7}, {"divider", 8},
        {"mul", 9}, {"complex", 10}, {"pipe", 11}, {"system", 12},
    };
    auto it = m.find(s);
    if (it == m.end()) return false;
    out = it->second;
    return true;
}

static bool unit_flag(const std::string &s, uint32_t &out) {
    static const std::map<std::string, uint32_t> m = {
        {"shared", 0x0001}, {"superscalar", 0x0002}, {"pipelined", 0x0004},
        {"noncoincident", 0x0008},
    };
    auto it = m.find(s);
    if (it == m.end()) return false;
    out = it->second;
    return true;
}

static bool model_flag(const std::string &s, uint32_t &out) {
    static const std::map<std::string, uint32_t> m = {
        {"in_order", 0x00000001}, {"post_ra", 0x00000002}, {"complete", 0x00000004},
        {"prefer_post_ram", 0x00000008}, {"no_itineraries", 0x00000010},
        {"vliw", 0x00000020}, {"predicable", 0x00000040}, {"morphable", 0x00000080},
        {"has_stalls", 0x00000100}, {"forwarding", 0x00000200},
        {"out_of_order", 0x00000400}, {"bundle_width", 0x00000800},
        {"default", 0x00001000},
    };
    auto it = m.find(s);
    if (it == m.end()) return false;
    out = it->second;
    return true;
}

static bool tune_kind(const std::string &s, uint32_t &out) {
    static const std::map<std::string, uint32_t> m = {
        {"generic", 0}, {"latency", 1}, {"uops", 2}, {"cost", 3}, {"prefer", 4},
        {"avoid", 5}, {"threshold", 6}, {"unroll", 7}, {"vec_width", 8},
        {"pair", 9}, {"strategy", 10}, {"preroll", 11}, {"split", 12},
    };
    auto it = m.find(s);
    if (it == m.end()) return false;
    out = it->second;
    return true;
}

static bool tune_flag(const std::string &s, uint32_t &out) {
    static const std::map<std::string, uint32_t> m = {
        {"only_if", 0x0001}, {"override", 0x0002}, {"experimental", 0x0004},
        {"disable", 0x0008}, {"negative", 0x0010},
    };
    auto it = m.find(s);
    if (it == m.end()) return false;
    out = it->second;
    return true;
}

static bool fusion_kind(const std::string &s, uint32_t &out) {
    static const std::map<std::string, uint32_t> m = {
        {"macro", 0}, {"cmp_branch", 1}, {"branch_pair", 2}, {"mem_arith", 3},
        {"mul_add", 4}, {"shift", 5}, {"mov", 6}, {"load_op", 7},
        {"cmp_select", 8}, {"vsetvli", 9}, {"target", 10},
    };
    auto it = m.find(s);
    if (it == m.end()) return false;
    out = it->second;
    return true;
}

static bool fusion_flag(const std::string &s, uint32_t &out) {
    static const std::map<std::string, uint32_t> m = {
        {"profitable", 0x0001}, {"requires_flags", 0x0002}, {"same_reg", 0x0004},
        {"adjacent", 0x0008}, {"lock_eligible", 0x0010},
        {"suppress_unfuse", 0x0020}, {"bidirectional", 0x0040},
    };
    auto it = m.find(s);
    if (it == m.end()) return false;
    out = it->second;
    return true;
}

// ---- name-list decoding ----------------------------------------------------

static std::vector<std::string> names(const Value &v, const char *key) {
    std::vector<std::string> out;
    const Value *list = v.find(key);
    if (!list) return out;
    if (list->type == json::Type::String) {
        out.push_back(list->as_string());
        return out;
    }
    if (list->type == json::Type::Array)
        for (const auto &item : *list->array)
            if (item.type == json::Type::String) out.push_back(item.as_string());
    return out;
}

static bool require_array(const Value &v, const char *key, std::string &diag,
                          const std::string &what) {
    const Value *list = v.find(key);
    if (!list || list->type != json::Type::Array) {
        diag = "document." + what + " must be an array";
        return false;
    }
    return true;
}

// Field accessor that yields a Null value when the key is absent, so that
// "optional string or list" fields can be passed straight to a resolver.
static const Value &field(const Value &v, const char *key) {
    static const Value kNull;
    const Value *p = v.find(key);
    return p ? *p : kNull;
}

} // namespace

Result compile_document(const std::string &document_json, domsymas::Builder &builder) {
    Result res;
    Value doc;
    try {
        doc = json::parse(document_json);
    } catch (const std::exception &e) {
        res.error = std::string("spec JSON parse failed: ") + e.what();
        return res;
    }
    if (doc.type != json::Type::Object) {
        res.error = "spec document must be a JSON object";
        return res;
    }

    auto fail = [&](const std::string &msg) -> Result {
        Result r;
        r.error = msg;
        return r;
    };

    const Value *schema = doc.find("schema");
    if (!schema || schema->as_int(-1) != (int64_t)kSchemaVersion) {
        return fail("unsupported spec schema version (expected " +
                    std::to_string(kSchemaVersion) + ")");
    }

    // Name -> index maps (Builder ids equal the per-type list index). The
    // lists are shared by every document that is folded into one builder, so
    // each document's indices are offset by the records already present.
    const domsymas::DatabankData &have = builder.data();
    const uint32_t base_features = (uint32_t)have.features.size();
    const uint32_t base_registers = (uint32_t)have.registers.size();
    const uint32_t base_syntaxes = (uint32_t)have.syntaxes.size();
    const uint32_t base_relocs = (uint32_t)have.relocs.size();
    const uint32_t base_sections = (uint32_t)have.sections.size();
    const uint32_t base_linkages = (uint32_t)have.linkages.size();
    const uint32_t base_mnemonics = (uint32_t)have.mnemonics.size();
    const uint32_t base_encoders = (uint32_t)have.encoders.size();
    const uint32_t base_forms = (uint32_t)have.forms.size();
    const uint32_t base_models = (uint32_t)have.models.size();
    const uint32_t base_units = (uint32_t)have.units.size();
    const uint32_t base_scheds = (uint32_t)have.scheds.size();
    const uint32_t base_tunes = (uint32_t)have.tunes.size();
    const uint32_t base_fusions = (uint32_t)have.fusions.size();

    auto index_map_at = [=](const Value &list, uint32_t base,
                             const char *what, std::string &diag) {
        std::map<std::string, uint32_t> m;
        if (list.type != json::Type::Array || !list.array) {
            diag = std::string("document.") + what + " must be an array";
            return m;
        }
        for (size_t i = 0; i < list.array->size(); ++i) {
            std::string name = str((*list.array)[i], "name");
            if (name.empty()) {
                diag = std::string(what) + " entry has empty name";
                return std::map<std::string, uint32_t>{};
            }
            if (!m.emplace(name, (uint32_t)i + base).second) {
                diag = std::string(what) + " contains duplicate name '" + name + "'";
                return std::map<std::string, uint32_t>{};
            }
        }
        return m;
    };

    std::map<std::string, uint32_t> feat_id, reg_id, syn_id, reloc_id,
        sec_id, link_id, mnem_id, enc_id;

    const Value *features = doc.find("features");
    const Value *registers = doc.find("registers");
    const Value *syntaxes = doc.find("syntaxes");
    const Value *relocs = doc.find("relocs");
    const Value *sections = doc.find("sections");
    const Value *linkages = doc.find("linkages");
    const Value *mnemonics = doc.find("mnemonics");
    const Value *encoders = doc.find("encoders");
    const Value *forms = doc.find("forms");
    const Value *targets = doc.find("targets");
    const Value *debug = doc.find("debug");

    std::string map_diag;
    if (features) feat_id = index_map_at(*features, base_features, "features", map_diag);
    if (map_diag.empty() && registers) reg_id = index_map_at(*registers, base_registers, "registers", map_diag);
    if (map_diag.empty() && syntaxes) syn_id = index_map_at(*syntaxes, base_syntaxes, "syntaxes", map_diag);
    if (map_diag.empty() && relocs) reloc_id = index_map_at(*relocs, base_relocs, "relocs", map_diag);
    if (map_diag.empty() && sections) sec_id = index_map_at(*sections, base_sections, "sections", map_diag);
    if (map_diag.empty() && linkages) link_id = index_map_at(*linkages, base_linkages, "linkages", map_diag);
    if (map_diag.empty() && mnemonics) mnem_id = index_map_at(*mnemonics, base_mnemonics, "mnemonics", map_diag);
    if (map_diag.empty() && encoders) enc_id = index_map_at(*encoders, base_encoders, "encoders", map_diag);
    if (!map_diag.empty()) return fail(map_diag);

    auto lookup = [](const std::map<std::string, uint32_t> &m,
                     const std::string &name) {
        auto it = m.find(name);
        return it != m.end() ? it->second : domsymas::kNoId;
    };

    // Form references used by the optimization sections: a mnemonic name, a
    // mnemonic name with a 1-based form index ("add#2"), or a mnemonic name
    // with the encoder that implements the form ("add/add_rr"). Every form of
    // the mnemonic matches when neither suffix is given.
    struct FormSlot {
        uint32_t form;
        std::string encoder;
    };
    std::map<std::string, std::vector<FormSlot>> mnem_forms;
    if (forms && forms->type == json::Type::Array) {
        for (size_t i = 0; i < forms->array->size(); ++i) {
            const Value &f = (*forms->array)[i];
            mnem_forms[str(f, "mnemonic")].push_back(
                FormSlot{base_forms + (uint32_t)i, str(f, "encoder")});
        }
    }
    // `diag` receives a message when the reference cannot be resolved.
    std::function<bool(const Value &, std::vector<uint32_t> &, std::string &)> resolve_forms =
        [&](const Value &v, std::vector<uint32_t> &out, std::string &diag) -> bool {
        out.clear();
        if (v.type == json::Type::Null) return true; // absent: "no form"
        if (v.type == json::Type::Array) {
            for (const auto &item : *v.array) {
                if (item.type != json::Type::String) {
                    diag = "form reference list must contain strings";
                    return false;
                }
                std::vector<uint32_t> one;
                if (!resolve_forms(item, one, diag)) return false;
                out.insert(out.end(), one.begin(), one.end());
            }
            return true;
        }
        if (v.type != json::Type::String) {
            diag = "form reference must be a string or a list of strings";
            return false;
        }
        const std::string &ref = v.string;
        if (ref.empty()) return true; // "no form" is legal for tuning hints
        std::string base = ref;
        uint32_t want_index = 0;
        std::string want_enc;
        size_t at = ref.find('#');
        if (at != std::string::npos) {
            base = ref.substr(0, at);
            want_index = (uint32_t)strtoul(ref.c_str() + at + 1, nullptr, 10);
        }
        at = ref.find('/');
        if (at != std::string::npos) {
            base = ref.substr(0, at);
            want_enc = ref.substr(at + 1);
        }
        auto it = mnem_forms.find(base);
        if (it == mnem_forms.end()) {
            diag = "unknown mnemonic '" + base + "' in form reference '" + ref + "'";
            return false;
        }
        uint32_t seen = 0;
        for (const FormSlot &s : it->second) {
            if (want_index && ++seen != want_index) continue;
            if (!want_enc.empty() && s.encoder != want_enc) continue;
            out.push_back(s.form);
        }
        if (out.empty()) {
            diag = "form reference '" + ref + "' matches no form";
            return false;
        }
        return true;
    };

    // ---- features ----
    if (features) {
        if (!require_array(doc, "features", res.error, "features")) return res;
        for (const auto &f : *features->array) {
            std::string name = str(f, "name");
            if (name.empty()) return fail("feature with empty name");
            builder.add_feature(name, (uint32_t)num(f, "flags", 0), str(f, "desc"));
        }
    }

    // ---- registers (two-phase: ids resolved via precomputed name->index) ----
    if (registers) {
        if (!require_array(doc, "registers", res.error, "registers")) return res;
        for (const auto &r : *registers->array) {
            RegisterRec rec;
            rec.name = str(r, "name");
            if (rec.name.empty()) return fail("register with empty name");
            uint32_t cls = 0;
            if (!reg_class(str(r, "class"), cls))
                return fail("register '" + rec.name + "' has unknown class");
            rec.regclass = cls;
            rec.width = (uint32_t)num(r, "width", 0);
            rec.enc_num = (uint32_t)num(r, "enc", 0);
            rec.sub_lo = (uint32_t)num(r, "sub_lo", 0);
            rec.sub_hi = (uint32_t)num(r, "sub_hi", 0);
            rec.parent = domsymas::kNoParent;
            if (const Value *p = r.find("parent"); p && p->type == json::Type::String &&
                !p->string.empty()) {
                rec.parent = lookup(reg_id, p->string);
                if (rec.parent == domsymas::kNoId)
                    return fail("register '" + rec.name + "' has unknown parent '" +
                                p->string + "'");
            }
            std::string dg;
            rec.flags = decode_flags(r, "flags", reg_flag, dg, "register");
            if (!dg.empty()) return fail(dg);
            builder.add_register(std::move(rec));
        }
    }

    // ---- syntaxes ----
    if (syntaxes) {
        if (!require_array(doc, "syntaxes", res.error, "syntaxes")) return res;
        for (const auto &s : *syntaxes->array) {
            SyntaxRec rec;
            rec.name = str(s, "name");
            rec.operand_order = (uint32_t)num(s, "order", 0);
            rec.reg_prefix = str(s, "reg_prefix");
            rec.imm_prefix = str(s, "imm_prefix");
            rec.comment = str(s, "comment");
            rec.separator = str(s, "separator");
            rec.tmpl = str(s, "tmpl");
            rec.case_sensitive = flag(s, "ci") ? 1 : 0;
            builder.add_syntax(std::move(rec));
        }
    }

    // ---- relocations ----
    if (relocs) {
        if (!require_array(doc, "relocs", res.error, "relocs")) return res;
        for (const auto &r : *relocs->array) {
            std::string name = str(r, "name");
            if (name.empty()) return fail("relocation with empty name");
            uint32_t ad = 0, ov = 0;
            if (!addend_kind(str(r, "addend"), ad))
                return fail("relocation '" + name + "' has unknown addend kind");
            if (!overflow_kind(str(r, "overflow"), ov))
                return fail("relocation '" + name + "' has unknown overflow kind");
            builder.add_reloc(name, (uint32_t)num(r, "width", 0),
                              (uint32_t)num(r, "offset", 0), flag(r, "signed") ? 1 : 0,
                              flag(r, "pcrel") ? 1 : 0, ov, ad, str(r, "expr"));
        }
    }

    // ---- sections ----
    if (sections) {
        if (!require_array(doc, "sections", res.error, "sections")) return res;
        for (const auto &s : *sections->array) {
            std::string name = str(s, "name");
            if (name.empty()) return fail("section with empty name");
            uint32_t content = 0;
            if (!section_content(str(s, "content"), content))
                return fail("section '" + name + "' has unknown content");
            std::string dg;
            uint32_t fl = decode_flags(s, "flags", section_flag, dg, "section");
            if (!dg.empty()) return fail(dg);
            builder.add_section(name, fl, (uint32_t)num(s, "align", 1),
                                (uint32_t)num(s, "addrspace", 0), content);
        }
    }

    // ---- linkages ----
    if (linkages) {
        if (!require_array(doc, "linkages", res.error, "linkages")) return res;
        for (const auto &l : *linkages->array) {
            std::string name = str(l, "name");
            if (name.empty()) return fail("linkage with empty name");
            uint32_t b = 0, v = 0, t = 0;
            if (!binding(str(l, "binding"), b))
                return fail("linkage '" + name + "' has unknown binding");
            if (!visibility(str(l, "visibility"), v))
                return fail("linkage '" + name + "' has unknown visibility");
            if (!symtype(str(l, "symtype"), t))
                return fail("linkage '" + name + "' has unknown symtype");
            builder.add_linkage(name, b, v, t, (uint32_t)num(l, "flags", 0));
        }
    }

    // ---- mnemonics ----
    if (mnemonics) {
        if (!require_array(doc, "mnemonics", res.error, "mnemonics")) return res;
        for (const auto &m : *mnemonics->array) {
            std::string name = str(m, "name");
            if (name.empty()) return fail("mnemonic with empty name");
            std::vector<std::string> aliases = names(m, "aliases");
            uint32_t fl = flag(m, "ci") ? 1 : 0;
            builder.add_mnemonic(name, aliases, fl);
        }
    }

    // ---- encoders ----
    if (encoders) {
        if (!require_array(doc, "encoders", res.error, "encoders")) return res;
        for (const auto &e : *encoders->array) {
            std::string name = str(e, "name");
            if (name.empty()) return fail("encoder with empty name");
            builder.add_encoder(name, str(e, "source"), (uint32_t)num(e, "flags", 0),
                                (uint32_t)num(e, "max_bytes", 64),
                                (uint32_t)num(e, "max_relocs", 8));
        }
    }

    // ---- forms + operands + links ----
    if (forms) {
        if (!require_array(doc, "forms", res.error, "forms")) return res;
        for (const auto &f : *forms->array) {
            std::string mnem = str(f, "mnemonic");
            std::string enc = str(f, "encoder");
            uint32_t mid = lookup(mnem_id, mnem);
            if (mid == domsymas::kNoId)
                return fail("form references unknown mnemonic '" + mnem + "'");
            uint32_t eid = lookup(enc_id, enc);
            if (eid == domsymas::kNoId)
                return fail("form references unknown encoder '" + enc + "'");

            std::vector<uint32_t> req, forb, syns;
            for (const auto &n : names(f, "required")) {
                uint32_t id = lookup(feat_id, n);
                if (id == domsymas::kNoId)
                    return fail("form required feature '" + n + "' unknown");
                req.push_back(id);
            }
            for (const auto &n : names(f, "forbidden")) {
                uint32_t id = lookup(feat_id, n);
                if (id == domsymas::kNoId)
                    return fail("form forbidden feature '" + n + "' unknown");
                forb.push_back(id);
            }
            for (const auto &n : names(f, "syntaxes")) {
                uint32_t id = lookup(syn_id, n);
                if (id == domsymas::kNoId)
                    return fail("form references unknown syntax '" + n + "'");
                syns.push_back(id);
            }

            uint32_t op_count = (uint32_t)num(f, "ops", 0);
            const Value *oplist = f.find("operands");
            size_t nops = (oplist && oplist->type == json::Type::Array)
                              ? oplist->array->size()
                              : 0;
            if (op_count == 0 && nops > 0) op_count = (uint32_t)nops;

            std::string dg;
            uint32_t fl = decode_flags(f, "flags", form_flag, dg, "form");
            if (!dg.empty()) return fail(dg);

            uint32_t fid = builder.add_form(mid, op_count, fl, eid, req, forb, syns);

            if (oplist && oplist->type == json::Type::Array) {
                for (size_t i = 0; i < oplist->array->size(); ++i) {
                    const Value &o = (*oplist->array)[i];
                    OperandRec rec;
                    rec.form = fid;
                    rec.position = (uint32_t)i;
                    std::string kind = str(o, "kind");
                    uint32_t kindnum = 0;
                    if (!operand_kind(kind, kindnum))
                        return fail("operand has unknown kind '" + kind + "'");
                    rec.kind = kindnum;
                    rec.width = (uint32_t)num(o, "width", 0);
                    rec.regclass = 0;
                    if (const Value *c = o.find("class"); c && c->type == json::Type::String &&
                        !c->string.empty()) {
                        uint32_t cls = 0;
                        if (!reg_class(c->string, cls))
                            return fail("operand has unknown class '" + c->string + "'");
                        rec.regclass = cls;
                    }
                    rec.imm_min = num(o, "imm_min", 0);
                    rec.imm_max = num(o, "imm_max", 0);
                    rec.is_signed = flag(o, "signed") ? 1 : 0;
                    rec.enc_role = (uint32_t)num(o, "role", 0);
                    uint32_t acc = 1;
                    if (!access_kind(str(o, "access"), acc))
                        return fail("operand has unknown access");
                    rec.access = acc;
                    rec.hidden = flag(o, "hidden") ? 1 : 0;
                    rec.constraint = str(o, "constraint");
                    builder.add_operand(std::move(rec));
                }
            }

            builder.link_mnemonic_form(mid, fid);
            for (uint32_t sid : syns) builder.link_syntax_form(sid, fid);
        }
    }

    // ---- machine models (per-model optimization metadata) ----
    std::map<std::string, uint32_t> model_id;
    const Value *models = doc.find("models");
    if (models) {
        if (!require_array(doc, "models", res.error, "models")) return res;
        model_id = index_map_at(*models, base_models, "models", res.error);
        if (!res.error.empty()) return res;
        for (const auto &m : *models->array) {
            ModelRec rec;
            rec.name = str(m, "name");
            if (rec.name.empty()) return fail("model with empty name");
            rec.vendor = str(m, "vendor");
            rec.issue_width = (uint32_t)num(m, "issue_width", 1);
            if (rec.issue_width == 0) rec.issue_width = 1;
            rec.micro_op_buffer = (uint32_t)num(m, "rob", 0);
            rec.loop_buffer = (uint32_t)num(m, "loop_buffer", 0);
            rec.load_latency = (uint32_t)num(m, "load_latency", 0);
            rec.mispredict_penalty = (uint32_t)num(m, "mispredict", 0);
            rec.high_latency = (uint32_t)num(m, "high_latency", 0);
            rec.high_uops = (uint32_t)num(m, "high_uops", 0);
            std::string dg;
            rec.flags = decode_flags(m, "flags", model_flag, dg, "model");
            if (!dg.empty()) return fail(dg);
            for (const auto &n : names(m, "features")) {
                uint32_t id = lookup(feat_id, n);
                if (id == domsymas::kNoId)
                    return fail("model '" + rec.name + "' references unknown feature '" + n + "'");
                rec.features.push_back(id);
            }
            for (const auto &n : names(m, "tune")) {
                uint32_t id = lookup(feat_id, n);
                if (id == domsymas::kNoId)
                    return fail("model '" + rec.name +
                                "' references unknown tuning feature '" + n + "'");
                rec.tune_features.push_back(id);
            }
            rec.desc = str(m, "desc");
            builder.add_model(std::move(rec));
        }
    }

    // ---- functional units / pipeline resources ----
    std::map<std::string, uint32_t> unit_id;
    const Value *units = doc.find("units");
    if (units) {
        if (!require_array(doc, "units", res.error, "units")) return res;
        unit_id = index_map_at(*units, base_units, "units", res.error);
        if (!res.error.empty()) return res;
        for (const auto &u : *units->array) {
            UnitRec rec;
            rec.name = str(u, "name");
            if (rec.name.empty()) return fail("unit with empty name");
            std::string model = str(u, "model");
            if (!model.empty()) {
                uint32_t id = lookup(model_id, model);
                if (id == domsymas::kNoId)
                    return fail("unit '" + rec.name + "' references unknown model '" + model + "'");
                rec.model = id;
            }
            if (!unit_kind(str(u, "kind"), rec.kind))
                return fail("unit '" + rec.name + "' has unknown kind");
            rec.count = (uint32_t)num(u, "count", 1);
            if (rec.count == 0) rec.count = 1;
            rec.latency = (uint32_t)num(u, "latency", 0);
            rec.throughput = (uint32_t)num(u, "throughput", 1);
            if (rec.throughput == 0) rec.throughput = 1;
            std::string dg;
            rec.flags = decode_flags(u, "flags", unit_flag, dg, "unit");
            if (!dg.empty()) return fail(dg);
            builder.add_unit(std::move(rec));
        }
    }

    // ---- instruction selection ----
    const Value *isels = doc.find("isels");
    if (isels) {
        if (!require_array(doc, "isels", res.error, "isels")) return res;
        for (const auto &i : *isels->array) {
            IselRec rec;
            rec.node = str(i, "node");
            if (rec.node.empty()) return fail("instruction-selection record with empty node");
            if (!isel_kind(str(i, "kind"), rec.kind))
                return fail("instruction-selection record '" + rec.node + "' has unknown kind");
            std::string dg;
            std::vector<uint32_t> forms_of;
            if (!resolve_forms(field(i, "form"), forms_of, dg))
                return fail("instruction-selection record '" + rec.node + "': " + dg);
            for (uint32_t f : forms_of) {
                IselRec one = rec;
                one.form = f;
                one.cost = (uint32_t)num(i, "cost", 0);
                one.complexity = (uint32_t)num(i, "complexity", 0);
                one.priority = (uint32_t)num(i, "priority", 0);
                one.flags = decode_flags(i, "flags", isel_flag, dg, "isel");
                if (!dg.empty()) return fail(dg);
                for (const auto &n : names(i, "required")) {
                    uint32_t id = lookup(feat_id, n);
                    if (id == domsymas::kNoId)
                        return fail("instruction-selection record '" + rec.node +
                                    "' references unknown required feature '" + n + "'");
                    one.required.push_back(id);
                }
                for (const auto &n : names(i, "forbidden")) {
                    uint32_t id = lookup(feat_id, n);
                    if (id == domsymas::kNoId)
                        return fail("instruction-selection record '" + rec.node +
                                    "' references unknown forbidden feature '" + n + "'");
                    one.forbidden.push_back(id);
                }
                for (const auto &n : names(i, "alts")) {
                    std::vector<uint32_t> alts;
                    if (!resolve_forms(json::Value::string_value(n), alts, dg))
                        return fail("instruction-selection record '" + rec.node +
                                    "' alternative: " + dg);
                    for (uint32_t a : alts)
                        if (a != f) one.alts.push_back(a);
                }
                one.pattern = str(i, "pattern");
                builder.add_isel(std::move(one));
            }
        }
    }

    // ---- scheduling ----
    const Value *scheds = doc.find("scheds");
    if (scheds) {
        if (!require_array(doc, "scheds", res.error, "scheds")) return res;
        for (const auto &s : *scheds->array) {
            SchedRec base;
            base.name = str(s, "name");
            base.latency = (uint32_t)num(s, "latency", 0);
            base.uops = (uint32_t)num(s, "uops", 1);
            if (base.uops == 0) base.uops = 1;
            base.cycles = (uint32_t)num(s, "cycles", 0);
            std::string dg;
            base.flags = decode_flags(s, "flags", sched_flag, dg, "sched");
            if (!dg.empty()) return fail(dg);
            for (const auto &n : names(s, "writes")) {
                uint32_t id = lookup(unit_id, n);
                if (id == domsymas::kNoId)
                    return fail("scheduling record '" + base.name +
                                "' references unknown unit '" + n + "'");
                base.writes.push_back(id);
            }
            for (const auto &n : names(s, "reads")) {
                uint32_t id = lookup(unit_id, n);
                if (id == domsymas::kNoId)
                    return fail("scheduling record '" + base.name +
                                "' references unknown unit '" + n + "'");
                base.reads.push_back(id);
            }
            std::vector<uint32_t> forms_of;
            if (!resolve_forms(field(s, "form"), forms_of, dg))
                return fail("scheduling record '" + base.name + "': " + dg);
            for (uint32_t f : forms_of) {
                SchedRec one = base;
                one.form = f;
                builder.add_sched(std::move(one));
            }
        }
    }

    // ---- per-model optimization hints ----
    const Value *tunes = doc.find("tunes");
    if (tunes) {
        if (!require_array(doc, "tunes", res.error, "tunes")) return res;
        for (const auto &t : *tunes->array) {
            TuneRec rec;
            std::string model = str(t, "model");
            if (model.empty()) return fail("tuning record without a model");
            uint32_t mid = lookup(model_id, model);
            if (mid == domsymas::kNoId)
                return fail("tuning record references unknown model '" + model + "'");
            rec.model = mid;
            rec.name = str(t, "name");
            if (rec.name.empty()) rec.name = str(t, "kind");
            if (!tune_kind(str(t, "kind"), rec.kind))
                return fail("tuning record '" + rec.name + "' has unknown kind");
            std::string dg;
            rec.flags = decode_flags(t, "flags", tune_flag, dg, "tune");
            if (!dg.empty()) return fail(dg);
            rec.value = num(t, "value", 0);
            std::vector<uint32_t> forms_of;
            if (!resolve_forms(field(t, "form"), forms_of, dg))
                return fail("tuning record '" + rec.name + "': " + dg);
            if (forms_of.size() > 1) rec.form = domsymas::kNoId; // ambiguous: not form-scoped
            else if (forms_of.size() == 1) rec.form = forms_of.front();
            std::vector<uint32_t> forms2;
            if (!resolve_forms(field(t, "form2"), forms2, dg))
                return fail("tuning record '" + rec.name + "' second form: " + dg);
            if (forms2.size() == 1) rec.form2 = forms2.front();
            for (const auto &n : names(t, "features")) {
                uint32_t id = lookup(feat_id, n);
                if (id == domsymas::kNoId)
                    return fail("tuning record '" + rec.name +
                                "' references unknown feature '" + n + "'");
                rec.features.push_back(id);
            }
            rec.desc = str(t, "desc");
            builder.add_tune(std::move(rec));
        }
    }

    // ---- instruction-pair fusion ----
    const Value *fusions = doc.find("fusions");
    if (fusions) {
        if (!require_array(doc, "fusions", res.error, "fusions")) return res;
        for (const auto &f : *fusions->array) {
            FusionRec rec;
            rec.name = str(f, "name");
            if (rec.name.empty()) return fail("fusion record with empty name");
            if (!fusion_kind(str(f, "kind"), rec.kind))
                return fail("fusion record '" + rec.name + "' has unknown kind");
            rec.latency = (uint32_t)num(f, "latency", 0);
            std::string dg;
            rec.flags = decode_flags(f, "flags", fusion_flag, dg, "fuse");
            if (!dg.empty()) return fail(dg);
            rec.pattern = str(f, "pattern");
            for (const auto &n : names(f, "features")) {
                uint32_t id = lookup(feat_id, n);
                if (id == domsymas::kNoId)
                    return fail("fusion record '" + rec.name +
                                "' references unknown feature '" + n + "'");
                rec.features.push_back(id);
            }
            std::vector<uint32_t> as, bs;
            if (!resolve_forms(field(f, "a"), as, dg))
                return fail("fusion record '" + rec.name + "': " + dg);
            if (!resolve_forms(field(f, "b"), bs, dg))
                return fail("fusion record '" + rec.name + "': " + dg);
            for (uint32_t x : as) {
                for (uint32_t y : bs) {
                    if (x == y) continue;
                    FusionRec one = rec;
                    one.a = x;
                    one.b = y;
                    builder.add_fusion(std::move(one));
                }
            }
        }
    }

    // ---- targets ----
    if (targets) {
        if (!require_array(doc, "targets", res.error, "targets")) return res;
        for (const auto &t : *targets->array) {
            TargetRec rec;
            rec.arch = str(t, "arch");
            rec.subarch = str(t, "subarch");
            rec.addr_size = (uint32_t)num(t, "addr_size", 0);
            rec.default_op_size = (uint32_t)num(t, "op_size", 0);
            rec.default_addr_size = (uint32_t)num(t, "addr_def", 0);
            rec.instr_align = (uint32_t)num(t, "align", 1);
            uint32_t bo = 0;
            if (!byte_order(str(t, "byte_order"), bo))
                return fail("target '" + rec.arch + "' has unknown byte_order");
            rec.byte_order = bo;
            rec.obj_format = str(t, "obj_format");
            rec.abi = str(t, "abi");
            rec.calling_conv = str(t, "calling_conv");
            for (const auto &n : names(t, "features")) {
                uint32_t id = lookup(feat_id, n);
                if (id == domsymas::kNoId)
                    return fail("target feature '" + n + "' unknown");
                rec.features.push_back(id);
            }
            builder.add_target(std::move(rec));
        }
    }

    // ---- debug records ----
    // Optimization records are referenced by their own name, not by a form.
    auto name_index = [=](const Value *list, uint32_t base) {
        std::map<std::string, uint32_t> m;
        if (list && list->type == json::Type::Array)
            for (size_t i = 0; i < list->array->size(); ++i) {
                std::string name = str((*list->array)[i], "name");
                if (!name.empty()) m[name] = (uint32_t)i + base;
            }
        return m;
    };
    if (debug) {
        if (!require_array(doc, "debug", res.error, "debug")) return res;
        for (const auto &d : *debug->array) {
            uint32_t kind = 0;
            if (!debug_kind(str(d, "kind"), kind))
                return fail("debug record has unknown kind");
            std::string ref = str(d, "ref");
            uint32_t refid = 0;
            if (!ref.empty()) {
                if (kind == 3) refid = lookup(reg_id, ref);
                else if (kind == 4) refid = lookup(feat_id, ref);
                else if (kind == 5) refid = lookup(mnem_id, ref);
                else if (kind == 8) refid = lookup(enc_id, ref);
                else if (kind == 9) refid = lookup(syn_id, ref);
                else if (kind == 10) refid = lookup(sec_id, ref);
                else if (kind == 11) refid = lookup(link_id, ref);
                else if (kind == 12) refid = lookup(reloc_id, ref);
                else if (kind == 17) refid = lookup(unit_id, ref);
                else if (kind == 18) refid = lookup(model_id, ref);
                else if (kind == 19) refid = lookup(name_index(tunes, base_tunes), ref);
                else if (kind == 20) refid = lookup(name_index(fusions, base_fusions), ref);
                else if (kind == 16) refid = lookup(name_index(scheds, base_scheds), ref);
                else if (kind == 7 || kind == 6 || kind == 15) {
                    std::string fref;
                    std::vector<uint32_t> forms_of;
                    if (!resolve_forms(field(d, "ref"), forms_of, fref)) return fail(fref);
                    if (!forms_of.empty()) refid = forms_of.front();
                }
                // target records reference no per-type id; leave 0.
            }
            builder.add_debug(str(d, "msg"), kind, refid);
        }
    }

    res.ok = true;
    res.spec_json = json::dump(doc);
    return res;
}

Result compile_document_to_bank(const std::string &document_json,
                                domsymas::Databank &out) {
    domsymas::Builder builder;
    Result res = compile_document(document_json, builder);
    if (!res.ok) return res;
    domsymas::Error e = builder.build(out);
    if (!e.ok()) {
        res.ok = false;
        res.error = "builder failed: " + e.message;
    }
    return res;
}

} // namespace domsymas::cc
