#pragma once
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mlvm {
inline constexpr std::string_view version = "0.1.0";
using Identifier = std::uint32_t;
enum class Severity : std::uint8_t { note, warning, error, fatal };
struct SourceLocation {
    std::uint32_t file_id = 0;
    std::uint32_t line = 0;
    std::uint32_t column = 0;
    friend bool operator==(const SourceLocation&, const SourceLocation&) = default;
};
struct RelatedLocation { SourceLocation location; std::string message; };
struct FixIt { SourceLocation begin, end; std::string replacement; };
struct Diagnostic {
    Severity severity = Severity::error;
    std::string code, message;
    SourceLocation location{};
    std::vector<RelatedLocation> related{};
    std::vector<FixIt> fixes{};
};
class DiagnosticEngine {
public:
    void report(Diagnostic diagnostic);
    void error(std::string code, std::string message, SourceLocation location = {});
    [[nodiscard]] const std::vector<Diagnostic>& diagnostics() const noexcept { return diagnostics_; }
    [[nodiscard]] bool has_error() const noexcept;
    void clear() noexcept { diagnostics_.clear(); }
private:
    std::vector<Diagnostic> diagnostics_;
};
struct SourceFile { std::string name, text; std::vector<std::size_t> line_offsets; };
class SourceManager {
public:
    Identifier add(std::string name, std::string text);
    [[nodiscard]] const SourceFile* file(Identifier id) const noexcept;
    [[nodiscard]] SourceLocation location(Identifier id, std::size_t offset) const noexcept;
    [[nodiscard]] std::string format(const Diagnostic& diagnostic) const;
private:
    std::deque<SourceFile> files_;
};
enum class Effect : std::uint8_t { pure, read, write, unknown };
struct Operation;
struct Module;
struct OperationDefinition {
    std::string name;
    std::size_t min_operands = 0, max_operands = 0;
    std::size_t min_results = 0, max_results = 0;
    std::size_t min_regions = 0, max_regions = 0;
    Effect effect = Effect::unknown;
    bool terminator = false;
    std::function<bool(const Operation&, const Module&, DiagnosticEngine&)> verify{};
};
class Context {
public:
    // Interned strings and definition references remain valid until context destruction.
    Identifier intern(std::string_view spelling);
    [[nodiscard]] std::string_view spelling(Identifier id) const noexcept;
    bool register_dialect(std::string name, DiagnosticEngine& diagnostics);
    bool register_operation(OperationDefinition definition, DiagnosticEngine& diagnostics);
    [[nodiscard]] bool has_dialect(std::string_view name) const;
    [[nodiscard]] const OperationDefinition* operation(std::string_view name) const;
    [[nodiscard]] std::vector<std::string> dialects() const;
private:
    std::deque<std::string> strings_;
    std::map<std::string, Identifier, std::less<>> identifiers_;
    std::map<std::string, bool, std::less<>> dialects_;
    std::map<std::string, OperationDefinition, std::less<>> operations_;
};
bool register_builtin_dialects(Context& context, DiagnosticEngine& diagnostics);
} // namespace mlvm
