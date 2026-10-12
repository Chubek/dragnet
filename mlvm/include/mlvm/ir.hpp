#pragma once
#include <mlvm/core.hpp>
#include <map>
#include <set>
#include <span>

namespace mlvm {
using NodeId = std::uint64_t;
using ValueId = std::uint64_t;
struct Use {
    NodeId operation = 0;
    std::size_t operand = 0;
    friend bool operator==(const Use&, const Use&) = default;
};
struct Value {
    ValueId id = 0;
    Identifier type = 0;
    NodeId owner = 0;
    bool block_argument = false;
    std::vector<Use> uses;
};
struct Block;
struct Region { std::vector<Block> blocks; };
struct Operation {
    NodeId id = 0;
    std::string name;
    std::vector<ValueId> operands, results;
    std::map<std::string, std::string, std::less<>> attributes;
    std::vector<Region> regions;
    SourceLocation location{};
};
struct Block {
    NodeId id = 0;
    std::vector<ValueId> arguments;
    std::vector<Operation> operations;
};
struct Module {
    // Context is borrowed and must outlive this module. Nodes are owned by value.
    explicit Module(Context& context) : context(&context) {}
    Context* context;
    Region body;
    // Optilisp declarations and ordered application requests. Parsing retains
    // them without executing; run_kernels applies them transactionally.
    std::vector<std::string> kernel_sources, kernel_applications;
    std::map<ValueId, Value> values;
    NodeId next_node = 1;
    ValueId next_value = 1;
    Block& add_block(Region& region, std::span<const Identifier> arguments = {});
    Operation& append(Block& block, std::string name, std::span<const ValueId> operands = {},
                      std::span<const Identifier> results = {}, SourceLocation location = {});
    [[nodiscard]] const Value* value(ValueId id) const;
    bool replace_uses(ValueId from, ValueId to, DiagnosticEngine& diagnostics);
    bool erase(Block& block, NodeId operation, DiagnosticEngine& diagnostics);
    void rebuild_uses();
};
void walk(Module& module, const std::function<void(Operation&)>& callback);
void walk(const Module& module, const std::function<void(const Operation&)>& callback);
bool verify(const Module& module, DiagnosticEngine& diagnostics);
// Versioned generic syntax; parsing returns no module on syntax or verification failure.
std::optional<Module> parse_ir(Context& context, SourceManager& sources, Identifier file,
                               DiagnosticEngine& diagnostics);
std::string print_ir(const Module& module);
} // namespace mlvm
