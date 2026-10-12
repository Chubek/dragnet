#include "cli.hpp"
#include <ketopt.h>
#include <algorithm>
#include <charconv>
#include <cctype>
#include <fstream>
#include <iostream>
#include <limits>

namespace mlvm::tools {
namespace {
inline constexpr int color_option = 65536;
void expand(std::string argument, Arguments& args, std::set<std::string>& active, unsigned depth) {
    if (argument.empty() || argument[0] != '@') {
        if (args.values.size() >= 65536) throw Error(2, "too many command-line arguments");
        args.values.push_back(std::move(argument)); return;
    }
    if (depth >= 16) throw Error(2, "response-file nesting exceeds 16");
    auto path = argument.substr(1); auto canonical = std::filesystem::weakly_canonical(path).string();
    if (!active.insert(canonical).second) throw Error(2, "response-file cycle: " + path);
    args.protected_inputs.push_back(path);
    auto bytes = read_file(path);
    std::string token; char quote = 0; bool escape = false, started = false;
    auto flush = [&] { if (started) expand(token, args, active, depth + 1); token.clear(); started = false; };
    for (char c : bytes) {
        if (!c) throw Error(2, "NUL in response file: " + path);
        if (escape) { token += c; escape = false; started = true; }
        else if (c == '\\') { escape = true; started = true; }
        else if (quote) { if (c == quote) quote = 0; else token += c; }
        else if (c == '\'' || c == '"') { quote = c; started = true; }
        else if (std::isspace(static_cast<unsigned char>(c))) flush();
        else { token += c; started = true; }
    }
    if (quote || escape) throw Error(2, "unterminated quote or escape in response file: " + path);
    flush(); active.erase(canonical);
}
}
Options parse_options(const Arguments& args, std::string_view shorts, std::initializer_list<OptionSpec> specs) {
    std::vector<OptionSpec> definitions(specs);
    definitions.push_back({"help", 'h'}); definitions.push_back({"version", 'V'});
    definitions.push_back({"color", color_option, true});
    std::vector<ko_longopt_t> long_options;
    for (auto& def : definitions) long_options.push_back({def.name.data(), def.argument ? ko_required_argument : ko_no_argument, def.id});
    long_options.push_back({nullptr, 0, 0});
    std::vector<std::string> values{"mlvm-tool"}; values.insert(values.end(), args.values.begin(), args.values.end());
    std::vector<char*> argv; for (auto& value : values) argv.push_back(value.data());
    std::string short_options(shorts); short_options += "hV";
    // ketopt permits abbreviated long names and flag=value. Validate exact
    // spellings at the point of consumption so values and '--' stay positional.
    ketopt_t state = KETOPT_INIT; Options result;
    while (true) {
        if (!state.pos) {
            auto i = state.i;
            while (i < static_cast<int>(argv.size()) && (argv[i][0] != '-' || argv[i][1] == '\0')) ++i;
            if (i < static_cast<int>(argv.size())) {
                const std::string_view arg(argv[i]);
                if (arg.starts_with("--") && arg != "--") {
                    const auto eq = arg.find('='); const auto key = arg.substr(2, eq == std::string_view::npos ? eq : eq - 2);
                    auto found = std::find_if(definitions.begin(), definitions.end(), [&](const auto& d) { return d.name == key; });
                    if (found == definitions.end()) throw Error(2, "unsupported option: " + std::string(arg));
                    if (!found->argument && eq != std::string_view::npos) throw Error(2, "option takes no argument: --" + std::string(key));
                }
            }
        }
        const auto option = ketopt(&state, static_cast<int>(argv.size()), argv.data(), 1, short_options.c_str(), long_options.data());
        if (option == -1) break;
        if (option == '?' || option == ':') throw Error(2, option == ':' ? "missing option argument" : "unsupported short option: " + std::string(1, static_cast<char>(state.opt)));
        if (option == 'h') result.help = true;
        else if (option == 'V') result.version = true;
        else if (option == color_option) { if (std::string_view(state.arg) != "never") throw Error(2, "only --color=never is supported"); }
        else result.options.push_back({option, state.arg ? state.arg : ""});
    }
    for (int i = state.ind; i < static_cast<int>(argv.size()); ++i) result.inputs.emplace_back(argv[i]);
    return result;
}
bool show_info(std::string_view name, const Options& options, std::string_view usage) {
    if (options.help) {
        std::cout << name << " (MLVM " << mlvm::version << ")\nUsage: " << name << ' ' << usage
            << "\nCommon: --help (-h), --version (-V), --color=never, @RESPONSE, --\n"; return true;
    }
    if (options.version) { std::cout << name << " (MLVM) " << mlvm::version << '\n'; return true; }
    return false;
}
void require_inputs(const Options& options, bool exactly_one) {
    if (options.inputs.empty() || (exactly_one && options.inputs.size() != 1))
        throw Error(2, exactly_one ? "expected exactly one input file" : "expected input file");
}
std::uint64_t unsigned_number(std::string_view value, std::string_view description, bool allow_hex) {
    int base = 10;
    if (allow_hex && value.starts_with("0x")) { value.remove_prefix(2); base = 16; }
    std::uint64_t result = 0; const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result, base);
    if (value.empty() || parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size()) throw Error(2, "invalid " + std::string(description));
    return result;
}
std::vector<std::uint8_t> read_file(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw Error(3, "cannot open '" + path + "'");
    input.seekg(0, std::ios::end); const auto size = input.tellg();
    if (size < 0 || size > 256 * 1024 * 1024) throw Error(3, "input is unreadable or exceeds 256 MiB: " + path);
    input.seekg(0); std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    if (!bytes.empty() && !input.read(reinterpret_cast<char*>(bytes.data()), size)) throw Error(3, "cannot read '" + path + "'");
    return bytes;
}
bool same_file(const std::filesystem::path& a, const std::filesystem::path& b) {
    std::error_code error;
    if (std::filesystem::equivalent(a, b, error) && !error) return true;
    return std::filesystem::weakly_canonical(a) == std::filesystem::weakly_canonical(b);
}
void check_output(const std::string& path, std::span<const std::string> inputs) {
    if (path.empty()) throw Error(2, "output requires -o FILE");
    for (const auto& input : inputs) if (same_file(path, input)) throw Error(2, "output would overwrite input '" + input + "'");
}
void write_file(const std::string& path, std::span<const std::uint8_t> bytes, std::span<const std::string> inputs) {
    check_output(path, inputs);
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) throw Error(3, "cannot open output '" + path + "'");
    if (!bytes.empty()) output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    output.close(); if (!output) throw Error(3, "cannot write output '" + path + "'");
}
std::vector<std::string> protected_files(const Arguments& args, const Options& options) {
    auto result = options.inputs; result.insert(result.end(), args.protected_inputs.begin(), args.protected_inputs.end()); return result;
}
bool is_archive(std::span<const std::uint8_t> bytes) {
    if (bytes.size() < 8) return false;
    const std::string_view magic(reinterpret_cast<const char*>(bytes.data()), 8);
    return magic == "!<arch>\n" || magic == "!<thin>\n";
}
std::optional<std::vector<NamedObject>> load_objects(const std::string& input, DiagnosticEngine& diagnostics) {
    auto bytes = read_file(input); std::vector<NamedObject> result;
    if (is_archive(bytes)) {
        auto archive = read_binary_archive(bytes, diagnostics); if (!archive) return std::nullopt;
        for (auto& m : *archive) result.push_back({input + '(' + m.name + ')', std::move(m.object)});
    } else {
        auto object = read_binary_object(bytes, diagnostics); if (!object) return std::nullopt;
        result.push_back({input, std::move(*object)});
    }
    return result;
}
int run_cli(std::string_view name, int argc, char** argv, const std::function<int(const Arguments&, DiagnosticEngine&)>& command) {
    DiagnosticEngine diagnostics; int status = 0;
    try {
        Arguments args; std::set<std::string> active;
        for (int i = 1; i < argc; ++i) expand(argv[i], args, active, 0);
        status = command(args, diagnostics);
    } catch (const Error& error) { std::cerr << name << ": " << error.what() << '\n'; status = error.status; }
    catch (const std::filesystem::filesystem_error& error) { std::cerr << name << ": " << error.what() << '\n'; status = 3; }
    catch (const std::exception& error) { std::cerr << name << ": " << error.what() << '\n'; status = 1; }
    SourceManager sources;
    for (const auto& diagnostic : diagnostics.diagnostics()) std::cerr << name << ": " << sources.format(diagnostic) << '\n';
    return std::cout ? status : 3;
}
} // namespace mlvm::tools
