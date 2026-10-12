#include <mlvm/optilisp/loader.hpp>

#include <algorithm>
#include <regex>
#include <stdexcept>

namespace mlvm::optilisp {
std::string_view spelling(Stage stage) {
    switch (stage) {
    case Stage::hir: return "hir";
    case Stage::mir: return "mir";
    case Stage::lir: return "lir";
    }
    return "invalid";
}
namespace {
void error(Diagnostics& diagnostics, std::string code, std::string message, const Location& at) {
    diagnostics.push_back({std::move(code), std::move(message), at});
}
std::string head(const Expr& expr) {
    return expr.kind == ExprKind::list && !expr.items.empty() && expr.items[0].kind == ExprKind::symbol
         ? expr.items[0].value : "";
}
bool stage_name(std::string_view text, Stage& stage) {
    if (text == "hir") stage = Stage::hir;
    else if (text == "mir") stage = Stage::mir;
    else if (text == "lir") stage = Stage::lir;
    else return false;
    return true;
}
using Fields = std::map<std::string, const Expr*>;
struct Definition { Fields keywords, sections; };
Definition fields(const Expr& form, Diagnostics& diagnostics) {
    Definition result;
    if (head(form).empty()) { error(diagnostics, "optilisp.form", "expected a named definition", form.location); return result; }
    bool body = false;
    for (std::size_t i = 1; i < form.items.size(); ++i) {
        const auto& item = form.items[i];
        if (item.kind == ExprKind::keyword && !body) {
            if (i + 1 == form.items.size() || form.items[i + 1].kind == ExprKind::keyword) {
                error(diagnostics, "optilisp.field", "keyword requires a value", item.location); continue;
            }
            if (!result.keywords.emplace(item.value.substr(1), &form.items[i + 1]).second)
                error(diagnostics, "optilisp.duplicate", "duplicate keyword " + item.value, item.location);
            ++i;
        } else if (!head(item).empty()) {
            body = true;
            if (!result.sections.emplace(head(item), &item).second)
                error(diagnostics, "optilisp.duplicate", "duplicate section " + head(item), item.location);
        } else error(diagnostics, "optilisp.field", "unexpected body atom or late keyword", item.location);
    }
    return result;
}
const Expr* find(const Fields& values, const std::string& key) {
    auto it = values.find(key);
    return it == values.end() ? nullptr : it->second;
}
std::string string_value(const Expr* expr, Diagnostics& diagnostics, const Location& location) {
    if (!expr || expr->kind != ExprKind::string || expr->value.find_first_not_of(" \t\r\n") == std::string::npos) {
        error(diagnostics, "optilisp.type", "expected a nonempty string", expr ? expr->location : location); return {};
    }
    return expr->value;
}
std::string text_section(const Expr* section, Diagnostics& diagnostics, const Location& at) {
    if (!section || section->items.size() != 2) {
        error(diagnostics, "optilisp.type", "expected a section with exactly one string", section ? section->location : at); return {};
    }
    return string_value(&section->items[1], diagnostics, at);
}
std::vector<std::string> identifiers(const Expr* section, Diagnostics& diagnostics, bool predicates = false) {
    std::vector<std::string> result;
    if (!section) return result;
    std::set<std::string> seen;
    for (std::size_t i = 1; i < section->items.size(); ++i) {
        const auto* item = &section->items[i];
        if (predicates && item->kind == ExprKind::list && item->items.size() == 1) item = &item->items[0];
        if ((item->kind != ExprKind::symbol && item->kind != ExprKind::string) || item->value.empty())
            error(diagnostics, "optilisp.type", "expected an atomic nonempty identifier", item->location);
        else {
            if (!seen.insert(item->value).second)
                error(diagnostics, "optilisp.duplicate", "duplicate identifier " + item->value, item->location);
            result.push_back(item->value);
        }
    }
    return result;
}
std::set<std::string> as_set(const std::vector<std::string>& values) { return {values.begin(), values.end()}; }
const std::set<std::string> docs = {
    "purpose", "motivation", "stage", "applicability", "preconditions", "required-information",
    "transformation", "semantic-assumptions", "correctness", "known-limitations", "expected-effects",
    "testing-strategy", "references", "recommended-tools"
};
const std::set<std::string> sections = {
    "category", "paradigms", "objective", "requires", "preconditions", "forbids", "correctness",
    "analysis-contract", "transform", "expected-effects", "limitations", "documentation",
    "prerequisites", "forbidden-prior-transformations"
};
const std::set<std::string> verbs = {
    "rewrite", "fold", "propagate", "eliminate", "hoist", "sink", "interchange", "unroll", "peel",
    "fuse", "distribute", "tile", "skew", "inline", "specialize", "devirtualize", "lower", "select",
    "combine", "promote", "demote", "coalesce", "rematerialize", "forward", "canonicalize",
    "simplify", "split", "merge", "rotate", "vectorize", "parallelize", "if-convert", "construct",
    "outline", "place", "schedule", "predict", "deduplicate", "remove", "strength-reduce"
};

Kernel decode(const Expr& form, Diagnostics& diagnostics, Loader::Profile profile, const Registry& registry) {
    Kernel kernel;
    kernel.location = form.location;
    if (head(form) != "define-kernel") { error(diagnostics, "optilisp.form", "expected define-kernel", form.location); return kernel; }
    auto def = fields(form, diagnostics);
    for (const auto& [key, expr] : def.keywords)
        if (key != "id" && key != "name" && key != "stage") error(diagnostics, "optilisp.field", "unknown keyword " + key, expr->location);
    for (const auto& [key, expr] : def.sections)
        if (!sections.contains(key)) error(diagnostics, "optilisp.field", "unknown section " + key, expr->location);
    for (const auto& key : sections)
        if (key != "prerequisites" && key != "forbidden-prior-transformations" &&
            !(key == "analysis-contract" && profile == Loader::Profile::blackstone) && !def.sections.contains(key))
            error(diagnostics, "optilisp.required", "missing section " + key, form.location);
    kernel.id = string_value(find(def.keywords, "id"), diagnostics, form.location);
    kernel.name = string_value(find(def.keywords, "name"), diagnostics, form.location);
    auto stage = find(def.keywords, "stage");
    if (!stage || stage->kind != ExprKind::symbol || !stage_name(stage->value, kernel.stage))
        error(diagnostics, "optilisp.stage", "expected hir, mir, or lir", form.location);
    auto categories = identifiers(find(def.sections, "category"), diagnostics);
    if (categories.size() != 1) error(diagnostics, "optilisp.category", "exactly one primary category required", form.location);
    else kernel.category = categories[0];
    kernel.paradigms = identifiers(find(def.sections, "paradigms"), diagnostics);
    kernel.preconditions = identifiers(find(def.sections, "preconditions"), diagnostics, true);
    kernel.forbids = identifiers(find(def.sections, "forbids"), diagnostics, true);
    kernel.correctness = identifiers(find(def.sections, "correctness"), diagnostics, true);
    kernel.objective = text_section(find(def.sections, "objective"), diagnostics, form.location);
    kernel.expected_effects = text_section(find(def.sections, "expected-effects"), diagnostics, form.location);
    kernel.limitations = text_section(find(def.sections, "limitations"), diagnostics, form.location);
    if (const auto* requires_section = find(def.sections, "requires")) {
        std::set<std::pair<std::string, std::string>> seen;
        for (std::size_t i = 1; i < requires_section->items.size(); ++i) {
            const auto& item = requires_section->items[i];
            const auto kind = head(item);
            if ((kind != "analysis" && kind != "capability") || item.items.size() != 2 ||
                (item.items[1].kind != ExprKind::symbol && item.items[1].kind != ExprKind::string) || item.items[1].value.empty()) {
                error(diagnostics, "optilisp.requirement", "expected (capability ID) or (analysis ID)", item.location); continue;
            }
            if (!seen.emplace(kind, item.items[1].value).second)
                error(diagnostics, "optilisp.duplicate", "duplicate requirement", item.location);
            kernel.requirements.push_back({kind, item.items[1].value});
        }
    }
    if (const auto* contract = find(def.sections, "analysis-contract")) {
        auto parts = fields(*contract, diagnostics);
        if (!parts.keywords.empty()) error(diagnostics, "optilisp.contract", "analysis contract has keywords", contract->location);
        for (const auto& [key, item] : parts.sections)
            if (key != "requires" && key != "invalidates" && key != "preserves")
                error(diagnostics, "optilisp.contract", "unknown analysis clause " + key, item->location);
        for (const auto& key : {"requires", "invalidates", "preserves"})
            if (!parts.sections.contains(key)) error(diagnostics, "optilisp.contract", "missing clause " + std::string(key), contract->location);
        kernel.analyses.requires_analyses = as_set(identifiers(find(parts.sections, "requires"), diagnostics));
        kernel.analyses.invalidates = as_set(identifiers(find(parts.sections, "invalidates"), diagnostics));
        kernel.analyses.preserves = as_set(identifiers(find(parts.sections, "preserves"), diagnostics));
    }
    if (const auto* transform = find(def.sections, "transform")) {
        if (transform->items.size() != 2 || head(transform->items[1]).empty())
            error(diagnostics, "optilisp.transform", "exactly one transformation form required", transform->location);
        else kernel.transformation = transform->items[1];
    }
    if (const auto* documentation = find(def.sections, "documentation")) {
        auto parts = fields(*documentation, diagnostics);
        if (!parts.keywords.empty()) error(diagnostics, "optilisp.documentation", "documentation has keywords", documentation->location);
        for (const auto& [key, section] : parts.sections) {
            if (!docs.contains(key)) error(diagnostics, "optilisp.documentation", "unknown topic " + key, section->location);
            kernel.documentation[key] = text_section(section, diagnostics, documentation->location);
        }
    }
    if (const auto* prior = find(def.sections, "prerequisites")) {
        for (std::size_t i = 1; i < prior->items.size(); ++i) {
            const auto& item = prior->items[i];
            if (head(item) != "optimization-applied" || item.items.size() != 2 || item.items[1].kind != ExprKind::string)
                error(diagnostics, "optilisp.history", "expected (optimization-applied STRING)", item.location);
            else if (!kernel.prerequisites.insert(item.items[1].value).second)
                error(diagnostics, "optilisp.duplicate", "duplicate prerequisite", item.location);
        }
    }
    kernel.forbidden_prior = as_set(identifiers(find(def.sections, "forbidden-prior-transformations"), diagnostics));
    if (profile == Loader::Profile::blackstone && !def.sections.contains("analysis-contract")) {
        for (const auto& requirement : kernel.requirements)
            if (requirement.kind == "analysis") kernel.analyses.requires_analyses.insert(requirement.id);
        // The upstream schema omits this section: consume declared analyses and
        // conservatively invalidate every registered analysis; preserve none.
        kernel.analyses.invalidates = registry.analyses;
    }
    return kernel;
}

void validate(std::span<const Kernel> kernels, const Registry& registry, Diagnostics& diagnostics, Loader::Profile profile) {
    std::map<std::string, const Kernel*> ids;
    std::map<std::string, std::string> fingerprints;
    static const std::regex id_pattern(R"(blackstone\.(hir|mir|lir)\.[a-z0-9-]+\.[a-z0-9.-]+)");
    for (const auto& kernel : kernels) {
        auto fail = [&](std::string code, std::string message) { error(diagnostics, std::move(code), kernel.id + ": " + message, kernel.location); };
        if (!ids.emplace(kernel.id, &kernel).second) fail("optilisp.duplicate", "duplicate kernel ID");
        if (kernel.id.size() > 256 || !std::regex_match(kernel.id, id_pattern) || !kernel.id.starts_with("blackstone." + std::string(spelling(kernel.stage)) + "."))
            fail("optilisp.id", "invalid ID or mismatched stage");
        if (kernel.name.empty() || kernel.category.empty() || kernel.objective.empty() || kernel.expected_effects.empty() || kernel.limitations.empty() ||
            kernel.preconditions.empty() || kernel.forbids.empty() || kernel.correctness.empty() || kernel.paradigms.empty())
            fail("optilisp.required", "mandatory metadata is empty");
        for (const auto& topic : docs)
            if ((profile == Loader::Profile::strict || topic == "purpose" || topic == "motivation" ||
                 topic == "applicability" || topic == "semantic-assumptions" || topic == "known-limitations" || topic == "testing-strategy") &&
                (!kernel.documentation.contains(topic) || kernel.documentation.at(topic).empty())) fail("optilisp.documentation", "missing topic " + topic);
        for (const auto& condition : kernel.preconditions)
            if (std::find(kernel.forbids.begin(), kernel.forbids.end(), condition) != kernel.forbids.end()) fail("optilisp.contradiction", "required and forbidden condition " + condition);
        std::set<std::string> consumed;
        bool capability = false;
        for (const auto& requirement : kernel.requirements) {
            if (requirement.kind == "capability") {
                capability = true;
                auto found = registry.capabilities.find(requirement.id);
                if (found == registry.capabilities.end()) fail("optilisp.reference", "unknown capability " + requirement.id);
                else if (!found->second.contains(kernel.stage)) fail("optilisp.stage", "capability incompatible with stage: " + requirement.id);
            } else if (requirement.kind == "analysis") {
                consumed.insert(requirement.id);
                if (!registry.analyses.contains(requirement.id)) fail("optilisp.reference", "unknown analysis " + requirement.id);
            } else fail("optilisp.requirement", "unknown requirement kind");
        }
        if (!capability) fail("optilisp.required", "IR capability declaration required");
        if (consumed != kernel.analyses.requires_analyses) fail("optilisp.contract", "consumed analyses do not match analysis contract");
        if (kernel.analyses.invalidates.empty()) fail("optilisp.contract", "explicit invalidation required");
        for (const auto* group : {&kernel.analyses.requires_analyses, &kernel.analyses.invalidates, &kernel.analyses.preserves})
            for (const auto& analysis : *group)
                if (!registry.analyses.contains(analysis)) fail("optilisp.reference", "unknown analysis " + analysis);
        for (const auto& analysis : kernel.analyses.invalidates)
            if (kernel.analyses.preserves.contains(analysis)) fail("optilisp.contradiction", "analysis invalidated and preserved: " + analysis);
        for (const auto& prior : kernel.prerequisites)
            if (kernel.forbidden_prior.contains(prior)) fail("optilisp.contradiction", "history required and forbidden: " + prior);
        const auto verb = head(kernel.transformation);
        if (!verbs.contains(verb) || kernel.transformation.items.size() < 2) fail("optilisp.transform", "unknown or empty transformation");
        else if ((verb == "rewrite" || verb == "fold") && kernel.transformation.items.size() != 3) fail("optilisp.transform", "rewrite/fold requires pattern and result");
        if ((kernel.stage != Stage::lir && (verb == "select" || verb == "lower" || verb == "rematerialize")) ||
            (kernel.stage == Stage::lir && (verb == "inline" || verb == "construct" || verb == "vectorize" || verb == "parallelize" || verb == "interchange")))
            fail("optilisp.stage", "transformation incompatible with stage");
        auto predicates = kernel.preconditions, forbidden = kernel.forbids;
        std::sort(predicates.begin(), predicates.end()); std::sort(forbidden.begin(), forbidden.end());
        Expr fingerprint{ExprKind::list, {}, {}, {}};
        fingerprint.items.push_back(Expr{ExprKind::symbol, std::string(spelling(kernel.stage)), {}, {}});
        fingerprint.items.push_back(kernel.transformation);
        for (const auto& group : {predicates, forbidden}) {
            Expr part{ExprKind::list, {}, {}, {}};
            for (const auto& value : group) part.items.push_back(Expr{ExprKind::string, value, {}, {}});
            fingerprint.items.push_back(std::move(part));
        }
        auto [found, inserted] = fingerprints.emplace(fingerprint.print(), kernel.id);
        if (!inserted) fail("optilisp.redundancy", "duplicate contract of " + found->second);
    }
    std::map<std::string, std::size_t> degree;
    std::map<std::string, std::vector<std::string>> users;
    for (const auto& [id, kernel] : ids) {
        degree[id] = 0;
        for (const auto* group : {&kernel->prerequisites, &kernel->forbidden_prior})
            for (const auto& dependency : *group)
                if (!ids.contains(dependency)) error(diagnostics, "optilisp.reference", "unresolved kernel " + dependency, kernel->location);
        for (const auto& dependency : kernel->prerequisites) if (ids.contains(dependency)) {
            ++degree[id]; users[dependency].push_back(id);
        }
    }
    std::set<std::string> ready;
    for (const auto& [id, count] : degree) if (!count) ready.insert(id);
    while (!ready.empty()) {
        const auto id = *ready.begin(); ready.erase(ready.begin());
        for (const auto& user : users[id]) if (--degree[user] == 0) ready.insert(user);
    }
    for (const auto& [id, count] : degree)
        if (count) error(diagnostics, "optilisp.cycle", "cyclic or cycle-dependent prerequisite: " + id, ids.at(id)->location);
}
std::vector<std::filesystem::path> discover(const std::filesystem::path& root, Diagnostics& diagnostics) {
    std::vector<std::filesystem::path> result;
    try {
        if (!std::filesystem::is_directory(root)) throw std::runtime_error("not a directory");
        for (const auto& entry : std::filesystem::recursive_directory_iterator(root))
            if (entry.is_regular_file() && entry.path().extension() == ".olisp") result.push_back(entry.path());
        std::sort(result.begin(), result.end());
    } catch (const std::exception& failure) {
        error(diagnostics, "optilisp.io", failure.what(), {root.string(), 1, 1});
    }
    return result;
}
} // namespace

RegistryResult load_registry(const std::filesystem::path& directory) {
    RegistryResult result;
    std::set<std::string> ids;
    for (const auto& path : discover(directory, result.diagnostics)) {
        auto parsed = parse_file(path.string());
        result.diagnostics.insert(result.diagnostics.end(), parsed.diagnostics.begin(), parsed.diagnostics.end());
        for (const auto& form : parsed.forms) {
            const auto kind = head(form);
            if (kind != "define-capability" && kind != "define-analysis" && kind != "define-monoid") {
                error(result.diagnostics, "optilisp.registry", "unknown registry form", form.location); continue;
            }
            auto def = fields(form, result.diagnostics);
            auto id = string_value(find(def.keywords, "id"), result.diagnostics, form.location);
            if (!ids.insert(id).second) error(result.diagnostics, "optilisp.duplicate", "duplicate registry ID " + id, form.location);
            if (kind == "define-analysis") result.registry.analyses.insert(id);
            else if (kind == "define-capability") {
                const auto* stages = find(def.keywords, "stages");
                if (!stages || stages->kind != ExprKind::list || stages->items.empty()) {
                    error(result.diagnostics, "optilisp.stage", "capability requires a stages list", form.location); continue;
                }
                std::set<Stage> values;
                for (const auto& item : stages->items) {
                    Stage stage;
                    if (item.kind != ExprKind::symbol || !stage_name(item.value, stage)) error(result.diagnostics, "optilisp.stage", "unknown capability stage", item.location);
                    else if (!values.insert(stage).second) error(result.diagnostics, "optilisp.duplicate", "duplicate stage", item.location);
                }
                result.registry.capabilities[id] = std::move(values);
            }
        }
    }
    if (result.registry.capabilities.empty()) error(result.diagnostics, "optilisp.registry", "registry contains no capabilities", {directory.string(), 1, 1});
    if (!result.ok()) result.registry = {};
    return result;
}

Loader::Loader(Registry registry, ParseLimits limits, Profile profile)
    : registry_(std::move(registry)), limits_(limits), profile_(profile) {}
LoadResult Loader::load_source(std::string_view source, std::string file) const {
    auto parsed = parse(source, std::move(file), limits_);
    LoadResult result{{}, std::move(parsed.diagnostics)};
    if (parsed.forms.empty() && result.ok()) error(result.diagnostics, "optilisp.form", "empty kernel source", {});
    for (const auto& form : parsed.forms) result.kernels.push_back(decode(form, result.diagnostics, profile_, registry_));
    validate(result.kernels, registry_, result.diagnostics, profile_);
    std::sort(result.kernels.begin(), result.kernels.end(), [](const Kernel& a, const Kernel& b) { return a.id < b.id; });
    if (!result.ok()) result.kernels.clear();
    return result;
}
LoadResult Loader::load(std::span<const std::filesystem::path> files) const {
    LoadResult result;
    std::vector<std::filesystem::path> ordered(files.begin(), files.end());
    std::sort(ordered.begin(), ordered.end());
    for (const auto& path : ordered) {
        auto parsed = parse_file(path.string(), limits_);
        result.diagnostics.insert(result.diagnostics.end(), parsed.diagnostics.begin(), parsed.diagnostics.end());
        if (parsed.ok() && parsed.forms.empty()) error(result.diagnostics, "optilisp.form", "empty kernel source", {path.string(), 1, 1});
        for (const auto& form : parsed.forms) result.kernels.push_back(decode(form, result.diagnostics, profile_, registry_));
    }
    if (ordered.empty()) error(result.diagnostics, "optilisp.catalog", "empty kernel catalog", {});
    validate(result.kernels, registry_, result.diagnostics, profile_);
    std::sort(result.kernels.begin(), result.kernels.end(), [](const Kernel& a, const Kernel& b) { return a.id < b.id; });
    if (!result.ok()) result.kernels.clear();
    return result;
}
LoadResult Loader::load_directory(const std::filesystem::path& directory) const {
    Diagnostics diagnostics;
    const auto files = discover(directory, diagnostics);
    if (!diagnostics.empty()) return {{}, std::move(diagnostics)};
    return load(files);
}
bool Loader::install(const LoadResult& result, KernelConsumer& consumer, Diagnostics& diagnostics) const {
    if (!result.ok()) {
        diagnostics.insert(diagnostics.end(), result.diagnostics.begin(), result.diagnostics.end()); return false;
    }
    Diagnostics checks;
    if (result.kernels.empty()) error(checks, "optilisp.install", "cannot install an empty catalog", {});
    validate(result.kernels, registry_, checks, profile_);
    if (!checks.empty()) { diagnostics.insert(diagnostics.end(), checks.begin(), checks.end()); return false; }
    try {
        const auto diagnostic_start = diagnostics.size();
        auto transaction = consumer.begin_installation(diagnostics);
        if (!transaction) { error(diagnostics, "optilisp.install", "backend refused installation", {}); return false; }
        if (diagnostics.size() != diagnostic_start) return false;
        for (const auto& kernel : result.kernels)
            if (!transaction->add(kernel, diagnostics) || diagnostics.size() != diagnostic_start) return false;
        return transaction->commit(diagnostics);
    } catch (const std::exception& failure) {
        error(diagnostics, "optilisp.install", failure.what(), {}); return false;
    }
}
Fact Facts::get(std::string_view key) const {
    auto found = facts_.find(key);
    return found == facts_.end() ? Fact::not_computed : found->second;
}
void Facts::set(std::string key, Fact value) { facts_[std::move(key)] = value; }
void Facts::invalidate(const AnalysisContract& contract) {
    for (auto& [key, value] : facts_)
        if (key.starts_with("analysis:") && !contract.preserves.contains(key.substr(9))) value = Fact::invalidated;
    for (const auto& id : contract.invalidates) facts_["analysis:" + id] = Fact::invalidated;
}
} // namespace mlvm::optilisp
