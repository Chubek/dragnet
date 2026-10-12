#include <mlvm/core.hpp>
#include <algorithm>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace mlvm {
void DiagnosticEngine::report(Diagnostic diagnostic) { diagnostics_.push_back(std::move(diagnostic)); }
void DiagnosticEngine::error(std::string code, std::string message, SourceLocation location) {
    report({Severity::error, std::move(code), std::move(message), location});
}
bool DiagnosticEngine::has_error() const noexcept {
    return std::any_of(diagnostics_.begin(), diagnostics_.end(), [](const auto& d) {
        return d.severity == Severity::error || d.severity == Severity::fatal;
    });
}
Identifier SourceManager::add(std::string name, std::string text) {
    if (files_.size() >= std::numeric_limits<Identifier>::max()) throw std::length_error("too many source files");
    SourceFile source{std::move(name), std::move(text), {0}};
    for (std::size_t i = 0; i < source.text.size(); ++i)
        if (source.text[i] == '\n') source.line_offsets.push_back(i + 1);
    files_.push_back(std::move(source));
    return static_cast<Identifier>(files_.size());
}
const SourceFile* SourceManager::file(Identifier id) const noexcept {
    return id && id <= files_.size() ? &files_[id - 1] : nullptr;
}
SourceLocation SourceManager::location(Identifier id, std::size_t offset) const noexcept {
    const auto* source = file(id);
    if (!source || offset > source->text.size()) return {};
    const auto it = std::upper_bound(source->line_offsets.begin(), source->line_offsets.end(), offset);
    const auto line = static_cast<std::size_t>(it - source->line_offsets.begin());
    const auto column = offset - source->line_offsets[line - 1] + 1;
    if (line > UINT32_MAX || column > UINT32_MAX) return {};
    return {id, static_cast<std::uint32_t>(line), static_cast<std::uint32_t>(column)};
}
std::string SourceManager::format(const Diagnostic& diagnostic) const {
    static constexpr const char* names[] = {"note", "warning", "error", "fatal"};
    std::ostringstream out;
    auto print_location = [&](SourceLocation loc) {
        if (const auto* source = file(loc.file_id)) out << source->name << ':' << loc.line << ':' << loc.column << ": ";
    };
    print_location(diagnostic.location);
    out << names[static_cast<unsigned>(diagnostic.severity)] << '[' << diagnostic.code << "]: " << diagnostic.message;
    for (const auto& note : diagnostic.related) {
        out << '\n'; print_location(note.location); out << "note: " << note.message;
    }
    return out.str();
}
Identifier Context::intern(std::string_view spelling) {
    if (const auto it = identifiers_.find(spelling); it != identifiers_.end()) return it->second;
    if (strings_.size() >= std::numeric_limits<Identifier>::max()) throw std::length_error("too many identifiers");
    strings_.emplace_back(spelling);
    const auto id = static_cast<Identifier>(strings_.size());
    identifiers_.emplace(strings_.back(), id);
    return id;
}
std::string_view Context::spelling(Identifier id) const noexcept {
    return id && id <= strings_.size() ? std::string_view(strings_[id - 1]) : std::string_view{};
}
bool Context::register_dialect(std::string name, DiagnosticEngine& diagnostics) {
    if (name.empty() || name.find('.') != std::string::npos || dialects_.contains(name)) {
        diagnostics.error("dialect.registration", "invalid or duplicate dialect '" + name + "'"); return false;
    }
    intern(name); dialects_.emplace(std::move(name), true); return true;
}
bool Context::register_operation(OperationDefinition definition, DiagnosticEngine& diagnostics) {
    const auto dot = definition.name.find('.');
    if (dot == std::string::npos || dot + 1 == definition.name.size() || !has_dialect(definition.name.substr(0, dot)) ||
        operations_.contains(definition.name) || definition.min_operands > definition.max_operands ||
        definition.min_results > definition.max_results || definition.min_regions > definition.max_regions) {
        diagnostics.error("dialect.operation", "invalid or duplicate operation definition '" + definition.name + "'"); return false;
    }
    intern(definition.name);
    auto name = definition.name;
    operations_.emplace(std::move(name), std::move(definition)); return true;
}
bool Context::has_dialect(std::string_view name) const { return dialects_.contains(name); }
const OperationDefinition* Context::operation(std::string_view name) const {
    const auto it = operations_.find(name); return it == operations_.end() ? nullptr : &it->second;
}
std::vector<std::string> Context::dialects() const {
    std::vector<std::string> result;
    for (const auto& [name, unused] : dialects_) { (void)unused; result.push_back(name); }
    return result;
}
} // namespace mlvm
