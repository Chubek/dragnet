// DomSymAs optimization queries: instruction selection, scheduling,
// per-model tuning hints, and instruction-pair fusion.
//
// These functions interpret the optional sections introduced by format 1.1
// (ISEL, SCHED, UNITS, MODELS, TUNING, FUSION). All of them tolerate banks
// that carry none of those sections: a missing record simply means "the
// producer had nothing to say", never a hard error. The only hard errors
// come from validation (dangling form/unit/model references).
//
// Ordering rules used by `select_candidates` mirror the usual
// instruction-selector rules: required/forbidden feature predicates filter
// first, then per-model preferences (AVOID removes a candidate, PREFER
// promotes it, COST replaces the cost), then the producer's own preference
// order (priority, added complexity, cost, record id).
#include "sandstone.hpp"

#include <algorithm>
#include <map>

namespace domsymas {

namespace {

bool has_feature(const std::vector<uint32_t> &enabled, uint32_t f) {
    return std::find(enabled.begin(), enabled.end(), f) != enabled.end();
}

bool has_all(const std::vector<uint32_t> &enabled, const std::vector<uint32_t> &req) {
    for (uint32_t f : req) if (!has_feature(enabled, f)) return false;
    return true;
}

bool has_none(const std::vector<uint32_t> &enabled, const std::vector<uint32_t> &forb) {
    for (uint32_t f : forb) if (has_feature(enabled, f)) return false;
    return true;
}

} // namespace

// ---- models ----

const ModelRec *Databank::find_model(const std::string &name) const {
    for (auto &m : data_.models)
        if (m.name == name) return &m;
    return nullptr;
}

uint32_t Databank::default_model() const {
    for (auto &m : data_.models)
        if (m.flags & DOMSYMAS_MF_DEFAULT) return m.id;
    return data_.models.empty() ? kNoId : data_.models.front().id;
}

// ---- instruction selection ----

const IselRec *Databank::find_isel(const std::string &node) const {
    for (auto &v : data_.isels)
        if (v.node == node) return &v;
    return nullptr;
}

std::vector<const IselRec *> Databank::isel_for_node(const std::string &node) const {
    std::vector<const IselRec *> out;
    for (auto &v : data_.isels)
        if (v.node == node) out.push_back(&v);
    return out;
}

std::vector<const IselRec *>
Databank::select_candidates(const std::string &node, const std::vector<uint32_t> &enabled,
                            uint32_t model) const {
    // Per-model preferences: form -> (skip, priority boost, cost override).
    // Several hints of the same kind for one form accumulate; the last one
    // wins for cost, as it does for a resolved schedule.
    struct Bias { bool skip = false; int64_t priority = 0; int64_t cost = -1; };
    std::map<uint32_t, Bias> bias;
    if (model != kNoId) {
        for (const TuneRec *t : tunes_for_model(model)) {
            if (t->form == kNoId) continue;
            Bias &b = bias[t->form];
            if (t->kind == (uint32_t)TuneKind::Avoid) b.skip = true;
            else if (t->kind == (uint32_t)TuneKind::Prefer)
                b.priority += (t->flags & DOMSYMAS_TF_NEGATIVE) ? -t->value : t->value;
            else if (t->kind == (uint32_t)TuneKind::Cost) b.cost = t->value;
        }
    }
    auto bias_of = [&](uint32_t form) -> const Bias * {
        auto it = bias.find(form);
        return it == bias.end() ? nullptr : &it->second;
    };

    struct Scored { const IselRec *rec; int64_t priority; uint32_t complexity, cost, id; };
    std::vector<Scored> scored;
    for (const IselRec *v : isel_for_node(node)) {
        if (!has_all(enabled, v->required) || !has_none(enabled, v->forbidden)) continue;
        if (v->form == kNoId) continue;
        const Bias *b = bias_of(v->form);
        if (b && b->skip) continue;
        Scored s{v, (int64_t)v->priority, v->complexity,
                 b && b->cost >= 0 ? (uint32_t)b->cost : v->cost, v->id};
        if (b) s.priority += b->priority;
        scored.push_back(s);
    }
    std::stable_sort(scored.begin(), scored.end(), [](const Scored &a, const Scored &b) {
        if (a.priority != b.priority) return a.priority > b.priority;
        if (a.complexity != b.complexity) return a.complexity > b.complexity;
        if (a.cost != b.cost) return a.cost < b.cost;
        return a.id < b.id;
    });
    std::vector<const IselRec *> out;
    out.reserve(scored.size());
    for (auto &s : scored) out.push_back(s.rec);
    return out;
}

uint32_t Databank::select_form(const std::string &node,
                               const std::vector<uint32_t> &enabled, uint32_t model) const {
    auto cands = select_candidates(node, enabled, model);
    return cands.empty() ? kNoId : cands.front()->form;
}

// ---- functional units ----

const UnitRec *Databank::find_unit(const std::string &name) const {
    for (auto &u : data_.units)
        if (u.name == name) return &u;
    return nullptr;
}

std::vector<const UnitRec *> Databank::units_for_model(uint32_t model) const {
    std::vector<const UnitRec *> out;
    for (auto &u : data_.units)
        if (u.model == kNoId || u.model == model) out.push_back(&u);
    return out;
}

// ---- scheduling ----

const SchedRec *Databank::find_sched(uint32_t form) const {
    for (auto &s : data_.scheds)
        if (s.form == form) return &s;
    return nullptr;
}

Databank::ScheduleInfo Databank::schedule_for_form(uint32_t form, uint32_t model) const {
    ScheduleInfo out;
    const FormRec *f = find_form(form);
    if (!f) return out;
    if (const SchedRec *s = find_sched(form)) {
        out.found = true;
        out.sched = s->id;
        out.name = s->name;
        out.latency = s->latency;
        out.uops = s->uops;
        out.cycles = s->cycles;
        out.flags = s->flags;
        out.writes = s->writes;
        out.reads = s->reads;
    }
    if (model != kNoId) {
        // Per-model overrides win over the base record (TK_OVERRIDE marks the
        // ones that replace rather than adjust; both are applied here so a
        // consumer never has to know which producer wrote which).
        int64_t v = 0;
        if (tune_value(model, form, (uint32_t)TuneKind::Latency, v))
            out.latency = (v < 0) ? 0 : (uint32_t)v;
        if (tune_value(model, form, (uint32_t)TuneKind::Uops, v))
            out.uops = (v < 0) ? 1 : (uint32_t)v;
        if (tune_value(model, form, (uint32_t)TuneKind::Cost, v)) {
            out.cost = (v < 0) ? 0 : (uint32_t)v;
            out.cost_known = true;
        }
        // No explicit record: fall back to the model's load latency for a
        // memory form, which is the single most useful default.
        if (!out.found) {
            if (f->flags & (DOMSYMAS_IF_READS_MEM | DOMSYMAS_IF_WRITES_MEM)) {
                const ModelRec *m = nullptr;
                for (auto &mm : data_.models) if (mm.id == model) m = &mm;
                if (m && m->load_latency) {
                    out.found = true;
                    out.latency = m->load_latency;
                    out.flags = DOMSYMAS_SCH_MAY_FAULT;
                }
            }
        }
    }
    return out;
}

// ---- tuning hints ----

std::vector<const TuneRec *> Databank::tunes_for_model(uint32_t model) const {
    std::vector<const TuneRec *> out;
    for (auto &t : data_.tunes)
        if (t.model == model) out.push_back(&t);
    return out;
}

std::vector<const TuneRec *> Databank::tunes_for_form(uint32_t model, uint32_t form) const {
    std::vector<const TuneRec *> out;
    for (auto &t : data_.tunes)
        if (t.model == model && (t.form == form || t.form2 == form)) out.push_back(&t);
    return out;
}

const TuneRec *Databank::find_tune(uint32_t model, uint32_t form, uint32_t kind) const {
    const TuneRec *best = nullptr;
    for (auto &t : data_.tunes) {
        if (t.model != model || t.kind != kind) continue;
        if (t.form != form) continue;
        if (!best || t.id >= best->id) best = &t; // later record wins
    }
    return best;
}

bool Databank::tune_value(uint32_t model, uint32_t form, uint32_t kind, int64_t &out) const {
    const TuneRec *t = find_tune(model, form, kind);
    if (!t) return false;
    out = t->value;
    return true;
}

// ---- instruction-pair fusion ----

const FusionRec *Databank::find_fusion(uint32_t a, uint32_t b, bool bidirect) const {
    for (auto &f : data_.fusions)
        if (f.a == a && f.b == b) return &f;
    if (bidirect)
        for (auto &f : data_.fusions)
            if (f.a == b && f.b == a) return &f;
    return nullptr;
}

std::vector<const FusionRec *> Databank::fusion_candidates(uint32_t form) const {
    std::vector<const FusionRec *> out;
    for (auto &f : data_.fusions)
        if (f.a == form || f.b == form) out.push_back(&f);
    return out;
}

} // namespace domsymas
