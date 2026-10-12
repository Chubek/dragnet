#pragma once
#include "../libir/Common.hpp"

namespace firestone::sema {

using ScopeId = std::size_t;
struct Symbol {
    std::string name;
    ir::Type type;
    bool mutable_value = false;
    ir::NodeId declaration = ir::invalid_id;
};

class SymbolTable {
public:
    SymbolTable() { scopes_.push_back({{}, {}}); }
    ScopeId root() const { return 0; }
    ScopeId child(ScopeId parent) { (void)scopes_.at(parent); scopes_.push_back({parent, {}}); return scopes_.size() - 1; }
    void declare(ScopeId scope, Symbol symbol) {
        auto& entries = scopes_.at(scope).symbols;
        if (symbol.name.empty() || !symbol.type.valid_value()) throw std::invalid_argument("invalid symbol declaration");
        if (entries.count(symbol.name)) throw std::invalid_argument("duplicate symbol in scope");
        auto name = symbol.name;
        entries.emplace(std::move(name), std::move(symbol));
    }
    const Symbol* lookup_local(ScopeId scope, const std::string& name) const {
        const auto& entries = scopes_.at(scope).symbols;
        auto found = entries.find(name); return found == entries.end() ? nullptr : &found->second;
    }
    const Symbol* lookup(ScopeId scope, const std::string& name) const {
        while (true) {
            if (auto symbol = lookup_local(scope, name)) return symbol;
            auto parent = scopes_.at(scope).parent;
            if (!parent) return nullptr;
            scope = *parent;
        }
    }
    std::vector<Symbol> visible(ScopeId scope) const {
        std::map<std::string, Symbol> result;
        while (true) {
            for (const auto& item : scopes_.at(scope).symbols) result.emplace(item);
            auto parent = scopes_.at(scope).parent;
            if (!parent) break;
            scope = *parent;
        }
        std::vector<Symbol> symbols;
        for (const auto& item : result) symbols.push_back(item.second);
        return symbols;
    }
private:
    struct Scope { std::optional<ScopeId> parent; std::map<std::string, Symbol> symbols; };
    std::vector<Scope> scopes_;
};

} // namespace firestone::sema
