#pragma once

#include <mlvm/optilisp/sexpr.hpp>
#include <filesystem>
#include <map>
#include <memory>
#include <set>
#include <span>

namespace mlvm::optilisp {

enum class Stage { hir, mir, lir };
std::string_view spelling(Stage stage);
enum class Fact { proven_true, proven_false, unknown, not_computed, invalidated };

struct Requirement {
    std::string kind, id;
};
struct AnalysisContract {
    std::set<std::string> requires_analyses, invalidates, preserves;
};
struct Kernel {
    std::string id, name, category, objective, expected_effects, limitations;
    Stage stage = Stage::hir;
    std::vector<std::string> paradigms, preconditions, forbids, correctness;
    std::vector<Requirement> requirements;
    std::set<std::string> prerequisites, forbidden_prior;
    AnalysisContract analyses;
    Expr transformation;
    std::map<std::string, std::string> documentation;
    Location location;
};
struct Registry {
    std::map<std::string, std::set<Stage>> capabilities;
    std::set<std::string> analyses;
};
struct RegistryResult {
    Registry registry;
    Diagnostics diagnostics;
    bool ok() const { return diagnostics.empty(); }
};
// Load registry forms using sfsexp. Monoids are syntactically read but not executed.
RegistryResult load_registry(const std::filesystem::path& directory);

struct LoadResult {
    std::vector<Kernel> kernels; // Stable ID order; empty on any error.
    Diagnostics diagnostics;
    bool ok() const { return diagnostics.empty(); }
};

// Transaction belongs to a host backend. Destruction before commit must roll back.
// add() must copy descriptors; commit() must atomically publish the entire batch.
class Installation {
public:
    virtual ~Installation() = default;
    virtual bool add(const Kernel&, Diagnostics&) = 0;
    virtual bool commit(Diagnostics&) = 0;
};
class KernelConsumer {
public:
    virtual ~KernelConsumer() = default;
    virtual std::unique_ptr<Installation> begin_installation(Diagnostics&) = 0;
};
class Loader {
public:
    enum class Profile { strict, blackstone };
    explicit Loader(Registry registry, ParseLimits limits = {}, Profile profile = Profile::strict);
    LoadResult load_source(std::string_view source, std::string file = "<kernel>") const;
    LoadResult load(std::span<const std::filesystem::path> files) const;
    LoadResult load_directory(const std::filesystem::path& directory) const;
    // Revalidates the catalog before calling the consumer; never installs partial loads.
    bool install(const LoadResult&, KernelConsumer&, Diagnostics&) const;
private:
    Registry registry_;
    ParseLimits limits_;
    Profile profile_;
};

// Namespaced global facts: capability:ID, analysis:ID, history:ID.
class Facts {
public:
    Fact get(std::string_view key) const;
    void set(std::string key, Fact value);
    void invalidate(const AnalysisContract&);
private:
    std::map<std::string, Fact, std::less<>> facts_;
};

} // namespace mlvm::optilisp
