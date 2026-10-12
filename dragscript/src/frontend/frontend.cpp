#include "semantic/dsir.hpp"
#include <tree_sitter/api.h>
#include <tree-sitter-typescript.h>
#include <tree-sitter-tsx.h>
#include <algorithm>
#include <charconv>
#include <limits>
#include <memory>
#include <stdexcept>

namespace dragscript::detail {
namespace {
struct FrontendFailure {};
using Parser = std::unique_ptr<TSParser, decltype(&ts_parser_delete)>;
using Tree = std::unique_ptr<TSTree, decltype(&ts_tree_delete)>;
std::string_view type(TSNode node) { return ts_node_is_null(node) ? std::string_view{} : ts_node_type(node); }
TSNode field(TSNode node, const char* name) {
    return ts_node_child_by_field_name(node, name, static_cast<std::uint32_t>(std::char_traits<char>::length(name)));
}
SourceSpan span(TSNode node) {
    const auto a = ts_node_start_point(node), b = ts_node_end_point(node);
    return {{ts_node_start_byte(node), a.row + 1U, a.column + 1U},
            {ts_node_end_byte(node), b.row + 1U, b.column + 1U}};
}
// Reject malformed UTF-8 before parsing. Runtime strings are separately UTF-16.
bool decode_utf8(std::string_view text, std::u16string* output = nullptr) {
    for (std::size_t i = 0; i < text.size();) {
        auto c = static_cast<unsigned char>(text[i++]);
        std::uint32_t cp = c, minimum = 0;
        unsigned continuation = 0;
        if (c >= 0xc2 && c <= 0xdf) { cp = c & 31; continuation = 1; minimum = 0x80; }
        else if (c >= 0xe0 && c <= 0xef) { cp = c & 15; continuation = 2; minimum = 0x800; }
        else if (c >= 0xf0 && c <= 0xf4) { cp = c & 7; continuation = 3; minimum = 0x10000; }
        else if (c >= 0x80) return false;
        for (unsigned n = 0; n < continuation; ++n) {
            if (i == text.size()) return false;
            auto next = static_cast<unsigned char>(text[i++]);
            if ((next & 0xc0) != 0x80) return false;
            cp = (cp << 6) | (next & 63);
        }
        if (cp < minimum || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) return false;
        if (output) {
            if (cp <= 0xffff) output->push_back(static_cast<char16_t>(cp));
            else { cp -= 0x10000; output->push_back(static_cast<char16_t>(0xd800 + (cp >> 10)));
                   output->push_back(static_cast<char16_t>(0xdc00 + (cp & 1023))); }
        }
    }
    return true;
}
struct ParseInput {
    const Source& source;
    const EngineOptions& limits;
    CompileOptions options;
    std::size_t checks = 0;
    bool cancelled = false;
};
const char* read_input(void* payload, std::uint32_t offset, TSPoint, std::uint32_t* bytes) {
    const auto& input = *static_cast<ParseInput*>(payload);
    if (offset >= input.source.text.size()) { *bytes = 0; return ""; }
    *bytes = static_cast<std::uint32_t>(input.source.text.size() - offset);
    return input.source.text.data() + offset;
}
bool progress(TSParseState* state) {
    auto& input = *static_cast<ParseInput*>(state->payload);
    input.cancelled = input.options.cancellation && input.options.cancellation->load(std::memory_order_relaxed);
    if (input.cancelled) return true;
    if (input.checks == input.limits.max_parse_checks) return true;
    ++input.checks;
    return false;
}
class Builder {
public:
    Program program;
    const EngineOptions& limits;
    bool javascript;
    Builder(Source source, const EngineOptions& limits, bool javascript)
        : program{std::move(source), {}, 0}, limits(limits), javascript(javascript) {}
    [[noreturn]] void fail(TSNode node, std::string code, std::string message, std::string limit = {}) {
        error = diagnostic(program.source, std::move(code), std::move(message), span(node), Phase::compile, std::move(limit));
        throw FrontendFailure{};
    }
    [[noreturn]] void unsupported(TSNode node) {
        fail(node, "DS_UNSUPPORTED", "Unsupported syntax: " + std::string(type(node)));
    }
    [[noreturn]] void fail_at(SourceSpan where, std::string code, std::string message, std::string limit = {}) {
        error = diagnostic(program.source, std::move(code), std::move(message), where, Phase::compile, std::move(limit));
        throw FrontendFailure{};
    }
    // Node budget is enforced here so synthesized keys and properties count as DSIR nodes.
    NodeId add(Node node) {
        if (program.nodes.size() >= limits.max_nodes) fail_at(node.span, "DS_LIMIT", "DSIR node limit exceeded", "nodes");
        const auto id = program.nodes.size(); program.nodes.push_back(std::move(node)); return id;
    }
    // This grammar exposes no prefix flag for ++/--, so token order decides.
    bool operator_precedes(TSNode syntax, TSNode argument) {
        for (std::uint32_t i = 0; i < ts_node_child_count(syntax); ++i) {
            auto child = ts_node_child(syntax, i);
            if (type(child) == "++" || type(child) == "--")
                return ts_node_start_byte(child) < ts_node_start_byte(argument);
        }
        return false;
    }
    // An identifier node used where a ds.identifier child is required.
    NodeId add_key_binding(const std::string& name, SourceSpan where) {
        Node node; node.kind = Kind::identifier; node.span = where; node.text = name;
        return add(std::move(node));
    }
    // Wraps an expression so it runs as a statement and yields a completion value.
    NodeId as_statement(NodeId expression) {
        Node statement; statement.kind = Kind::expression;
        statement.span = program.nodes[expression].span;
        statement.children.push_back(expression);
        return add(std::move(statement));
    }
    NodeId add_key(const std::string& spelling, SourceSpan where) {
        Node key; key.kind = Kind::literal; key.span = where;
        key.literal = Value(std::u16string(spelling.begin(), spelling.end()));
        return add(std::move(key));
    }
    Diagnostic error;
    std::string text(TSNode node) const {
        const auto s = span(node);
        return program.source.text.substr(s.begin.byte, s.end.byte - s.begin.byte);
    }
    void erase(TSNode node) { if (!ts_node_is_null(node) && javascript) unsupported(node); }
    std::vector<TSNode> named(TSNode node) const {
        std::vector<TSNode> result;
        for (std::uint32_t i = 0; i < ts_node_named_child_count(node); ++i) {
            auto child = ts_node_named_child(node, i);
            if (type(child) != "comment") result.push_back(child);
        }
        return result;
    }
    std::string name(TSNode node) {
        if (ts_node_is_null(node) || type(node) != "identifier" || !identifier_name(text(node))) unsupported(node);
        return text(node);
    }
    Value string_literal(TSNode node) {
        const auto raw = text(node);
        std::u16string result;
        const auto body = std::string_view(raw).substr(1, raw.size() - 2);
        for (std::size_t i = 0; i < body.size();) {
            if (body[i] != '\\') {
                auto end = body.find('\\', i);
                if (end == std::string_view::npos) end = body.size();
                if (!decode_utf8(body.substr(i, end - i), &result)) fail(node, "DS_ENCODING", "Invalid UTF-8 string");
                i = end;
            } else {
                ++i;
                if (i == body.size()) fail(node, "DS_STRING", "Incomplete string escape");
                const auto escape = body[i++];
                switch (escape) {
                case 'n': result += u'\n'; break;
                case 'r': result += u'\r'; break;
                case 't': result += u'\t'; break;
                case 'b': result += u'\b'; break;
                case 'f': result += u'\f'; break;
                case 'v': result += u'\v'; break;
                case '\\': case '\'': case '"': result += static_cast<char16_t>(escape); break;
                case '\n': break;
                case '\r': if (i < body.size() && body[i] == '\n') ++i; break;
                case '0':
                    if (i < body.size() && body[i] >= '0' && body[i] <= '9') unsupported(node);
                    result += char16_t(0); break;
                case 'x': case 'u': {
                    const std::size_t digits = escape == 'x' ? 2 : 4;
                    if (i + digits > body.size()) fail(node, "DS_STRING", "Incomplete hexadecimal escape");
                    unsigned value = 0;
                    auto parsed = std::from_chars(body.data() + i, body.data() + i + digits, value, 16);
                    if (parsed.ec != std::errc{} || parsed.ptr != body.data() + i + digits) unsupported(node);
                    result += static_cast<char16_t>(value); i += digits; break;
                }
                default: unsupported(node); // No accidental legacy octal or code-point approximation.
                }
            }
            if (result.size() > limits.max_string_units) fail(node, "DS_LIMIT", "String literal exceeds limit", "string_units");
        }
        return Value(std::move(result));
    }
    NodeId convert(TSNode syntax, std::size_t depth = 0) {
        if (depth > limits.max_compile_depth) fail(syntax, "DS_LIMIT", "Semantic nesting exceeds limit", "compile_depth");
        Node n; n.span = span(syntax);
        const auto kind = type(syntax);
        const auto kids = named(syntax);
        auto child = [&](TSNode node) { return convert(node, depth + 1); };
        if (kind == "program" || kind == "statement_block") {
            n.kind = kind == "program" ? Kind::program : Kind::block;
            for (auto k : kids) n.children.push_back(child(k));
        } else if (kind == "empty_statement" || kind == "comment") n.kind = Kind::empty;
        else if (kind == "type_alias_declaration" || kind == "interface_declaration") {
            erase(syntax); n.kind = Kind::empty;
        } else if (kind == "expression_statement") {
            n.kind = Kind::expression; n.children.push_back(child(kids.at(0)));
        } else if (kind == "lexical_declaration") {
            n.kind = Kind::sequence;
            const bool immutable = text(field(syntax, "kind")) == "const";
            for (auto k : kids) {
                if (type(k) != "variable_declarator") unsupported(k);
                Node decl; decl.kind = Kind::declaration; decl.span = span(k); decl.immutable = immutable;
                decl.text = name(field(k, "name")); erase(field(k, "type"));
                auto init = field(k, "value");
                if (!ts_node_is_null(init)) decl.children.push_back(child(init));
                else if (immutable) fail(k, "DS_DECLARATION", "const requires an initializer");
                n.children.push_back(add(std::move(decl)));
            }
        } else if (kind == "function_declaration") {
            for (std::uint32_t i = 0; i < ts_node_child_count(syntax); ++i)
                if (type(ts_node_child(syntax, i)) == "async") unsupported(syntax);
            n.kind = Kind::function; n.text = name(field(syntax, "name"));
            erase(field(syntax, "type_parameters")); erase(field(syntax, "return_type"));
            for (auto parameter : named(field(syntax, "parameters"))) {
                if (type(parameter) != "required_parameter" && type(parameter) != "optional_parameter") unsupported(parameter);
                if (type(parameter) == "optional_parameter") erase(parameter);
                auto binding = field(parameter, "pattern");
                if (ts_node_is_null(binding)) binding = field(parameter, "name");
                n.parameters.push_back(name(binding)); erase(field(parameter, "type"));
                if (!ts_node_is_null(field(parameter, "value"))) unsupported(parameter);
                for (auto k : named(parameter))
                    if (!ts_node_eq(k, binding) && !ts_node_eq(k, field(parameter, "type"))) unsupported(k);
            }
            n.children.push_back(child(field(syntax, "body")));
        } else if (kind == "if_statement") {
            n.kind = Kind::conditional;
            n.children = {child(field(syntax, "condition")), child(field(syntax, "consequence"))};
            auto alternative = field(syntax, "alternative");
            if (!ts_node_is_null(alternative)) n.children.push_back(child(named(alternative).at(0)));
        } else if (kind == "while_statement" || kind == "do_statement") {
            n.kind = kind == "while_statement" ? Kind::loop_while : Kind::loop_do;
            const auto body = child(field(syntax, "body")), test = child(field(syntax, "condition"));
            n.children = kind == "while_statement" ? std::vector<NodeId>{test, body} : std::vector<NodeId>{body, test};
        } else if (kind == "for_statement") {
            n.kind = Kind::loop_for;
            // Optional parts are marked by attributes so both backends agree on the region layout.
            const auto initializer = field(syntax, "initializer"), condition = field(syntax, "condition");
            const auto increment = field(syntax, "increment");
            const bool has_initializer = !ts_node_is_null(initializer) && type(initializer) != "empty_statement";
            const bool has_test = !ts_node_is_null(condition) && type(condition) != "empty_statement";
            const bool has_update = !ts_node_is_null(increment);
            n.parts = {has_initializer, has_test, has_update};
            if (has_initializer) {
                if (type(initializer) == "lexical_declaration") {
                    const auto declaration = child(initializer);
                    for (auto id : program.nodes[declaration].children) n.bindings.push_back(program.nodes[id].text);
                    n.children.push_back(declaration);
                } else n.children.push_back(as_statement(child(initializer)));
            }
            // Test and update are statement children: their values are completion values.
            if (has_test) n.children.push_back(as_statement(child(condition)));
            if (has_update) n.children.push_back(as_statement(child(increment)));
            n.children.push_back(child(field(syntax, "body")));
        } else if (kind == "for_in_statement") {
            n.kind = Kind::loop_for_of;
            n.text = text(field(syntax, "operator"));
            if (n.text != "of") unsupported(syntax); // for-in enumeration is not supported.
            const auto declaration = field(syntax, "kind");
            n.declared = !ts_node_is_null(declaration);
            n.immutable = n.declared && text(declaration) == "const";
            n.text = name(field(syntax, "left"));
            n.children = {child(field(syntax, "right")), child(field(syntax, "body"))};
        } else if (kind == "break_statement" || kind == "continue_statement") {
            if (!ts_node_is_null(field(syntax, "label"))) unsupported(syntax); // Labelled break/continue.
            n.kind = kind == "break_statement" ? Kind::break_ : Kind::continue_;
        } else if (kind == "object" || kind == "array") {
            n.kind = kind == "object" ? Kind::object_literal : Kind::array_literal;
            for (auto k : kids) {
                if (kind == "array") {
                    if (type(k) == "spread_element") unsupported(k);
                    n.children.push_back(child(k));
                    continue;
                }
                if (type(k) != "pair") unsupported(k); // Methods, shorthand and spread.
                Node entry; entry.kind = Kind::property; entry.span = span(k);
                auto key = field(k, "key");
                if (type(key) == "property_identifier") entry.children.push_back(add_key(text(key), span(key)));
                else if (type(key) == "computed_property_name") entry.children.push_back(child(named(key).at(0)));
                else entry.children.push_back(child(key)); // string or number literal
                entry.children.push_back(child(field(k, "value")));
                n.children.push_back(add(std::move(entry)));
            }
        } else if (kind == "member_expression" || kind == "subscript_expression") {
            if (!ts_node_is_null(field(syntax, "optional_chain"))) unsupported(syntax);
            n.kind = Kind::member;
            n.text = kind == "member_expression" ? "." : "[";
            if (kind == "member_expression") {
                auto property = field(syntax, "property");
                if (type(property) != "property_identifier") unsupported(property); // Private names.
                const auto spelling = text(property);
                n.children = {child(field(syntax, "object")), add_key(spelling, span(property))};
            } else n.children = {child(field(syntax, "object")), child(field(syntax, "index"))};
        } else if (kind == "return_statement") {
            n.kind = Kind::return_;
            if (!kids.empty()) n.children.push_back(child(kids[0]));
        } else if (kind == "parenthesized_expression") { erase(field(syntax, "type")); return child(kids.at(0)); }
        else if (kind == "as_expression" || kind == "satisfies_expression" || kind == "non_null_expression") {
            erase(syntax); return child(kids.at(0));
        } else if (kind == "type_assertion") {
            erase(syntax); return child(kids.back());
        } else if (kind == "number") {
            n.kind = Kind::literal;
            const auto raw = text(syntax);
            if (raw.find_first_of("_nxXoObB") != std::string::npos ||
                (raw.size() > 1 && raw[0] == '0' && raw[1] >= '0' && raw[1] <= '9')) unsupported(syntax);
            double number = 0;
            auto parsed = std::from_chars(raw.data(), raw.data() + raw.size(), number);
            if (parsed.ec != std::errc{} || parsed.ptr != raw.data() + raw.size()) unsupported(syntax);
            n.literal = Value(number);
        } else if (kind == "string") { n.kind = Kind::literal; n.literal = string_literal(syntax); }
        else if (kind == "true" || kind == "false") { n.kind = Kind::literal; n.literal = Value(kind == "true"); }
        else if (kind == "null") { n.kind = Kind::literal; n.literal = Value::null(); }
        else if (kind == "identifier" || kind == "undefined") {
            n.kind = Kind::identifier; n.text = text(syntax);
            if (!identifier_name(n.text)) unsupported(syntax);
        } else if (kind == "unary_expression" || kind == "binary_expression") {
            n.kind = kind == "unary_expression" ? Kind::unary : Kind::binary;
            n.text = text(field(syntax, "operator"));
            const std::string operators = n.kind == Kind::unary ? " ! + - typeof void "
                                                                : " + - * / % === !== < <= > >= && || ?? ";
            if (operators.find(" " + n.text + " ") == std::string::npos) unsupported(syntax);
            if (n.kind == Kind::unary) n.children.push_back(child(field(syntax, "argument")));
            else n.children = {child(field(syntax, "left")), child(field(syntax, "right"))};
        } else if (kind == "assignment_expression") {
            auto left = field(syntax, "left");
            const bool member = type(left) == "member_expression" || type(left) == "subscript_expression";
            if (member) {
                const bool dot = type(left) == "member_expression";
                if (!ts_node_is_null(field(left, "optional_chain"))) unsupported(left);
                n.kind = Kind::assign_member; n.text = dot ? "." : "[";
                if (dot) {
                    auto property = field(left, "property");
                    if (type(property) != "property_identifier") unsupported(property);
                    n.children = {child(field(left, "object")), add_key(text(property), span(property))};
                } else n.children = {child(field(left, "object")), child(field(left, "index"))};
            } else { n.kind = Kind::assign; n.text = name(left); }
            n.children.push_back(child(field(syntax, "right")));
        } else if (kind == "augmented_assignment_expression") {
            n.kind = Kind::compound;
            n.op = text(field(syntax, "operator"));
            constexpr std::string_view supported[] = {"+=", "-=", "*=", "/=", "%="};
            if (std::find(std::begin(supported), std::end(supported), n.op) == std::end(supported)) unsupported(syntax);
            n.text = name(field(syntax, "left")); // Only identifier targets are supported.
            // Children are the identifier target and the right-hand value, in evaluation order.
            n.children.push_back(add_key_binding(n.text, span(field(syntax, "left"))));
            n.children.push_back(child(field(syntax, "right")));
        } else if (kind == "update_expression") {
            n.kind = Kind::update;
            n.op = text(field(syntax, "operator"));
            auto argument = field(syntax, "argument");
            // This grammar exposes no prefix flag; the operator token precedes the operand only in prefix form.
            n.prefix = operator_precedes(syntax, argument);
            n.text = name(argument);
            n.children.push_back(child(argument));
        } else if (kind == "call_expression") {
            if (!ts_node_is_null(field(syntax, "optional_chain"))) unsupported(syntax);
            // This grammar exposes optional call syntax as an unnamed token.
            for (std::uint32_t i = 0; i < ts_node_child_count(syntax); ++i)
                if (type(ts_node_child(syntax, i)) == "?." || type(ts_node_child(syntax, i)) == "optional_chain") unsupported(syntax);
            n.kind = Kind::call; erase(field(syntax, "type_arguments"));
            n.children.push_back(child(field(syntax, "function")));
            auto args = field(syntax, "arguments");
            if (type(args) != "arguments") unsupported(args);
            for (auto arg : named(args)) n.children.push_back(child(arg));
        } else unsupported(syntax);
        return add(std::move(n));
    }
};
} // namespace

std::optional<Program> frontend(Source source, const EngineOptions& limits, CompileOptions options,
                               std::vector<Diagnostic>& diagnostics) {
    auto fail = [&](std::string code, std::string message, std::string limit = {}) -> std::optional<Program> {
        auto d = diagnostic(source, std::move(code), std::move(message), {}, Phase::compile, std::move(limit));
        if (diagnostics.size() < limits.max_diagnostics) diagnostics.push_back(std::move(d));
        else diagnostics.back() = std::move(d);
        return std::nullopt;
    };
    if (source.text.size() > limits.max_source_bytes || source.text.size() > std::numeric_limits<std::uint32_t>::max() - 1U)
        return fail("DS_LIMIT", "Source byte limit exceeded", "source_bytes");
    if (!decode_utf8(source.text)) return fail("DS_ENCODING", "Source must be valid UTF-8");
    auto mode = source.kind;
    if (mode == SourceKind::infer) {
        auto dot = source.name.rfind('.');
        const auto ext = dot == std::string::npos ? "" : source.name.substr(dot);
        if (ext == ".ts") mode = SourceKind::typescript;
        else if (ext == ".tsx") mode = SourceKind::tsx;
        else if (ext == ".js") mode = SourceKind::javascript;
        else if (ext == ".jsx") mode = SourceKind::jsx;
        else return fail("DS_MODE", "Unknown source extension; specify SourceKind explicitly");
    }
    if (mode != SourceKind::javascript && mode != SourceKind::typescript && mode != SourceKind::tsx && mode != SourceKind::jsx)
        return fail("DS_MODE", "Invalid source mode");
    if (mode == SourceKind::jsx && !limits.enable_jsx) return fail("DS_MODE", "JSX mode requires enable_jsx");
    if (options.cancellation && options.cancellation->load(std::memory_order_relaxed)) return fail("DS_CANCELLED", "Compilation cancelled");
    if (!limits.max_parse_checks) return fail("DS_LIMIT", "Parser work limit exceeded", "parse_checks");
    Parser parser(ts_parser_new(), ts_parser_delete);
    if (!parser) return fail("DS_ALLOCATION", "Cannot allocate parser");
    const auto* language = mode == SourceKind::tsx || mode == SourceKind::jsx ? tree_sitter_tsx() : tree_sitter_typescript();
    if (!ts_parser_set_language(parser.get(), language)) return fail("DS_GRAMMAR", "Incompatible Tree-sitter grammar ABI");
    ParseInput input{source, limits, options};
    Tree tree(ts_parser_parse_with_options(parser.get(), nullptr,
        TSInput{&input, read_input, TSInputEncodingUTF8, nullptr}, TSParseOptions{&input, progress}), ts_tree_delete);
    if (!tree) return input.cancelled ? fail("DS_CANCELLED", "Compilation cancelled")
                                      : fail("DS_LIMIT", "Parser work limit exceeded", "parse_checks");
    // Cursor traversal bounds every node, including erased types and error recovery.
    auto cursor = ts_tree_cursor_new(ts_tree_root_node(tree.get()));
    struct CursorCleanup { TSTreeCursor* cursor; ~CursorCleanup() { ts_tree_cursor_delete(cursor); } } cleanup{&cursor};
    std::size_t count = 0, depth = 0;
    while (true) {
        const auto node = ts_tree_cursor_current_node(&cursor);
        if (options.cancellation && options.cancellation->load(std::memory_order_relaxed)) return fail("DS_CANCELLED", "Compilation cancelled");
        if (++count > limits.max_nodes || depth > limits.max_compile_depth)
            return fail("DS_LIMIT", "Syntax tree resource limit exceeded", count > limits.max_nodes ? "nodes" : "compile_depth");
        if (ts_node_is_error(node) || ts_node_is_missing(node)) {
            if (diagnostics.size() < limits.max_diagnostics)
                diagnostics.push_back(diagnostic(source, "DS_SYNTAX", ts_node_is_missing(node)
                    ? "Missing " + std::string(type(node)) : "Unexpected syntax", span(node)));
        }
        if (ts_tree_cursor_goto_first_child(&cursor)) { ++depth; continue; }
        while (!ts_tree_cursor_goto_next_sibling(&cursor)) {
            if (!ts_tree_cursor_goto_parent(&cursor)) goto walked;
            --depth;
        }
    }
walked:
    if (!diagnostics.empty()) return std::nullopt;
    if (ts_node_has_error(ts_tree_root_node(tree.get()))) return fail("DS_SYNTAX", "Syntax tree contains errors");
    Builder builder(std::move(source), limits, mode == SourceKind::javascript || mode == SourceKind::jsx);
    try {
        builder.program.root = builder.convert(ts_tree_root_node(tree.get()));
        return std::move(builder.program);
    } catch (const FrontendFailure&) {
        diagnostics.push_back(std::move(builder.error)); return std::nullopt;
    }
}
} // namespace dragscript::detail
