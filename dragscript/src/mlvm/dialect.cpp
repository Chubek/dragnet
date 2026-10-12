#include "lowering.hpp"
#include <charconv>
#include <optional>
#include <set>

namespace dragscript::detail {
namespace {
constexpr Kind all_kinds[] = {
    Kind::program, Kind::block, Kind::sequence, Kind::empty, Kind::expression, Kind::declaration,
    Kind::function, Kind::conditional, Kind::return_, Kind::literal, Kind::identifier, Kind::unary,
    Kind::binary, Kind::assign, Kind::call, Kind::property, Kind::object_literal, Kind::array_literal,
    Kind::member, Kind::assign_member, Kind::loop_while, Kind::loop_do, Kind::loop_for, Kind::loop_for_of,
    Kind::break_, Kind::continue_, Kind::update, Kind::compound};

// Returns a value, not a pointer into a loop variable, so callers can hold it.
std::optional<Kind> kind_of(std::string_view name) {
    for (Kind kind : all_kinds) if (name == kind_name(kind)) return kind;
    return std::nullopt;
}
bool unsigned_attribute(const mlvm::Operation& op, std::string_view name, std::size_t& value) {
    const auto it = op.attributes.find(name);
    if (it == op.attributes.end() || it->second.empty()) return false;
    const auto& text = it->second;
    const auto r = std::from_chars(text.data(), text.data() + text.size(), value);
    return r.ec == std::errc{} && r.ptr == text.data() + text.size();
}
bool flag(const mlvm::Operation& op, std::string_view name, bool expected) {
    const auto it = op.attributes.find(name);
    return it != op.attributes.end() && it->second == (expected ? "true" : "false");
}
bool has_flag(const mlvm::Operation& op, std::string_view name) {
    const auto it = op.attributes.find(name);
    return it != op.attributes.end() && (it->second == "true" || it->second == "false");
}
bool value_type(const mlvm::Module& module, mlvm::ValueId id, std::string_view type) {
    const auto* value = module.value(id);
    return value && module.context && module.context->spelling(value->type) == type;
}
bool spelling_is(const mlvm::Operation& op, std::string_view key, std::initializer_list<std::string_view> allowed) {
    const auto it = op.attributes.find(key);
    if (it == op.attributes.end()) return false;
    for (auto candidate : allowed) if (it->second == candidate) return true;
    return false;
}
} // namespace
bool register_dialect(mlvm::Context& context, mlvm::DiagnosticEngine& diagnostics) {
    if (!context.register_dialect("ds", diagnostics)) return false;
    for (Kind kind : all_kinds) {
        mlvm::OperationDefinition def;
        def.name = kind_name(kind);
        def.min_results = def.max_results = 1;
        // Regions are lazy semantic children in source order, never CFG edges.
        // min/max regions: expression operands are children; statements take bodies.
        switch (kind) {
        case Kind::program: case Kind::block: case Kind::sequence:
        case Kind::object_literal: case Kind::array_literal:
            def.max_regions = 50000; break;
        case Kind::call: def.min_regions = 1; def.max_regions = 50000; break;
        case Kind::expression: case Kind::unary: case Kind::assign: case Kind::update:
            def.min_regions = def.max_regions = 1; break;
        case Kind::property: case Kind::compound: case Kind::loop_while: case Kind::loop_do:
            def.min_regions = def.max_regions = 2; break;
        case Kind::member: case Kind::loop_for_of:
            def.min_regions = def.max_regions = 2; break;
        case Kind::assign_member: def.min_regions = def.max_regions = 3; break;
        case Kind::binary: def.min_regions = def.max_regions = 2; break;
        case Kind::conditional: def.min_regions = 2; def.max_regions = 3; break;
        case Kind::loop_for: def.min_regions = 1; def.max_regions = 4; break;
        case Kind::function: def.min_regions = def.max_regions = 1; break;
        case Kind::return_: case Kind::declaration: def.max_regions = 1; break;
        default: break; // break, continue, empty, identifier, literal take no regions.
        }
        def.effect = mlvm::Effect::unknown; // May allocate, fail, or contain observable children.
        def.verify = [kind](const mlvm::Operation& op, const mlvm::Module& module, mlvm::DiagnosticEngine& d) {
            auto fail = [&](std::string message) { d.error("ds.contract", std::move(message), op.location); return false; };
            std::size_t begin = 0, end = 0;
            if (!unsigned_attribute(op, "source_begin", begin) || !unsigned_attribute(op, "source_end", end) || begin > end)
                return fail("ds operation requires an ordered source byte range");
            if (op.results.size() != 1 ||
                !value_type(module, op.results[0], is_expression_kind(kind) ? "ds.value" : "ds.completion"))
                return fail("ds operation has an invalid result representation");

            // Child classification: which regions hold values, which hold completions.
            const bool expression_children = is_expression_kind(kind) || kind == Kind::expression ||
                                             kind == Kind::declaration || kind == Kind::return_ ||
                                             kind == Kind::property;
            auto value_child = [&](std::size_t index) {
                if (kind == Kind::conditional || kind == Kind::loop_for_of) return index == 0;
                if (kind == Kind::loop_while) return index == 0;
                if (kind == Kind::loop_do) return index == 1;
                if (kind == Kind::loop_for) return false; // Init, test, update and body are statements.
                return expression_children;
            };
            // The trailing child of a loop or conditional is its body, which must be a statement.
auto statement_child = [&](std::size_t index) {
                switch (kind) {
                case Kind::loop_while: case Kind::loop_for_of: return index == 1;
                case Kind::loop_do: return index == 0;
                case Kind::conditional: return index >= 1;
                case Kind::loop_for: {
                    std::size_t cursor = flag(op, "init", true) ? 1 : 0;
                    cursor += flag(op, "test", true) ? 1 : 0;
                    cursor += flag(op, "update", true) ? 1 : 0;
                    return index == cursor;
                }
                default: return false;
                }
            };
            for (std::size_t index = 0; index < op.regions.size(); ++index) {
                const auto& region = op.regions[index];
                if (region.blocks.size() != 1 || !region.blocks[0].arguments.empty())
                    return fail("ds child requires one argument-free block");
                const auto& ops = region.blocks[0].operations;
                if (ops.size() != 2 || ops.back().name != "ds.yield" || ops.back().operands.size() != 1 ||
                    ops.front().results.size() != 1 || ops.back().operands[0] != ops.front().results[0])
                    return fail("ds child must contain one semantic operation followed by its yield");
                if (!value_type(module, ops.back().operands[0], value_child(index) ? "ds.value" : "ds.completion"))
                    return fail("ds child yield has the wrong type");
                if (statement_child(index)) {
                    const auto child = kind_of(ops.front().name);
                    if (!child || is_expression_kind(*child))
                        return fail("ds " + op.name + " body child '" + ops.front().name + "' must be a statement operation");
                }
                if (kind == Kind::sequence && ops.front().name != "ds.declaration")
                    return fail("ds.sequence contains only declarations");
                if (kind == Kind::function && ops.front().name != "ds.block")
                    return fail("ds.function body must be a lexical block");
                if (kind == Kind::object_literal && ops.front().name != "ds.property")
                    return fail("ds.object contains only ds.property children");
                if ((kind == Kind::compound || kind == Kind::update) && index == 0 && ops.front().name != "ds.identifier")
                    return fail("ds assignment target must be an identifier");
                if (kind == Kind::compound && index == 1) {
                    const auto value = kind_of(ops.front().name);
                    if (!value || !is_expression_kind(*value)) return fail("ds.compound value must be an expression");
                }
            }
            if (kind == Kind::compound && op.regions.size() != 2) return fail("ds.compound requires a target and a value");
            if (kind == Kind::member && op.regions.size() != 2) return fail("ds.member requires a base and a key");
            if (kind == Kind::assign_member && op.regions.size() != 3)
                return fail("ds.assign_member requires a base, a key and a value");
            if (kind == Kind::property && op.regions.size() != 2) return fail("ds.property requires a key and a value");

            if (kind == Kind::identifier || kind == Kind::declaration || kind == Kind::function ||
                kind == Kind::assign || kind == Kind::update || kind == Kind::compound) {
                const auto it = op.attributes.find("text");
                if (it == op.attributes.end() || !identifier_name(it->second))
                    return fail("ds binding requires an identifier spelling");
            }
            if (kind == Kind::loop_for_of) {
                const auto it = op.attributes.find("text");
                if (it == op.attributes.end() || !identifier_name(it->second))
                    return fail("ds.for_of requires a variable spelling");
                if (!has_flag(op, "declared") || !has_flag(op, "immutable")) return fail("ds.for_of requires declared/immutable flags");
            }
            if (kind == Kind::unary || kind == Kind::binary) {
                const bool known = spelling_is(op, "text", kind == Kind::unary
                    ? std::initializer_list<std::string_view>{"!", "+", "-", "typeof", "void"}
                    : std::initializer_list<std::string_view>{"+", "-", "*", "/", "%", "===", "!==", "<", "<=", ">", ">=", "&&", "||", "??"});
                return known ? true : fail("Unsupported ds operator");
            }
            if (kind == Kind::compound)
                return spelling_is(op, "operator", std::initializer_list<std::string_view>{"+", "-", "*", "/", "%", "+=", "-=", "*=", "/=", "%="})
                    ? true : fail("Unsupported ds compound operator");
            if (kind == Kind::member || kind == Kind::assign_member)
                return spelling_is(op, "text", std::initializer_list<std::string_view>{".", "["})
                    ? true : fail("ds property access requires '.' or '['");
            if (kind == Kind::update) {
                if (!spelling_is(op, "increment", {"++", "--"})) return fail("ds.update requires ++ or --");
                if (!has_flag(op, "prefix")) return fail("ds.update requires a prefix flag");
            }
            if (kind == Kind::loop_for) {
                std::size_t expected = 1; // The body is always present.
                for (std::size_t part = 0; part < 3; ++part) {
                    const char* key = part == 0 ? "init" : part == 1 ? "test" : "update";
                    if (!has_flag(op, key)) return fail("ds.for requires init/test/update flags");
                    if (flag(op, key, true)) ++expected;
                }
                if (op.regions.size() != expected) return fail("ds.for regions do not match its flags");
                std::set<std::string> bindings;
                for (std::size_t index = 0;; ++index) {
                    const auto it = op.attributes.find("binding_" + std::to_string(index));
                    if (it == op.attributes.end()) break;
                    if (!identifier_name(it->second) || !bindings.insert(it->second).second)
                        return fail("Invalid or duplicate ds.for binding");
                }
            }
            if (kind == Kind::declaration) {
                if (!has_flag(op, "immutable")) return fail("ds.declaration requires an immutable flag");
                if (flag(op, "immutable", true) && op.regions.empty()) return fail("const requires an initializer");
            }
            if (kind == Kind::function) {
                std::size_t count = 0;
                if (!unsigned_attribute(op, "parameter_count", count) || count > 50000) return fail("Invalid parameter count");
                std::set<std::string> parameters;
                for (std::size_t p = 0; p < count; ++p) {
                    const auto it = op.attributes.find("parameter_" + std::to_string(p));
                    if (it == op.attributes.end() || !identifier_name(it->second) || !parameters.insert(it->second).second)
                        return fail("Invalid or duplicate parameter spelling");
                }
            }
            if (kind == Kind::literal) {
                const auto tag = op.attributes.find("tag"), payload = op.attributes.find("payload");
                if (tag == op.attributes.end() || payload == op.attributes.end())
                    return fail("Literal requires tag and payload");
                if (tag->second == "undefined" || tag->second == "null") {
                    if (!payload->second.empty()) return fail("Nullish literal payload must be empty");
                } else if (tag->second == "boolean") {
                    if (payload->second != "true" && payload->second != "false") return fail("Invalid boolean payload");
                } else if (tag->second == "number") {
                    double value = 0;
                    const auto& s = payload->second;
                    const auto result = std::from_chars(s.data(), s.data() + s.size(), value);
                    if (result.ec != std::errc{} || result.ptr != s.data() + s.size()) return fail("Invalid binary64 payload");
                } else if (tag->second == "string") {
                    if (payload->second.size() % 4 != 0) return fail("String payload is hexadecimal UTF-16 units");
                    for (char c : payload->second)
                        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return fail("Invalid string payload");
                } else return fail("Unknown value tag");
            }
            return true;
        };
        if (!context.register_operation(std::move(def), diagnostics)) return false;
    }
    mlvm::OperationDefinition yield;
    yield.name = "ds.yield"; yield.min_operands = yield.max_operands = 1;
    yield.terminator = true; yield.effect = mlvm::Effect::unknown;
    yield.verify = [](const mlvm::Operation& op, const mlvm::Module& module, mlvm::DiagnosticEngine& d) {
        if (op.operands.size() != 1 ||
            (!value_type(module, op.operands[0], "ds.value") && !value_type(module, op.operands[0], "ds.completion"))) {
            d.error("ds.yield", "yield requires a runtime value or completion", op.location); return false;
        }
        return true;
    };
    return context.register_operation(std::move(yield), diagnostics);
}
} // namespace dragscript::detail