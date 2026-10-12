#include <mlvm/ir.hpp>
#include <algorithm>
#include <limits>

namespace mlvm {
namespace {
template<class R, class F> void visit(R& region, const F& callback) {
    for (auto& block : region.blocks) for (auto& op : block.operations) {
        callback(op);
        for (auto& nested : op.regions) visit(nested, callback);
    }
}
}
Block& Module::add_block(Region& region, std::span<const Identifier> arguments) {
    Block block; block.id = next_node++;
    for (auto type : arguments) {
        const auto id = next_value++;
        values.emplace(id, Value{id, type, block.id, true, {}}); block.arguments.push_back(id);
    }
    region.blocks.push_back(std::move(block)); return region.blocks.back();
}
Operation& Module::append(Block& block, std::string name, std::span<const ValueId> operands,
                          std::span<const Identifier> results, SourceLocation location) {
    Operation op; op.id = next_node++; op.name = std::move(name); op.location = location;
    op.operands.assign(operands.begin(), operands.end());
    for (auto type : results) {
        const auto id = next_value++;
        values.emplace(id, Value{id, type, op.id, false, {}}); op.results.push_back(id);
    }
    for (std::size_t i = 0; i < op.operands.size(); ++i)
        if (auto it = values.find(op.operands[i]); it != values.end()) it->second.uses.push_back({op.id, i});
    block.operations.push_back(std::move(op)); return block.operations.back();
}
const Value* Module::value(ValueId id) const { const auto it = values.find(id); return it == values.end() ? nullptr : &it->second; }
void walk(Module& module, const std::function<void(Operation&)>& callback) { visit(module.body, callback); }
void walk(const Module& module, const std::function<void(const Operation&)>& callback) { visit(module.body, callback); }
void Module::rebuild_uses() {
    for (auto& [id, value] : values) { (void)id; value.uses.clear(); }
    walk(*this, [&](const Operation& op) {
        for (std::size_t i = 0; i < op.operands.size(); ++i)
            if (auto it = values.find(op.operands[i]); it != values.end()) it->second.uses.push_back({op.id, i});
    });
}
bool Module::replace_uses(ValueId from, ValueId to, DiagnosticEngine& diagnostics) {
    const auto* a = value(from); const auto* b = value(to);
    if (!a || !b || a->type != b->type) {
        diagnostics.error("ir.replace", "replacement values must exist and have identical types"); return false;
    }
    walk(*this, [&](Operation& op) { for (auto& operand : op.operands) if (operand == from) operand = to; });
    rebuild_uses(); return true;
}
bool Module::erase(Block& block, NodeId id, DiagnosticEngine& diagnostics) {
    const auto it = std::find_if(block.operations.begin(), block.operations.end(), [&](const auto& op) { return op.id == id; });
    if (it == block.operations.end()) { diagnostics.error("ir.erase", "operation is not in this block"); return false; }
    std::set<NodeId> owners;
    std::set<ValueId> removed;
    auto collect = [&](const Operation& op) { owners.insert(op.id); removed.insert(op.results.begin(), op.results.end()); };
    collect(*it);
    for (const auto& region : it->regions) {
        std::function<void(const Region&)> collect_region = [&](const Region& r) {
            for (const auto& b : r.blocks) {
                removed.insert(b.arguments.begin(), b.arguments.end());
                for (const auto& op : b.operations) { collect(op); for (const auto& child : op.regions) collect_region(child); }
            }
        };
        collect_region(region);
    }
    bool external_use = false;
    walk(*this, [&](const Operation& op) {
        if (!owners.contains(op.id)) for (auto operand : op.operands) if (removed.contains(operand)) external_use = true;
    });
    if (external_use) { diagnostics.error("ir.live-result", "cannot erase an operation with externally used values", it->location); return false; }
    for (auto value : removed) values.erase(value);
    block.operations.erase(it); rebuild_uses(); return true;
}
bool verify(const Module& module, DiagnosticEngine& diagnostics) {
    bool valid = true;
    auto fail = [&](std::string code, std::string message, SourceLocation loc = SourceLocation{}) {
        diagnostics.error(std::move(code), std::move(message), loc); valid = false;
    };
    if (!module.context) { fail("ir.context", "module has no context"); return false; }
    std::set<NodeId> nodes;
    std::set<ValueId> definitions;
    std::map<ValueId, std::vector<Use>> uses;
    std::set<std::string> symbols;
    auto define = [&](ValueId id, NodeId owner, bool argument) {
        const auto* value = module.value(id);
        if (!value || !id || value->id != id || value->owner != owner || value->block_argument != argument ||
            module.context->spelling(value->type).empty() || !definitions.insert(id).second)
            fail("ir.value", "invalid, missing or duplicate value definition");
    };
    std::function<void(const Region&, std::set<ValueId>, unsigned, const Operation*)> check_region;
    check_region = [&](const Region& region, std::set<ValueId> outer, unsigned depth, const Operation* function) {
        if (depth > 256) { fail("ir.depth", "region nesting exceeds 256"); return; }
        for (const auto& block : region.blocks) {
            if (!block.id || !nodes.insert(block.id).second) fail("ir.node", "duplicate or invalid block ID");
            auto available = outer;
            for (auto argument : block.arguments) { define(argument, block.id, true); available.insert(argument); }
            for (std::size_t index = 0; index < block.operations.size(); ++index) {
                const auto& op = block.operations[index];
                if (!op.id || !nodes.insert(op.id).second) fail("ir.node", "duplicate or invalid operation ID", op.location);
                const auto* def = module.context->operation(op.name);
                if (!def) fail("ir.operation", "unregistered operation '" + op.name + "'", op.location);
                else {
                    if (op.operands.size() < def->min_operands || op.operands.size() > def->max_operands ||
                        op.results.size() < def->min_results || op.results.size() > def->max_results ||
                        op.regions.size() < def->min_regions || op.regions.size() > def->max_regions)
                        fail("ir.arity", "wrong operand, result or region count for '" + op.name + "'", op.location);
                    if (def->terminator && index + 1 != block.operations.size()) fail("ir.terminator", "terminator must be last", op.location);
                }
                for (std::size_t i = 0; i < op.operands.size(); ++i) {
                    if (!available.contains(op.operands[i])) fail("ir.dominance", "operand is undefined or does not dominate its use", op.location);
                    uses[op.operands[i]].push_back({op.id, i});
                }
                if (op.name == "func.return") {
                    if (!function) fail("func.return", "return must be inside a function", op.location);
                    else {
                        const auto it = function->attributes.find("result");
                        const auto result = it == function->attributes.end() ? "void" : it->second;
                        if (result == "void") {
                            if (!op.operands.empty()) fail("func.return", "void function cannot return a value", op.location);
                        } else if (op.operands.size() != 1 || !module.value(op.operands[0]) ||
                                   module.context->spelling(module.value(op.operands[0])->type) != result)
                            fail("func.return", "return type does not match function result", op.location);
                    }
                }
                // Functions are isolated; other regions may capture earlier values.
                for (const auto& child : op.regions)
                    check_region(child, op.name == "builtin.func" ? std::set<ValueId>{} : available,
                                 depth + 1, op.name == "builtin.func" ? &op : function);
                for (auto result : op.results) { define(result, op.id, false); available.insert(result); }
                if (def && def->verify && !def->verify(op, module, diagnostics)) valid = false;
                if (op.name == "builtin.func") {
                    const auto it = op.attributes.find("sym");
                    if (it == op.attributes.end() || it->second.empty() || !symbols.insert(it->second).second)
                        fail("ir.symbol", "function requires a unique nonempty 'sym' attribute", op.location);
                }
            }
            if (depth && (block.operations.empty() || !module.context->operation(block.operations.back().name) ||
                          !module.context->operation(block.operations.back().name)->terminator))
                fail("ir.terminator", "nested block must end in a terminator");
        }
    };
    check_region(module.body, {}, 0, nullptr);
    for (const auto& [id, value] : module.values) {
        if (!definitions.contains(id)) fail("ir.orphan", "value is not owned by any node");
        if (value.uses != uses[id]) fail("ir.use-def", "use-def chain is inconsistent");
        if (id >= module.next_value) fail("ir.id", "value allocator would reuse an existing ID");
    }
    if (!nodes.empty() && *nodes.rbegin() >= module.next_node) fail("ir.id", "node allocator would reuse an existing ID");
    return valid;
}
} // namespace mlvm
