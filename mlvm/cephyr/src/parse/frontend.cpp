#include <cephyr/frontend.hpp>
#include <tree_sitter/api.h>
#include <charconv>
#include <limits>
#include <memory>

extern "C" const TSLanguage* tree_sitter_c(void);
namespace cephyr {
namespace {
using Tree = std::unique_ptr<TSTree, decltype(&ts_tree_delete)>;
Tree syntax(mlvm::SourceManager& sources, mlvm::Identifier file, mlvm::DiagnosticEngine& diagnostics) {
    const auto* source = sources.file(file);
    if (!source || source->text.size() > UINT32_MAX) { diagnostics.error("cephyr.source", "invalid or oversized source"); return {nullptr, ts_tree_delete}; }
    std::unique_ptr<TSParser, decltype(&ts_parser_delete)> parser(ts_parser_new(), ts_parser_delete);
    if (!parser || !ts_parser_set_language(parser.get(), tree_sitter_c())) { diagnostics.error("cephyr.parser", "Tree-sitter C parser unavailable or incompatible"); return {nullptr, ts_tree_delete}; }
    Tree tree(ts_parser_parse_string(parser.get(), nullptr, source->text.data(), static_cast<std::uint32_t>(source->text.size())), ts_tree_delete);
    if (!tree) { diagnostics.error("cephyr.parser", "Tree-sitter parse failed"); return tree; }
    if (ts_node_has_error(ts_tree_root_node(tree.get()))) {
        std::vector<TSNode> pending{ts_tree_root_node(tree.get())};
        while (!pending.empty()) {
            const auto node = pending.back(); pending.pop_back();
            if (ts_node_is_error(node) || ts_node_is_missing(node)) {
                diagnostics.error("cephyr.syntax", std::string("unexpected or missing C syntax: ") + ts_node_type(node), sources.location(file, ts_node_start_byte(node))); break;
            }
            for (std::uint32_t i = ts_node_child_count(node); i > 0; --i) pending.push_back(ts_node_child(node, i - 1));
        }
        return {nullptr, ts_tree_delete};
    }
    return tree;
}
TSNode field(TSNode node, const char* name) {
    return ts_node_child_by_field_name(node, name, static_cast<std::uint32_t>(std::char_traits<char>::length(name)));
}
std::string_view kind(TSNode node) { return ts_node_type(node); }
std::string_view text(TSNode node, const std::string& source) {
    return std::string_view(source).substr(ts_node_start_byte(node), ts_node_end_byte(node) - ts_node_start_byte(node));
}
}
bool parse(mlvm::SourceManager& sources, mlvm::Identifier file, mlvm::DiagnosticEngine& diagnostics) { return static_cast<bool>(syntax(sources, file, diagnostics)); }
std::optional<mlvm::Module> lower(mlvm::Context& context, mlvm::SourceManager& sources, mlvm::Identifier file, mlvm::DiagnosticEngine& diagnostics) {
    auto tree = syntax(sources, file, diagnostics); if (!tree) return std::nullopt;
    const auto& source = sources.file(file)->text;
    mlvm::register_builtin_dialects(context, diagnostics);
    mlvm::Module module(context); auto& top = module.add_block(module.body);
    const auto i32 = context.intern("i32");
    bool ok = true;
    auto fail = [&](TSNode node, std::string message) { diagnostics.error("cephyr.unsupported", std::move(message), sources.location(file, ts_node_start_byte(node))); ok = false; };
    const auto root = ts_tree_root_node(tree.get());
    for (std::uint32_t i = 0; i < ts_node_named_child_count(root); ++i) {
        const auto function = ts_node_named_child(root, i);
        if (kind(function) == "comment") continue;
        if (kind(function) != "function_definition") { fail(function, "only function definitions are supported by semantic lowering"); continue; }
        const auto type = field(function, "type"), declarator = field(function, "declarator"), body = field(function, "body");
        if (text(type, source) != "int" || kind(declarator) != "function_declarator" || ts_node_named_child_count(function) != 3) { fail(function, "expected unqualified int function definition"); continue; }
        const auto name_node = field(declarator, "declarator"), parameters = field(declarator, "parameters");
        if (kind(name_node) != "identifier" || ts_node_named_child_count(declarator) != 2 ||
            ts_node_named_child_count(parameters) != 1 || kind(ts_node_named_child(parameters, 0)) != "parameter_declaration" ||
            text(ts_node_named_child(parameters, 0), source) != "void") { fail(declarator, "only functions with an explicit (void) parameter list are supported"); continue; }
        if (ts_node_named_child_count(body) != 1 || kind(ts_node_named_child(body, 0)) != "return_statement") { fail(body, "function body must contain one return statement"); continue; }
        const auto ret = ts_node_named_child(body, 0);
        if (ts_node_named_child_count(ret) != 1) { fail(ret, "int function requires a return expression"); continue; }
        std::function<std::optional<std::int64_t>(TSNode, unsigned)> evaluate;
        evaluate = [&](TSNode node, unsigned depth) -> std::optional<std::int64_t> {
            if (depth > 128) { fail(node, "constant expression nesting exceeds 128"); return std::nullopt; }
            if (kind(node) == "parenthesized_expression") return evaluate(ts_node_named_child(node, 0), depth + 1);
            if (kind(node) == "number_literal") {
                auto s = text(node, source); int base = 10;
                if (s.starts_with("0x") || s.starts_with("0X")) { s.remove_prefix(2); base = 16; }
                else if (s.size() > 1 && s[0] == '0') base = 8;
                std::int64_t value = 0; auto r = std::from_chars(s.data(), s.data() + s.size(), value, base);
                if (s.empty() || r.ec != std::errc{} || r.ptr != s.data() + s.size() || value > INT32_MAX) { fail(node, "only unsuffixed signed-int literals are supported"); return std::nullopt; }
                return value;
            }
            if (kind(node) == "unary_expression") {
                auto value = evaluate(field(node, "argument"), depth + 1); if (!value) return std::nullopt;
                auto op = text(field(node, "operator"), source);
                if (op == "+") return value;
                if (op == "-" && *value != INT32_MIN) return -*value;
                fail(node, "unsupported or overflowing unary constant expression"); return std::nullopt;
            }
            if (kind(node) == "binary_expression") {
                auto left = evaluate(field(node, "left"), depth + 1), right = evaluate(field(node, "right"), depth + 1);
                if (!left || !right) return std::nullopt;
                auto op = text(field(node, "operator"), source); std::int64_t value;
                if (op == "+") value = *left + *right;
                else if (op == "-") value = *left - *right;
                else if (op == "*") value = *left * *right;
                else if (op == "&") value = *left & *right;
                else if (op == "|") value = *left | *right;
                else if (op == "^") value = *left ^ *right;
                else if (op == "/" || op == "%") {
                    if (*right == 0) { fail(node, "division by zero in constant expression"); return std::nullopt; }
                    if (*left == INT32_MIN && *right == -1) { fail(node, "signed integer overflow in constant expression"); return std::nullopt; }
                    value = op == "/" ? *left / *right : *left % *right;
                } else if (op == "<<" || op == ">>") {
                    if (*right < 0 || *right > 31) { fail(node, "shift amount must be between 0 and 31 in constant expression"); return std::nullopt; }
                    if (op == ">>") value = *left >> *right;
                    else {
                        value = *left << *right;
                        // Detect bits shifted out of the signed 32-bit range.
                        if ((value >> *right) != *left) { fail(node, "signed integer overflow in constant expression"); return std::nullopt; }
                    }
                } else { fail(node, "supported constant binary operators are +, -, *, /, %, &, |, ^, <<, >>"); return std::nullopt; }
                if (value < INT32_MIN || value > INT32_MAX) { fail(node, "signed integer overflow in constant expression"); return std::nullopt; }
                return value;
            }
            fail(node, "expected a supported integer constant expression"); return std::nullopt;
        };
        auto result = evaluate(ts_node_named_child(ret, 0), 0); if (!result) continue;
        auto& fn = module.append(top, "builtin.func", {}, {}, sources.location(file, ts_node_start_byte(function)));
        fn.attributes["sym"] = text(name_node, source); fn.attributes["result"] = "i32"; fn.regions.emplace_back();
        auto& block = module.add_block(fn.regions[0]);
        auto& constant = module.append(block, "arith.constant", {}, std::span(&i32, 1), sources.location(file, ts_node_start_byte(ret)));
        constant.attributes["value"] = std::to_string(*result); const auto value = constant.results[0];
        module.append(block, "func.return", std::span(&value, 1), {}, sources.location(file, ts_node_start_byte(ret)));
    }
    if (!ok || !mlvm::verify(module, diagnostics)) return std::nullopt;
    return module;
}
} // namespace cephyr
