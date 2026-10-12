#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace mlvm::optilisp {

struct Location {
    std::string file;
    std::size_t line = 1, column = 1;
};
struct Diagnostic {
    std::string code, message;
    Location location;
    std::string render() const;
};
using Diagnostics = std::vector<Diagnostic>;

enum class ExprKind { list, symbol, keyword, string, number, boolean, nil };
// Value-owned AST: no sfsexp pointers escape the parser. Equality ignores locations.
struct Expr {
    ExprKind kind = ExprKind::symbol;
    std::string value;
    std::vector<Expr> items;
    Location location;
    bool operator==(const Expr& other) const;
    bool symbol(std::string_view spelling) const;
    std::string print() const;
};
struct ParseLimits {
    std::size_t max_bytes = 1024 * 1024;
    std::size_t max_depth = 128;
    std::size_t max_nodes = 100000;
};
struct ParseResult {
    std::vector<Expr> forms;
    Diagnostics diagnostics;
    bool ok() const { return diagnostics.empty(); }
};
// Optilisp lexical checks precede sfsexp AST parsing; errors discard all forms.
ParseResult parse(std::string_view source, std::string file = "<string>",
                  ParseLimits limits = {});
ParseResult parse_file(const std::string& path, ParseLimits limits = {});

} // namespace mlvm::optilisp
