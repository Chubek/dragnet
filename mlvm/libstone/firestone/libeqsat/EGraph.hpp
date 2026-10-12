#pragma once

#include "../librewrite/Term.hpp"
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <unordered_map>

namespace firestone::eqsat {

using EClassId = std::size_t;
struct ENode {
    std::string op;
    std::vector<EClassId> children;
    friend bool operator==(const ENode& a, const ENode& b) { return a.op == b.op && a.children == b.children; }
    friend bool operator<(const ENode& a, const ENode& b) { return a.op != b.op ? a.op < b.op : a.children < b.children; }
};
struct ENodeHash {
    std::size_t operator()(const ENode& n) const {
        auto hash = std::hash<std::string>{}(n.op);
        for (auto c : n.children) hash ^= std::hash<EClassId>{}(c) + 0x9e3779b9u + (hash << 6u) + (hash >> 2u);
        return hash;
    }
};
class NodeLimitReached : public std::runtime_error {
public:
    NodeLimitReached() : std::runtime_error("e-graph node allocation limit reached") {}
};

struct Extraction { rewrite::Term term; double cost = 0; };

class EGraph {
public:
    EClassId find(EClassId id) const {
        if (id >= classes_.size()) throw std::out_of_range("invalid e-class");
        while (classes_[id].parent != id) id = classes_[id].parent;
        return id;
    }
    bool equivalent(EClassId a, EClassId b) { rebuild(); return find(a) == find(b); }
    EClassId add(ENode node, std::size_t allocation_limit = std::numeric_limits<std::size_t>::max()) {
        if (node.op.empty()) throw std::invalid_argument("e-node operator is empty");
        rebuild(); canonicalize(node);
        auto it = memo_.find(node);
        if (it != memo_.end()) return find(it->second);
        if (allocated_nodes_ >= allocation_limit) throw NodeLimitReached();
        auto id = classes_.size(); classes_.push_back({id, {node}}); memo_.emplace(std::move(node), id);
        ++allocated_nodes_; ++version_; return id;
    }
    EClassId add(const rewrite::Term& term, std::size_t allocation_limit = std::numeric_limits<std::size_t>::max()) {
        rewrite::validate_term(term);
        std::vector<EClassId> children;
        for (const auto& c : term.children) children.push_back(add(c, allocation_limit));
        return add(ENode{term.op, std::move(children)}, allocation_limit);
    }
    bool merge(EClassId a, EClassId b) {
        a = find(a); b = find(b); if (a == b) return false;
        // Union by class size bounds lookup depth; ties choose a stable ID.
        if (classes_[a].nodes.size() < classes_[b].nodes.size() ||
            (classes_[a].nodes.size() == classes_[b].nodes.size() && a > b)) std::swap(a, b);
        classes_[b].parent = a;
        auto& nodes = classes_[a].nodes;
        nodes.insert(nodes.end(), classes_[b].nodes.begin(), classes_[b].nodes.end()); classes_[b].nodes.clear();
        dirty_ = true; ++version_; return true;
    }
    void rebuild() {
        if (!dirty_) return;
        bool changed;
        do {
            changed = false;
            std::unordered_map<ENode, EClassId, ENodeHash> memo;
            const auto roots = classes();
            for (auto id : roots) {
                if (find(id) != id) continue;
                // Merging may append to this class, so iterate over a snapshot.
                auto nodes = classes_[id].nodes;
                for (auto node : nodes) {
                    canonicalize(node);
                    auto pair = memo.emplace(node, find(id));
                    if (!pair.second && merge(id, pair.first->second)) changed = true;
                }
            }
            memo_.clear();
            for (auto id : classes()) {
                auto& nodes = classes_[id].nodes;
                for (auto& n : nodes) canonicalize(n);
                std::sort(nodes.begin(), nodes.end()); nodes.erase(std::unique(nodes.begin(), nodes.end()), nodes.end());
                for (const auto& n : nodes) memo_[n] = id;
            }
        } while (changed);
        // Path compression is done at this explicit mutating boundary.
        for (EClassId id = 0; id < classes_.size(); ++id) classes_[id].parent = find(id);
        dirty_ = false;
    }
    std::vector<EClassId> classes() const {
        std::vector<EClassId> out;
        for (EClassId id = 0; id < classes_.size(); ++id) if (classes_[id].parent == id) out.push_back(id);
        return out;
    }
    const std::vector<ENode>& nodes(EClassId id) const {
        if (dirty_) throw std::logic_error("rebuild e-graph before inspecting nodes");
        return classes_[find(id)].nodes;
    }
    std::size_t node_count() const {
        std::size_t n = 0; for (auto id : classes()) n += classes_[id].nodes.size(); return n;
    }
    std::size_t allocated_nodes() const { return allocated_nodes_; }
    std::size_t version() const { return version_; }
    bool contains(EClassId id) const { return id < classes_.size(); }
    std::optional<EClassId> lookup(ENode node) {
        rebuild(); canonicalize(node);
        auto found = memo_.find(node);
        return found == memo_.end() ? std::nullopt : std::optional<EClassId>{find(found->second)};
    }
    std::set<EClassId> reachable_classes(EClassId root) {
        rebuild(); root = find(root);
        std::set<EClassId> result{root}; std::vector<EClassId> pending{root};
        while (!pending.empty()) {
            auto id = pending.back(); pending.pop_back();
            for (const auto& node : nodes(id)) for (auto child : node.children) {
                child = find(child);
                if (result.insert(child).second) pending.push_back(child);
            }
        }
        return result;
    }
    Extraction extract(EClassId root, std::function<double(const ENode&)> local_cost = {}) {
        rebuild(); root = find(root);
        if (!local_cost) local_cost = [](const ENode&) { return 1.0; };
        struct Best { double cost = std::numeric_limits<double>::infinity(); std::size_t size = std::numeric_limits<std::size_t>::max(); std::optional<ENode> node; };
        std::map<EClassId, Best> best;
        std::map<ENode, double> costs;
        const auto roots = classes();
        for (auto id : roots) {
            best[id];
            for (const auto& node : nodes(id)) {
                auto cost = local_cost(node);
                if (!std::isfinite(cost) || cost < 0) throw std::invalid_argument("extraction costs must be finite and nonnegative");
                costs[node] = cost;
            }
        }
        bool changed = true;
        while (changed) {
            changed = false;
            for (auto id : roots) for (const auto& node : nodes(id)) {
                double cost = costs.at(node); std::size_t size = 1;
                bool finite = true;
                for (auto c : node.children) {
                    const auto& child = best.at(find(c));
                    if (!child.node || child.size > std::numeric_limits<std::size_t>::max() - size) { finite = false; break; }
                    cost += child.cost; size += child.size;
                }
                if (!finite || !std::isfinite(cost)) continue;
                auto& old = best[id];
                if (cost < old.cost || (cost == old.cost && (size < old.size ||
                    (size == old.size && (!old.node || node < *old.node))))) {
                    old = {cost, size, node}; changed = true;
                }
            }
        }
        if (!best.at(root).node) throw std::runtime_error("e-class has no finite expression");
        std::set<EClassId> active;
        std::function<rewrite::Term(EClassId)> build = [&](EClassId id) {
            id = find(id);
            if (!active.insert(id).second) throw std::logic_error("cyclic extraction choice");
            const auto& node = *best.at(id).node;
            std::vector<rewrite::Term> children;
            for (auto c : node.children) children.push_back(build(c));
            active.erase(id); return rewrite::Term(node.op, std::move(children));
        };
        return {build(root), best.at(root).cost};
    }
private:
    struct EClass { EClassId parent; std::vector<ENode> nodes; };
    std::vector<EClass> classes_;
    std::unordered_map<ENode, EClassId, ENodeHash> memo_;
    std::size_t allocated_nodes_ = 0, version_ = 0;
    bool dirty_ = false;
    void canonicalize(ENode& node) const { for (auto& c : node.children) c = find(c); }
};

} // namespace firestone::eqsat
