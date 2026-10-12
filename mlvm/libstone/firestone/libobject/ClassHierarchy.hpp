#pragma once
#include "VTable.hpp"
#include "../libtype/Imperative.hpp"

namespace firestone::object {

using ClassId = std::size_t;
struct ClassDefinition {
    std::string name;
    std::optional<ClassId> parent;
    std::vector<std::pair<std::string, type::Type>> fields;
    VTable vtable;
    bool frozen = false;
};

class ClassHierarchy {
public:
    ClassId create(std::string name, std::optional<ClassId> parent = {}) {
        if (name.empty() || names_.count(name)) throw std::invalid_argument("class name must be unique and nonempty");
        VTable table;
        if (parent) table = get(*parent).vtable;
        auto id = classes_.size();
        classes_.push_back({name, parent, {}, std::move(table), false});
        try { names_.emplace(std::move(name), id); } catch (...) { classes_.pop_back(); throw; }
        if (parent) classes_[*parent].frozen = true;
        return id;
    }
    ClassId lookup(const std::string& name) const { return names_.at(name); }
    const ClassDefinition& get(ClassId id) const { return classes_.at(id); }
    bool is_subclass(ClassId derived, ClassId base) const {
        get(base);
        while (true) { if (derived == base) return true; auto parent = get(derived).parent; if (!parent) return false; derived = *parent; }
    }
    std::vector<std::pair<std::string, type::Type>> fields(ClassId id) const {
        get(id);
        std::vector<ClassId> chain; auto current = id;
        while (true) {
            chain.push_back(current);
            if (chain.size() > classes_.size() + 1) throw std::logic_error("class hierarchy cycle");
            auto parent = classes_.at(current).parent;
            if (!parent) break;
            current = *parent;
        }
        std::vector<std::pair<std::string, type::Type>> result;
        for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
            const auto& own = classes_.at(*it).fields;
            result.insert(result.end(), own.begin(), own.end());
        }
        return result;
    }
    void add_field(ClassId id, std::string name, type::Type field_type) {
        require_mutable(id); type::validate(field_type);
        if (name.empty() || name == "$vptr") throw std::invalid_argument("invalid class field name");
        for (const auto& field : fields(id)) if (field.first == name) throw std::invalid_argument("duplicate inherited field");
        classes_[id].fields.emplace_back(std::move(name), std::move(field_type));
    }
    std::size_t define_method(ClassId id, Method method) { require_mutable(id); return classes_[id].vtable.install(std::move(method)); }
    void freeze(ClassId id) { classes_.at(id).frozen = true; }
private:
    std::vector<ClassDefinition> classes_;
    std::map<std::string, ClassId> names_;
    void require_mutable(ClassId id) const { if (get(id).frozen) throw std::logic_error("class is frozen after derivation or finalization"); }
};

} // namespace firestone::object
