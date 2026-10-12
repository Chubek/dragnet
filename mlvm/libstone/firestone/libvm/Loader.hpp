#pragma once
#include "BytecodeCaching.hpp"
#include "Environment.hpp"

namespace firestone::vm {

class Loader {
public:
    explicit Loader(Index cache_capacity = 64) : cache_(cache_capacity) {}
    std::shared_ptr<const Module> load(std::string name, const BytecodeBytes& bytes, const BinaryLimits& limits = {}) {
        if (name.empty()) throw std::invalid_argument("empty module name");
        auto module = cache_.load(bytes, limits); modules_.insert_or_assign(std::move(name), module); return module;
    }
    std::shared_ptr<const Module> get(const std::string& name) const {
        auto found = modules_.find(name);
        if (found == modules_.end()) throw VMError("vm.module", "unknown module: " + name);
        return found->second;
    }
    bool unload(const std::string& name) { return modules_.erase(name) != 0; }
    const BytecodeCache& cache() const { return cache_; }
    void link(const Module& module, const Environment& environment) const {
        require_valid(module);
        for (const auto& declaration : module.natives)
            if (!(environment.native(declaration.first).signature == declaration.second)) throw VMError("vm.signature", "native link signature mismatch");
    }
private:
    BytecodeCache cache_;
    std::map<std::string, std::shared_ptr<const Module>> modules_;
};

} // namespace firestone::vm
