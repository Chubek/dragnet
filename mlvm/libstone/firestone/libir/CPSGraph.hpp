#pragma once
#include "Graph.hpp"

namespace firestone::ir {

enum class TailKind { Halt, Jump, Branch };
struct CPSTail {
    TailKind kind = TailKind::Halt;
    std::optional<Operand> value; // Halt result or branch condition.
    std::vector<NodeId> targets;
    std::vector<std::vector<Operand>> arguments;
};
struct Continuation {
    std::string name;
    std::vector<ValueId> parameters;
    std::vector<Instruction> bindings;
    std::optional<CPSTail> tail;
};

class CPSGraph {
public:
    NodeId continuation(std::string name, std::vector<Type> parameters = {}) {
        if (name.empty()) throw std::invalid_argument("continuation name is empty");
        for (const auto& t : parameters) if (!t.valid_value()) throw std::invalid_argument("continuation parameter has no value type");
        Continuation c; c.name = std::move(name);
        for (auto& type : parameters) { c.parameters.push_back(types_.size()); types_.push_back(std::move(type)); }
        auto id = graph_.add_node(std::move(c));
        if (!entry_) entry_ = id;
        return id;
    }
    void set_entry(NodeId id) { graph_.node(id); entry_ = id; }
    NodeId entry() const { if (!entry_) throw std::logic_error("CPS graph has no entry"); return *entry_; }
    const Continuation& continuation(NodeId id) const { return graph_.node(id); }
    std::size_t value_count() const { return types_.size(); }
    ValueId bind(NodeId continuation, Opcode opcode, Type type, std::vector<Operand> operands, std::string annotation = {}) {
        auto& c = graph_.node(continuation);
        if (c.tail) throw std::logic_error("continuation already terminated");
        if (is_terminator(opcode) || opcode == Opcode::Phi || opcode == Opcode::Store || type.kind == TypeKind::Void)
            throw std::invalid_argument("CPS binding requires a value operation");
        auto id = types_.size(); types_.push_back(type);
        try { c.bindings.push_back({opcode, id, std::move(type), std::move(operands), {}, {}, std::move(annotation)}); }
        catch (...) { types_.pop_back(); throw; }
        return id;
    }
    void terminate(NodeId continuation, CPSTail tail) {
        graph_.node(continuation);
        if (graph_.node(continuation).tail) throw std::logic_error("continuation already terminated");
        for (auto t : tail.targets) graph_.node(t);
        auto expected = tail.kind == TailKind::Halt ? 0u : tail.kind == TailKind::Jump ? 1u : 2u;
        if (tail.targets.size() != expected || tail.arguments.size() != expected ||
            (tail.kind == TailKind::Branch && !tail.value) || (tail.kind == TailKind::Jump && tail.value))
            throw std::invalid_argument("invalid CPS tail shape");
        auto edges = tail.targets.size();
        graph_.node(continuation).tail = std::move(tail);
        try { for (std::size_t p = 0; p < edges; ++p) graph_.add_edge(continuation, graph_.node(continuation).tail->targets[p], EdgeKind::Control, p); }
        catch (...) { graph_.node(continuation).tail.reset(); throw; }
    }
    void effect(NodeId continuation, Opcode opcode, std::vector<Operand> operands, std::string annotation = {}) {
        auto& c = graph_.node(continuation);
        if (c.tail) throw std::logic_error("continuation already terminated");
        if (opcode != Opcode::Store && opcode != Opcode::Call) throw std::invalid_argument("CPS effect must be a store or void call");
        c.bindings.push_back({opcode, {}, Type::void_type(), std::move(operands), {}, {}, std::move(annotation)});
    }
    void halt(NodeId c, std::optional<Operand> value = {}) { terminate(c, {TailKind::Halt, std::move(value), {}, {}}); }
    void jump(NodeId c, NodeId target, std::vector<Operand> arguments) {
        terminate(c, {TailKind::Jump, {}, {target}, {std::move(arguments)}});
    }
    void branch(NodeId c, Operand predicate, NodeId yes, std::vector<Operand> yes_args, NodeId no, std::vector<Operand> no_args) {
        terminate(c, {TailKind::Branch, std::move(predicate), {yes, no}, {std::move(yes_args), std::move(no_args)}});
    }
    const DirectedGraph<Continuation>& graph() const { return graph_; }
    Type type(ValueId v) const { return types_.at(v); }
    Diagnostics verify() const {
        Diagnostics out;
        if (!entry_) diagnose(out, "entry", "CPS graph has no entry");
        auto resolve = [&](ValueId v) { return v < types_.size() ? types_[v] : Type{}; };
        for (auto id : graph_.nodes()) {
            const auto& c = graph_.node(id);
            std::set<ValueId> scope(c.parameters.begin(), c.parameters.end());
            for (auto p : c.parameters) if (!type(p).valid_value())
                diagnose(out, "type", "continuation parameter has no value type", id);
            auto check_operand = [&](const Operand& o) {
                if (o.kind == OperandKind::Value && !scope.count(o.value)) diagnose(out, "scope", "operand is outside continuation scope", id);
                auto t = operand_type(o, resolve);
                if (!t.valid_value()) diagnose(out, "type", "tail operand has no value type", id);
                Instruction copy{Opcode::Copy, ValueId{0}, t, {o}, {}, {}, {}};
                append_diagnostics(out, verify_instruction(copy, resolve), id, invalid_id);
            };
            for (std::size_t n = 0; n < c.bindings.size(); ++n) {
                const auto& i = c.bindings[n];
                for (const auto& o : i.operands) check_operand(o);
                append_diagnostics(out, verify_instruction(i, resolve), id, n);
                if (i.result) scope.insert(*i.result);
            }
            if (!c.tail) { diagnose(out, "tail", "continuation has no tail", id); continue; }
            if (c.tail->value) {
                check_operand(*c.tail->value);
                if (c.tail->kind == TailKind::Branch && operand_type(*c.tail->value, resolve) != Type::boolean())
                    diagnose(out, "type", "CPS branch condition must be boolean", id);
            }
            for (std::size_t p = 0; p < c.tail->targets.size(); ++p) {
                const auto& params = graph_.node(c.tail->targets[p]).parameters;
                const auto& args = c.tail->arguments[p];
                if (params.size() != args.size()) diagnose(out, "arity", "continuation argument count differs", id);
                for (std::size_t a = 0; a < args.size(); ++a) {
                    check_operand(args[a]);
                    if (a < params.size() && operand_type(args[a], resolve) != type(params[a])) diagnose(out, "type", "continuation argument type differs", id);
                }
            }
        }
        return out;
    }
private:
    DirectedGraph<Continuation> graph_;
    std::vector<Type> types_;
    std::optional<NodeId> entry_;
};

} // namespace firestone::ir
