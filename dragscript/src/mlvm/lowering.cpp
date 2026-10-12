#include "lowering.hpp"
#include <array>
#include <functional>

namespace dragscript::detail {
namespace {
std::string payload(const Value& value) {
    if (value.tag() == Value::Tag::undefined || value.tag() == Value::Tag::null) return {};
    if (value.tag() != Value::Tag::string) return value.display();
    constexpr char hex[] = "0123456789abcdef";
    std::string result;
    for (auto unit : value.string())
        for (int shift = 12; shift >= 0; shift -= 4) result += hex[(unit >> shift) & 15];
    return result;
}
std::string flag_text(bool value) { return value ? "true" : "false"; }
} // namespace
std::optional<LoweredModule> lower(const Program& program, mlvm::DiagnosticEngine& diagnostics) {
    auto lowered = std::make_shared<LoweredModule>();
    if (!mlvm::register_builtin_dialects(*lowered->context, diagnostics) ||
        !register_dialect(*lowered->context, diagnostics)) return std::nullopt;
    auto& module = lowered->module;
    lowered->file = lowered->sources->add(program.source.name, program.source.text);
    std::function<mlvm::ValueId(NodeId, mlvm::Block&)> emit;
    emit = [&](NodeId id, mlvm::Block& block) {
        const auto& node = program.nodes[id];
        const std::array result_type{lowered->context->intern(is_expression_kind(node.kind) ? "ds.value" : "ds.completion")};
        const auto location = mlvm::SourceLocation{lowered->file, static_cast<std::uint32_t>(node.span.begin.line),
                                             static_cast<std::uint32_t>(node.span.begin.column)};
        auto& op = module.append(block, kind_name(node.kind), {}, result_type, location);
        const auto result = op.results[0];
        op.attributes["source_begin"] = std::to_string(node.span.begin.byte);
        op.attributes["source_end"] = std::to_string(node.span.end.byte);
        if (!node.text.empty()) op.attributes["text"] = node.text;
        if (node.kind == Kind::declaration || node.kind == Kind::loop_for_of)
            op.attributes["immutable"] = flag_text(node.immutable);
        if (node.kind == Kind::loop_for_of) op.attributes["declared"] = flag_text(node.declared);
        // ds.update/ds.compound bind an identifier in "text" and an operator elsewhere.
        if (node.kind == Kind::update) {
            op.attributes["increment"] = node.op;
            op.attributes["prefix"] = flag_text(node.prefix);
        }
        if (node.kind == Kind::compound) op.attributes["operator"] = node.op;
        if (node.kind == Kind::loop_for)
            for (std::size_t part = 0; part < 3; ++part) op.attributes[part == 0 ? "init" : part == 1 ? "test" : "update"] = flag_text(node.parts[part]);
        if (node.kind == Kind::function) {
            op.attributes["parameter_count"] = std::to_string(node.parameters.size());
            for (std::size_t i = 0; i < node.parameters.size(); ++i)
                op.attributes["parameter_" + std::to_string(i)] = node.parameters[i];
        }
        if (node.kind == Kind::loop_for)
            for (std::size_t i = 0; i < node.bindings.size(); ++i)
                op.attributes["binding_" + std::to_string(i)] = node.bindings[i];
        if (node.kind == Kind::literal) {
            constexpr const char* tags[] = {"undefined", "null", "boolean", "number", "string"};
            op.attributes["tag"] = tags[static_cast<unsigned>(node.literal.tag())];
            op.attributes["payload"] = payload(node.literal);
        }
        // Resize once before taking region references; emit only appends in child blocks.
        op.regions.resize(node.children.size());
        for (std::size_t i = 0; i < node.children.size(); ++i) {
            auto& child = module.add_block(op.regions[i]);
            const std::array operand{emit(node.children[i], child)};
            module.append(child, "ds.yield", operand, {}, location);
        }
        return result;
    };
    auto& block = module.add_block(module.body);
    const std::array operand{emit(program.root, block)};
    module.append(block, "ds.yield", operand);
    if (!mlvm::verify(module, diagnostics)) return std::nullopt;
    lowered->text = mlvm::print_ir(module);
    return std::move(*lowered);
}
} // namespace dragscript::detail