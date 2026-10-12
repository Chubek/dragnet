#pragma once

#include <algorithm>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace mlvm::nodeir {

enum class Stage { hir, mir, lir };
enum class Kind { operation, symbol, keyword, string, number, boolean, nil };
enum class Effects { unknown, pure, observable };
using NodeId = std::uint64_t;
struct Location {
    std::string file;
    std::size_t line = 1, column = 1;
    bool operator==(const Location&) const = default;
};
struct Metadata {
    std::string type; // Empty means unknown, never an inferred type.
    Effects effects = Effects::unknown;
    Location location;
    std::map<std::string, std::string> attributes;
    bool operator==(const Metadata&) const = default;
};
struct Node {
    NodeId id = 0;
    Kind kind = Kind::operation;
    std::string value; // Opcode or exact literal spelling/value.
    std::vector<NodeId> operands;
    std::vector<NodeId> successors; // Explicit control edges; not expression operands.
    Metadata metadata;
    bool operator==(const Node&) const = default;
};
struct Limits {
    std::size_t max_nodes = 100000, max_depth = 128, max_bytes = 1024 * 1024;
};

// Value-owned graph with monotonic context-local IDs. SSA, typing, memory models,
// and opcode semantics are explicit host contracts, not inferred from the stage.
template<Stage S> class Graph {
public:
    static constexpr Stage stage = S;
    NodeId add(Kind kind, std::string value, std::vector<NodeId> operands = {}, Metadata metadata = {}) {
        if (next_ == std::numeric_limits<NodeId>::max()) throw std::runtime_error("NodeIR ID space exhausted");
        const auto id = next_;
        nodes_.emplace(id, Node{id, kind, std::move(value), std::move(operands), {}, std::move(metadata)});
        ++next_;
        return id;
    }
    NodeId operation(std::string opcode, std::vector<NodeId> operands = {}, Metadata metadata = {}) {
        return add(Kind::operation, std::move(opcode), std::move(operands), std::move(metadata));
    }
    NodeId literal(Kind kind, std::string value, Metadata metadata = {}) {
        if (kind == Kind::operation) throw std::invalid_argument("operation is not a literal kind");
        return add(kind, std::move(value), {}, std::move(metadata));
    }
    const Node& at(NodeId id) const { return nodes_.at(id); }
    Node& at(NodeId id) { return nodes_.at(id); }
    const std::map<NodeId, Node>& nodes() const { return nodes_; }
    const std::vector<NodeId>& roots() const { return roots_; }
    void roots(std::vector<NodeId> values) { roots_ = std::move(values); }
    bool operator==(const Graph&) const = default;

    // Structural verifier permits phi/data cycles and control cycles. Algorithms
    // requiring acyclic expressions must separately reject them.
    void verify(Limits limits = {}) const {
        if (nodes_.size() > limits.max_nodes) throw std::runtime_error("NodeIR node limit exceeded");
        std::size_t bytes = 0, edges = 0;
        auto count = [&](std::size_t size) {
            if (size > limits.max_bytes - std::min(bytes, limits.max_bytes)) throw std::runtime_error("NodeIR byte limit exceeded");
            bytes += size;
        };
        auto edge = [&](NodeId id) {
            if (!nodes_.contains(id)) throw std::runtime_error("NodeIR dangling reference: " + std::to_string(id));
            if (++edges > limits.max_bytes / sizeof(NodeId)) throw std::runtime_error("NodeIR edge limit exceeded");
        };
        for (const auto& [id, node] : nodes_) {
            if (id != node.id || !id) throw std::runtime_error("NodeIR invalid node identity");
            if (node.kind != Kind::operation && (!node.operands.empty() || !node.successors.empty()))
                throw std::runtime_error("NodeIR literal has edges");
            if ((node.kind == Kind::operation || node.kind == Kind::symbol || node.kind == Kind::keyword || node.kind == Kind::number) && node.value.empty())
                throw std::runtime_error("NodeIR empty opcode or literal");
            if (node.kind == Kind::boolean && node.value != "true" && node.value != "false") throw std::runtime_error("NodeIR invalid Boolean");
            if (node.kind == Kind::nil && node.value != "nil") throw std::runtime_error("NodeIR invalid nil");
            count(node.value.size()); count(node.metadata.type.size()); count(node.metadata.location.file.size());
            for (const auto& [key, value] : node.metadata.attributes) { count(key.size()); count(value.size()); }
            for (auto operand : node.operands) edge(operand);
            for (auto successor : node.successors) edge(successor);
        }
        for (auto root : roots_) edge(root);
    }
    // Deterministic traversal, including cyclic and shared subgraphs exactly once.
    std::vector<NodeId> reachable() const {
        std::vector<NodeId> result, work(roots_.rbegin(), roots_.rend());
        std::set<NodeId> seen;
        while (!work.empty()) {
            const auto id = work.back(); work.pop_back();
            if (!seen.insert(id).second) continue;
            const auto& node = at(id); result.push_back(id);
            work.insert(work.end(), node.successors.rbegin(), node.successors.rend());
            work.insert(work.end(), node.operands.rbegin(), node.operands.rend());
        }
        return result;
    }
    // Canonical reachable-graph encoding ignores allocation IDs and locations,
    // retains sharing, control edges, types, effects and semantic attributes.
    std::string fingerprint() const {
        const auto order = reachable();
        std::map<NodeId, std::size_t> indices;
        for (std::size_t i = 0; i < order.size(); ++i) indices[order[i]] = i;
        std::string output;
        auto text = [&](const std::string& value) { output += std::to_string(value.size()) + ":" + value; };
        auto edges = [&](const std::vector<NodeId>& values) {
            output += "[";
            for (auto id : values) output += std::to_string(indices.at(id)) + ",";
            output += "]";
        };
        edges(roots_);
        for (auto id : order) {
            const auto& node = at(id);
            output += std::to_string(static_cast<int>(node.kind)) + ":";
            text(node.value); text(node.metadata.type);
            output += std::to_string(static_cast<int>(node.metadata.effects)) + ":";
            output += std::to_string(node.metadata.attributes.size()) + ":";
            for (const auto& [key, value] : node.metadata.attributes) { text(key); text(value); }
            edges(node.operands); edges(node.successors);
        }
        return output;
    }
    // Replace every existing use, preserving bound IDs and shared definitions.
    // Control nodes require a dedicated host transformation, not an expression rewrite.
    void replace(NodeId old_id, NodeId new_id) {
        if (old_id == new_id) return;
        if (!at(old_id).successors.empty()) throw std::runtime_error("NodeIR expression rewrite has control successors");
        for (const auto& [id, node] : nodes_) {
            (void)id;
            if (std::find(node.successors.begin(), node.successors.end(), old_id) != node.successors.end())
                throw std::runtime_error("NodeIR expression rewrite targets a control successor");
        }
        std::vector<NodeId> work{new_id};
        std::set<NodeId> seen;
        while (!work.empty()) {
            auto id = work.back(); work.pop_back();
            if (id == old_id) throw std::runtime_error("NodeIR replacement depends on replaced node");
            if (!seen.insert(id).second) continue;
            const auto& node = at(id);
            work.insert(work.end(), node.operands.begin(), node.operands.end());
        }
        for (auto& [id, node] : nodes_) {
            (void)id;
            for (auto& operand : node.operands) if (operand == old_id) operand = new_id;
        }
        for (auto& root : roots_) if (root == old_id) root = new_id;
    }
private:
    std::map<NodeId, Node> nodes_;
    std::vector<NodeId> roots_;
    NodeId next_ = 1;
};

// Kernel pattern language: constructor heads are literal, operand symbols are
// variables; other atoms are exact literals. Repeated variables bind the SAME ID.
struct Pattern {
    bool variable = false;
    Kind kind = Kind::operation;
    std::string value;
    std::vector<Pattern> operands;
    static Pattern bind(std::string name) { return {true, Kind::symbol, std::move(name), {}}; }
    static Pattern literal(Kind kind, std::string value) { return {false, kind, std::move(value), {}}; }
    static Pattern operation(std::string opcode, std::vector<Pattern> operands) {
        return {false, Kind::operation, std::move(opcode), std::move(operands)};
    }
    void validate(std::set<std::string>& variables, Limits limits = {}) const {
        std::size_t nodes = 0, bytes = 0;
        std::function<void(const Pattern&, std::size_t)> visit = [&](const Pattern& p, std::size_t depth) {
            if (++nodes > limits.max_nodes || depth > limits.max_depth) throw std::runtime_error("NodeIR pattern limit exceeded");
            if (p.value.size() > limits.max_bytes - std::min(bytes, limits.max_bytes)) throw std::runtime_error("NodeIR pattern byte limit exceeded");
            bytes += p.value.size();
            if (p.variable) {
                if (p.value.empty() || !p.operands.empty()) throw std::runtime_error("NodeIR malformed variable");
                variables.insert(p.value);
            } else {
                if (p.kind != Kind::operation && !p.operands.empty()) throw std::runtime_error("NodeIR literal pattern has operands");
                if (p.kind == Kind::operation && p.value.empty()) throw std::runtime_error("NodeIR empty pattern constructor");
                for (const auto& operand : p.operands) visit(operand, depth + 1);
            }
        };
        visit(*this, 1);
    }
};
using Bindings = std::map<std::string, NodeId>;

template<class IR> bool match(const Pattern& pattern, const IR& ir, NodeId input,
                             Bindings& bindings, Limits limits = {}) {
    std::set<std::string> variables;
    pattern.validate(variables, limits);
    auto candidate = bindings;
    std::function<bool(const Pattern&, NodeId)> visit = [&](const Pattern& p, NodeId id) {
        const auto& node = ir.at(id);
        if (p.variable) {
            auto [it, inserted] = candidate.emplace(p.value, id);
            return inserted || it->second == id;
        }
        if (p.kind != node.kind || p.value != node.value || p.operands.size() != node.operands.size()) return false;
        for (std::size_t i = 0; i < p.operands.size(); ++i)
            if (!visit(p.operands[i], node.operands[i])) return false;
        return true;
    };
    if (!visit(pattern, input)) return false;
    bindings = std::move(candidate);
    return true;
}

// Only newly constructed nodes get the supplied provenance. Types/effects stay
// unknown until the host proves them; bindings retain their original metadata.
template<class IR> NodeId instantiate(const Pattern& pattern, IR& ir, const Bindings& bindings,
                                     Location origin = {}, Limits limits = {}) {
    std::set<std::string> variables;
    pattern.validate(variables, limits);
    for (const auto& variable : variables) (void)ir.at(bindings.at(variable));
    IR candidate = ir;
    std::function<NodeId(const Pattern&)> visit = [&](const Pattern& p) {
        if (p.variable) return bindings.at(p.value);
        std::vector<NodeId> operands;
        for (const auto& operand : p.operands) operands.push_back(visit(operand));
        Metadata metadata; metadata.location = origin;
        return candidate.add(p.kind, p.value, std::move(operands), std::move(metadata));
    };
    const auto result = visit(pattern);
    candidate.verify(limits);
    ir = std::move(candidate);
    return result;
}

// Bidirectional adapters supply semantic translation explicitly. Each callback
// returns a fresh output or throws a diagnostic; source objects are const.
template<class T> struct Translation {
    std::optional<T> value;
    std::vector<std::string> diagnostics;
    bool ok() const { return value.has_value() && diagnostics.empty(); }
};
template<class IR, class External, class Importer>
Translation<IR> translate_to(const External& source, Importer&& importer, Limits limits = {}) {
    try {
        IR ir = std::invoke(std::forward<Importer>(importer), source);
        ir.verify(limits);
        return {std::move(ir), {}};
    } catch (const std::exception& error) { return {std::nullopt, {error.what()}}; }
}
template<class External, class IR, class Exporter>
Translation<External> translate_from(const IR& ir, Exporter&& exporter, Limits limits = {}) {
    try {
        ir.verify(limits);
        return {std::invoke(std::forward<Exporter>(exporter), ir), {}};
    } catch (const std::exception& error) { return {std::nullopt, {error.what()}}; }
}

} // namespace mlvm::nodeir
