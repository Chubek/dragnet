#include <cephyr/frontend.hpp>
#include <cctype>
#include <fstream>
#include <sstream>

namespace cephyr {
namespace {
std::string trim(std::string_view s) {
    auto first = s.find_first_not_of(" \t\r");
    return first == std::string_view::npos ? "" : std::string(s.substr(first, s.find_last_not_of(" \t\r") - first + 1));
}
bool word(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }
bool identifier(std::string_view s) {
    if (s.empty() || !(std::isalpha(static_cast<unsigned char>(s.front())) || s.front() == '_')) return false;
    for (char c : s) if (!word(c)) return false;
    return true;
}
class Preprocessor {
public:
    Preprocessor(mlvm::SourceManager& sources, const PreprocessOptions& options, mlvm::DiagnosticEngine& diagnostics)
        : sources(sources), options(options), diagnostics(diagnostics), macros(options.defines) {
        macros["__STDC__"] = "1"; macros["__STDC_VERSION__"] = "201710L"; macros["__STDC_HOSTED__"] = "0";
        for (const auto& name : options.undefines) macros.erase(name);
    }
    std::optional<std::string> run(mlvm::Identifier file) {
        std::string output;
        for (const auto& include : options.forced_includes) {
            auto part = load(include, 0); if (!part) return std::nullopt; output += *part;
        }
        auto part = process(file, 0); if (!part) return std::nullopt;
        return output + *part;
    }
private:
    mlvm::SourceManager& sources;
    const PreprocessOptions& options;
    mlvm::DiagnosticEngine& diagnostics;
    std::map<std::string, std::string> macros;
    std::set<std::filesystem::path> active_files;
    bool failed = false;
    void error(std::string message, mlvm::SourceLocation location = {}) { diagnostics.error("cephyr.preprocess", std::move(message), location); failed = true; }
    std::string expand(std::string_view line, std::set<std::string> disabled = {}, unsigned depth = 0) {
        if (depth > 128) { error("macro expansion depth exceeds 128"); return {}; }
        std::string out;
        for (std::size_t i = 0; i < line.size();) {
            if (line[i] == '"' || line[i] == '\'') {
                const auto quote = line[i]; out += line[i++];
                while (i < line.size()) {
                    auto ch = line[i++]; out += ch;
                    if (ch == '\\' && i < line.size()) out += line[i++]; else if (ch == quote) break;
                }
            } else if (std::isdigit(static_cast<unsigned char>(line[i])) ||
                       (line[i] == '.' && i + 1 < line.size() && std::isdigit(static_cast<unsigned char>(line[i + 1])))) {
                // A preprocessing number is indivisible: never expand identifiers inside 1e3 or 0xFF.
                const auto start = i++;
                while (i < line.size()) {
                    if (word(line[i]) || line[i] == '.') ++i;
                    else if ((line[i] == '+' || line[i] == '-') && i > start &&
                             (line[i - 1] == 'e' || line[i - 1] == 'E' || line[i - 1] == 'p' || line[i - 1] == 'P')) ++i;
                    else break;
                }
                out += line.substr(start, i - start);
            } else if (std::isalpha(static_cast<unsigned char>(line[i])) || line[i] == '_') {
                const auto start = i++; while (i < line.size() && word(line[i])) ++i;
                const std::string name(line.substr(start, i - start));
                const auto it = macros.find(name);
                if (it == macros.end() || disabled.contains(name)) out += name;
                else { auto nested = disabled; nested.insert(name); out += expand(it->second, std::move(nested), depth + 1); }
            } else out += line[i++];
            if (out.size() > 16 * 1024 * 1024) { error("macro expansion exceeds 16 MiB"); return {}; }
        }
        return out;
    }
    std::optional<std::string> load(const std::filesystem::path& path, unsigned depth) {
        std::ifstream input(path, std::ios::binary);
        if (!input) { error("cannot open include '" + path.string() + "'"); return std::nullopt; }
        std::ostringstream data; data << input.rdbuf();
        if (input.bad()) { error("cannot read include '" + path.string() + "'"); return std::nullopt; }
        return process(sources.add(path.string(), data.str()), depth);
    }
    std::optional<std::string> process(mlvm::Identifier file, unsigned depth) {
        const auto* source = sources.file(file);
        if (!source || depth > 64) { error("invalid source or include nesting exceeds 64"); return std::nullopt; }
        std::error_code ec;
        auto path = std::filesystem::weakly_canonical(source->name, ec);
        if (ec) path = std::filesystem::path(source->name).lexically_normal();
        if (!active_files.insert(path).second) { error("include cycle: " + source->name); return std::nullopt; }
        // Translation phase 2: splice escaped newlines before recognizing comments.
        std::string text;
        for (std::size_t i = 0; i < source->text.size(); ++i) {
            if (source->text[i] == '\\' && i + 1 < source->text.size() && source->text[i + 1] == '\n') { ++i; continue; }
            if (source->text[i] == '\\' && i + 2 < source->text.size() && source->text[i + 1] == '\r' && source->text[i + 2] == '\n') { i += 2; continue; }
            text += source->text[i];
        }
        // Replace comments with whitespace, preserving line breaks and quoted tokens.
        bool comment = false; char quote = 0;
        for (std::size_t i = 0; i < text.size(); ++i) {
            if (comment) {
                if (text[i] == '*' && i + 1 < text.size() && text[i + 1] == '/') { text[i] = text[i + 1] = ' '; ++i; comment = false; }
                else if (text[i] != '\n') text[i] = ' ';
            } else if (quote) {
                if (text[i] == '\\' && i + 1 < text.size()) ++i; else if (text[i] == quote) quote = 0;
            } else if (text[i] == '"' || text[i] == '\'') quote = text[i];
            else if (text[i] == '/' && i + 1 < text.size() && text[i + 1] == '*') { text[i] = text[i + 1] = ' '; ++i; comment = true; }
            else if (text[i] == '/' && i + 1 < text.size() && text[i + 1] == '/') {
                while (i < text.size() && text[i] != '\n') text[i++] = ' ';
                if (i == text.size()) break;
            }
        }
        if (comment) error("unterminated block comment");
        struct Conditional { bool parent, condition, seen_else; };
        std::vector<Conditional> stack;
        bool enabled = true;
        std::string output, line; std::istringstream input(text); std::uint32_t row = 0;
        while (std::getline(input, line)) {
            ++row; auto content = trim(line); mlvm::SourceLocation location{file, row, 1};
            if (content.starts_with('#')) {
                content = trim(std::string_view(content).substr(1)); const auto split = content.find_first_of(" \t");
                const auto directive = content.substr(0, split); auto argument = split == std::string::npos ? "" : trim(content.substr(split + 1));
                if (directive == "ifdef" || directive == "ifndef" || directive == "if") {
                    bool condition = false;
                    if (directive == "if") {
                        if (enabled) {
                            auto expression = trim(expand(argument));
                            if (expression != "0" && expression != "1") error("#if supports only 0 or 1 after macro expansion", location);
                            condition = expression == "1";
                        }
                    } else {
                        if (!identifier(argument)) error("expected macro name", location);
                        condition = macros.contains(argument); if (directive == "ifndef") condition = !condition;
                    }
                    stack.push_back({enabled, condition, false}); enabled = enabled && condition;
                } else if (directive == "else") {
                    if (stack.empty() || stack.back().seen_else || !argument.empty()) error("unmatched or malformed #else", location);
                    else { auto& c = stack.back(); c.seen_else = true; enabled = c.parent && !c.condition; }
                } else if (directive == "endif") {
                    if (stack.empty() || !argument.empty()) error("unmatched or malformed #endif", location);
                    else { enabled = stack.back().parent; stack.pop_back(); }
                } else if (directive == "elif" || directive == "elifdef" || directive == "elifndef") {
                    error("unsupported conditional directive #" + directive, location);
                } else if (enabled) {
                    if (directive == "define") {
                        std::size_t i = 0; while (i < argument.size() && word(argument[i])) ++i;
                        const auto name = argument.substr(0, i); const auto replacement = trim(argument.substr(i));
                        if (!identifier(name) || (i < argument.size() && argument[i] == '(') || replacement.find('#') != std::string::npos) error("only object-like macros without # or ## are supported", location);
                        else if (macros.contains(name) && macros[name] != replacement) error("incompatible macro redefinition: " + name, location);
                        else macros[name] = replacement;
                    } else if (directive == "undef") {
                        if (!identifier(argument)) error("expected macro name", location); else macros.erase(argument);
                    } else if (directive == "include") {
                        argument = trim(expand(argument));
                        if (argument.size() < 3 || !((argument.front() == '"' && argument.back() == '"') || (argument.front() == '<' && argument.back() == '>'))) error("expected quoted or angle-bracket include", location);
                        else {
                            const auto name = argument.substr(1, argument.size() - 2);
                            auto paths = options.include_paths;
                            if (argument.front() == '"') paths.insert(paths.begin(), std::filesystem::path(source->name).parent_path());
                            bool found = false;
                            for (const auto& directory : paths) if (std::filesystem::is_regular_file(directory / name, ec)) {
                                auto part = load(directory / name, depth + 1); if (part) output += *part; found = true; break;
                            }
                            if (!found) error("include not found: " + name, location);
                        }
                    } else if (directive == "error") error(argument, location);
                    else if (!directive.empty()) error("unsupported directive #" + directive, location);
                }
                output += '\n';
            } else output += (enabled ? expand(line) : "") + '\n';
            if (output.size() > 64 * 1024 * 1024) { error("preprocessed output exceeds 64 MiB", location); break; }
        }
        if (!stack.empty()) error("unterminated conditional directive", {file, row, 1});
        active_files.erase(path);
        if (failed) return std::nullopt;
        return output;
    }
};
}
std::optional<std::string> preprocess(mlvm::SourceManager& sources, mlvm::Identifier file,
                                     const PreprocessOptions& options, mlvm::DiagnosticEngine& diagnostics) {
    return Preprocessor(sources, options, diagnostics).run(file);
}
} // namespace cephyr
