#pragma once

#include "simple_isa.hpp"
#include "DomDSL.hpp"

#include <memory>

namespace sandslab {
namespace isa = qarch::simple_isa;
using Error = isa::ISAError;
using Operands = isa::Operands;

// An instruction retains the ISA form and named operands, not baked-in bytes.
struct Instruction {
    std::string operation;
    Operands operands;
};

template <dsl::FixedString Form> struct Instr : Instruction {
    explicit Instr(Operands operands = {})
        : Instruction{std::string(Form.view()), std::move(operands)} {}
};

enum class RelativeBase { instruction, next_instruction };
struct Label { std::string name; };
struct Fixup {
    std::string operand, label;
    RelativeBase base = RelativeBase::instruction;
    std::int64_t bias = 0;
    std::int64_t unit = 1;
};
struct Emission {
    Instruction instruction;
    std::vector<Fixup> fixups;
};
using Node = std::variant<Label, Emission>;

struct Code {
    std::vector<std::uint8_t> bytes;
    std::map<std::string, std::size_t> labels;
    // Numeric displacements are exactly those passed to the ISA encoder.
    std::string assembly;
};

class Program : public dsl::DSL<Program, dsl::Pipeline> {
    std::shared_ptr<const isa::ISA> target_;
    std::vector<Node> nodes_;

    static void check_label(const std::string &name) {
        if (name.empty() || !(std::isalpha(static_cast<unsigned char>(name[0])) || name[0] == '_'))
            throw Error("invalid label: " + name);
        for (unsigned char c : name)
            if (!std::isalnum(c) && c != '_') throw Error("invalid label: " + name);
    }

public:
    explicit Program(isa::ISA target)
        : target_(std::make_shared<const isa::ISA>(std::move(target))) {}
    const isa::ISA &target() const { return *target_; }
    const std::vector<Node> &nodes() const { return nodes_; }

    Program &label(std::string name) {
        check_label(name);
        for (const auto &node : nodes_)
            if (auto p = std::get_if<Label>(&node); p && p->name == name)
                throw Error("duplicate label: " + name);
        nodes_.push_back(Label{std::move(name)});
        return *this;
    }

    Program &emit(Instruction instruction, std::vector<Fixup> fixups = {}) {
        const auto &op = target().operation(instruction.operation);
        std::set<std::string> seen;
        for (const auto &fix : fixups) {
            check_label(fix.label);
            if (fix.unit <= 0) throw Error("fixup unit must be positive");
            if (!seen.insert(fix.operand).second) throw Error("duplicate fixup operand");
            auto operand = std::find_if(op.operands.begin(), op.operands.end(),
                [&](const auto &v) { return v.first == fix.operand; });
            if (operand == op.operands.end() || (operand->second != "rel" && operand->second != "imm"))
                throw Error("fixup requires an immediate or relative operand: " + fix.operand);
            if (instruction.operands.contains(fix.operand))
                throw Error("fixup operand already has a value: " + fix.operand);
            instruction.operands.emplace(fix.operand, 0);
        }
        target().render(instruction.operation, instruction.operands);
        nodes_.push_back(Emission{std::move(instruction), std::move(fixups)});
        return *this;
    }
    Program &emit(std::string operation, Operands operands = {}) {
        return emit(Instruction{std::move(operation), std::move(operands)});
    }

    // Also works for text-only targets. Symbols require binary layout, so use
    // compile().assembly when the program contains fixups.
    std::string assembly() const {
        std::string out;
        for (const auto &node : nodes_) {
            if (auto p = std::get_if<Label>(&node)) out += p->name + ":\n";
            else {
                const auto &e = std::get<Emission>(node);
                if (!e.fixups.empty()) throw Error("symbolic assembly requires compile()");
                out += target().render(e.instruction.operation, e.instruction.operands) + "\n";
            }
        }
        return out;
    }

    Code compile() const {
        Code out;
        std::vector<std::size_t> sizes;
        std::size_t offset = 0;
        for (const auto &node : nodes_) {
            if (auto p = std::get_if<Label>(&node)) {
                out.labels.emplace(p->name, offset);
                sizes.push_back(0);
            } else {
                const auto &e = std::get<Emission>(node);
                const auto &op = target().operation(e.instruction.operation);
                if (!e.fixups.empty() && target().encodings.at(op.encoding).width == isa::Width::stream)
                    throw Error("stream fixups require relaxation and are unsupported");
                auto bytes = target().encode(e.instruction.operation, e.instruction.operands);
                if (bytes.size() > std::size_t(INT64_MAX) - offset)
                    throw Error("program too large");
                sizes.push_back(bytes.size());
                offset += bytes.size();
            }
        }
        offset = 0;
        for (std::size_t i = 0; i < nodes_.size(); ++i) {
            if (auto p = std::get_if<Label>(&nodes_[i])) {
                out.assembly += p->name + ":\n";
                continue;
            }
            const auto &e = std::get<Emission>(nodes_[i]);
            auto args = e.instruction.operands;
            for (const auto &fix : e.fixups) {
                auto dest = out.labels.find(fix.label);
                if (dest == out.labels.end()) throw Error("undefined label: " + fix.label);
                auto base = offset + (fix.base == RelativeBase::next_instruction ? sizes[i] : 0);
                auto delta = static_cast<std::int64_t>(dest->second) - static_cast<std::int64_t>(base);
                if ((fix.bias > 0 && delta < INT64_MIN + fix.bias) ||
                    (fix.bias < 0 && delta > INT64_MAX + fix.bias))
                    throw Error("fixup displacement overflow");
                delta -= fix.bias;
                if (delta % fix.unit) throw Error("unaligned fixup: " + fix.label);
                args.insert_or_assign(fix.operand, delta / fix.unit);
            }
            auto bytes = target().encode(e.instruction.operation, args);
            if (bytes.size() != sizes[i]) throw Error("fixup changed instruction size; relaxation required");
            out.bytes.insert(out.bytes.end(), bytes.begin(), bytes.end());
            out.assembly += target().render(e.instruction.operation, args) + "\n";
            offset += sizes[i];
        }
        return out;
    }
};
} // namespace sandslab
