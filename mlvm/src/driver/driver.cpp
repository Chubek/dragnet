#include "driver.hpp"
#include <mlvm/mlvm.hpp>
#include <mlvm/object.hpp>
#include <mlvm/codegen.hpp>
#ifdef MLVM_HAS_OPTILISP
#include <mlvm/optilisp/pass.hpp>
#endif
#ifdef MLVM_HAS_CEPHYR
#include <cephyr/frontend.hpp>
#endif
#include <algorithm>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <sstream>
#include <stdexcept>

namespace mlvm {
namespace {
struct Error : std::runtime_error {
    int status;
    Error(int status, std::string message) : std::runtime_error(std::move(message)), status(status) {}
};
std::vector<std::uint8_t> read(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw Error(3, "cannot open '" + path + "'");
    input.seekg(0, std::ios::end); auto length = input.tellg();
    if (length < 0 || length > 256 * 1024 * 1024) throw Error(3, "input is unreadable or exceeds 256 MiB: " + path);
    input.seekg(0);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(length));
    if (!bytes.empty() && !input.read(reinterpret_cast<char*>(bytes.data()), length)) throw Error(3, "cannot read '" + path + "'");
    return bytes;
}
std::string text(const std::vector<std::uint8_t>& bytes) { return {bytes.begin(), bytes.end()}; }
bool same_file(const std::filesystem::path& a, const std::filesystem::path& b) {
    std::error_code ec;
    if (std::filesystem::equivalent(a, b, ec) && !ec) return true;
    return std::filesystem::weakly_canonical(a) == std::filesystem::weakly_canonical(b);
}
void write(const std::string& path, std::span<const std::uint8_t> bytes, const std::vector<std::string>& inputs, bool executable = false) {
    if (path.empty()) throw Error(2, "output requires -o FILE");
    for (const auto& input : inputs) if (same_file(path, input)) throw Error(2, "output would overwrite input '" + input + "'");
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) throw Error(3, "cannot open output '" + path + "'");
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())); output.close();
    if (!output) throw Error(3, "cannot write output '" + path + "'");
    if (executable) std::filesystem::permissions(path, std::filesystem::perms::owner_exec | std::filesystem::perms::group_exec | std::filesystem::perms::others_exec, std::filesystem::perm_options::add);
}
void write_text(const std::string& path, const std::string& value, const std::vector<std::string>& inputs) {
    if (path.empty() || path == "-") { std::cout << value; return; }
    write(path, {reinterpret_cast<const std::uint8_t*>(value.data()), value.size()}, inputs);
}
void expand_response(std::string argument, std::vector<std::string>& args, std::set<std::string>& active,
                     std::vector<std::string>& inputs, unsigned depth = 0) {
    if (argument.empty() || argument[0] != '@') { args.push_back(std::move(argument)); return; }
    if (depth >= 16) throw Error(2, "response-file nesting exceeds 16");
    const auto path = argument.substr(1);
    inputs.push_back(path);
    if (!active.insert(std::filesystem::weakly_canonical(path).string()).second) throw Error(2, "response-file cycle: " + path);
    const auto data = text(read(path));
    std::string token; char quote = 0; bool escape = false, started = false;
    auto flush = [&] { if (started) expand_response(token, args, active, inputs, depth + 1); token.clear(); started = false; };
    for (char c : data) {
        if (escape) { token += c; escape = false; started = true; }
        else if (c == '\\') { escape = true; started = true; }
        else if (quote) { if (c == quote) quote = 0; else token += c; }
        else if (c == '\'' || c == '"') { quote = c; started = true; }
        else if (std::isspace(static_cast<unsigned char>(c))) flush();
        else { token += c; started = true; }
        if (args.size() > 65536) throw Error(2, "too many response-file arguments");
    }
    if (quote || escape) throw Error(2, "unterminated quote or escape in response file");
    flush(); active.erase(std::filesystem::weakly_canonical(path).string());
}
void help(std::string_view name) {
    std::cout << name << " (MLVM " << version << ")\nUsage: " << name << " [options] FILE...\n"
              << "Common: --help --version --color=never @RESPONSE --\n";
    if (name == "mlvm") std::cout << "  --verify --print --passes=constant-fold,canonicalize,dce --dump-passes -o FILE\n"
        << "  --blackstone-dir=DIR --kernel-file=FILE --apply-kernel=ID (repeatable)\n";
    else if (name == "mlvm-as") std::cout << "  --target=x86_64-unknown-linux-gnu -o FILE (required)\n";
    else if (name == "mlvm-ld") std::cout << "  -r (relocatable) -e SYMBOL (default _start) -o FILE (required)\n";
    else if (name == "mlvm-ar") std::cout << "  t ARCHIVE | q -o ARCHIVE FILE... (deterministic short-name archive)\n";
    else if (name == "mlvm-strings") std::cout << "  -n LENGTH (default 4), printable ASCII strings\n";
    else if (name == "mlvm-objcopy") std::cout << "  --remove-section=NAME --only-section=NAME --rename-section=OLD=NEW -O binary -o FILE\n";
    else if (name == "mlvm-strip") std::cout << "  -o FILE (remove unreferenced local symbols)\n";
    else if (name == "mlvm-objdump") std::cout << "  -d (documented x86 subset) -s (section bytes) -t (symbols) -r (relocations)\n";
    else if (name == "cephyr") std::cout << "  -E --parse-only -fsyntax-only --emit-ir -S -c -o FILE -I DIR -D NAME[=VALUE] -U NAME\n"
        << "  -include FILE -std=c17 -ffreestanding -target x86_64-unknown-linux-gnu -O0 -O1\n"
        << "  Semantic/codegen subset: int name(void) { return integer-constant-expression; }\n";
    else std::cout << "  Inspect x86-64 ELF64 relocatable objects (see docs/TOOLS.md).\n";
}
struct Options {
    std::vector<std::string> inputs;
    std::vector<std::string> protected_inputs;
    std::string output, entry = "_start", passes, remove, only, rename, mode, archive_command;
    std::string blackstone;
    std::vector<std::string> kernel_files, kernel_applications;
    bool relocatable = false, dump = false, raw = false, disassemble = false;
    std::size_t minimum = 4;
#ifdef MLVM_HAS_CEPHYR
    cephyr::PreprocessOptions preprocessor;
#endif
};
Options options(std::string_view name, const std::vector<std::string>& args) {
    Options result; bool positional = false;
    for (std::size_t i = 0; i < args.size(); ++i) {
        const auto& arg = args[i];
        auto next = [&]() { if (++i == args.size()) throw Error(2, "missing argument for " + arg); return args[i]; };
        if (!positional && arg == "--") { positional = true; continue; }
        if (!positional && arg == "--color=never") continue;
        if (!positional && arg == "-o") {
            if (name != "mlvm" && name != "cephyr" && name != "mlvm-as" && name != "mlvm-ld" && name != "mlvm-ar" && name != "mlvm-objcopy" && name != "mlvm-strip") throw Error(2, "-o is unsupported by this tool");
            result.output = next(); continue;
        }
        if (!positional && (arg.starts_with("--target=") || (name == "cephyr" && arg == "-target"))) {
            if (name != "mlvm-as" && name != "cephyr") throw Error(2, "target selection unsupported by this tool");
            const auto target = arg == "-target" ? next() : arg.substr(9);
            if (!TargetRegistry::builtins().find(target)) throw Error(2, "unsupported target: " + target);
            continue;
        }
        if (!positional && name == "mlvm") {
            if (arg.starts_with("--kernel-file=")) {
                result.kernel_files.push_back(arg.substr(14)); result.protected_inputs.push_back(arg.substr(14)); continue;
            }
            if (arg.starts_with("--apply-kernel=")) { result.kernel_applications.push_back(arg.substr(15)); continue; }
            if (arg.starts_with("--blackstone-dir=")) { result.blackstone = arg.substr(17); continue; }
            if (arg == "--verify" || arg == "--print") { result.mode = arg; continue; }
            if (arg.starts_with("--passes=")) { result.passes = arg.substr(9); continue; }
            if (arg == "--dump-passes") { result.dump = true; continue; }
        }
        if (!positional && name == "mlvm-ld") {
            if (arg == "-r") { result.relocatable = true; continue; }
            if (arg == "-e" || arg == "--entry") { result.entry = next(); continue; }
        }
        if (!positional && name == "mlvm-strings" && arg == "-n") {
            const auto value = next(); const auto r = std::from_chars(value.data(), value.data() + value.size(), result.minimum);
            if (r.ec != std::errc{} || r.ptr != value.data() + value.size() || !result.minimum) throw Error(2, "string length must be positive");
            continue;
        }
        if (!positional && name == "mlvm-objcopy") {
            if (arg.starts_with("--remove-section=")) { result.remove = arg.substr(17); continue; }
            if (arg.starts_with("--only-section=")) { result.only = arg.substr(15); continue; }
            if (arg.starts_with("--rename-section=")) { result.rename = arg.substr(17); continue; }
            if (arg == "-O") { if (next() != "binary") throw Error(2, "only binary output conversion is supported"); result.raw = true; continue; }
        }
        if (!positional && name == "mlvm-objdump" && (arg == "-d" || arg == "-s" || arg == "-t" || arg == "-r")) {
            if (arg == "-d") result.disassemble = true;
            continue;
        }
#ifdef MLVM_HAS_CEPHYR
        if (!positional && name == "cephyr") {
            if (arg == "-E" || arg == "-fsyntax-only" || arg == "--parse-only" || arg == "--emit-ir" || arg == "-S" || arg == "-c") {
                if (!result.mode.empty() && result.mode != arg) throw Error(2, "conflicting compilation modes");
                result.mode = arg; continue;
            }
            if (arg == "-std=c17" || arg == "-ffreestanding" || arg == "-O0" || arg == "-O1") continue;
            if (arg == "-include") { result.preprocessor.forced_includes.push_back(next()); continue; }
            if (arg.starts_with("-I")) { result.preprocessor.include_paths.push_back(arg.size() == 2 ? next() : arg.substr(2)); continue; }
            if (arg.starts_with("-D") || arg.starts_with("-U")) {
                auto value = arg.size() == 2 ? next() : arg.substr(2); const auto eq = value.find('='); const auto key = value.substr(0, eq);
                if (key.empty() || !(std::isalpha(static_cast<unsigned char>(key[0])) || key[0] == '_') ||
                    key.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_") != std::string::npos) throw Error(2, "invalid macro name: " + key);
                if (arg[1] == 'U') { if (eq != std::string::npos) throw Error(2, "-U requires a macro name"); result.preprocessor.defines.erase(key); result.preprocessor.undefines.insert(key); }
                else { result.preprocessor.undefines.erase(key); result.preprocessor.defines[key] = eq == std::string::npos ? "1" : value.substr(eq + 1); }
                continue;
            }
        }
#endif
        if (!positional && name == "mlvm-ar" && result.archive_command.empty() && (arg == "t" || arg == "q")) { result.archive_command = arg; continue; }
        if (!positional && arg.starts_with('-')) throw Error(2, "unsupported option: " + arg);
        result.inputs.push_back(arg);
    }
    if (result.inputs.empty()) throw Error(2, "expected input file");
    return result;
}
void inspect(const Object& object, std::string_view tool, bool disassemble) {
    if (tool == "mlvm-size") {
        std::uint64_t total = 0; std::cout << "section\tsize\n";
        for (const auto& s : object.sections) { std::cout << s.name << '\t' << s.data.size() << '\n'; total += s.data.size(); }
        std::cout << "total\t" << total << '\n'; return;
    }
    if (tool != "mlvm-nm") {
        std::cout << "ELF64 little-endian x86-64 relocatable\n";
        for (std::size_t i = 0; i < object.sections.size(); ++i) {
            const auto& s = object.sections[i];
            std::cout << "section " << i << ' ' << s.name << " size=" << s.data.size() << " align=" << s.alignment << " flags=" << s.flags << '\n';
            if (tool == "mlvm-objdump") for (std::size_t p = 0; p < s.data.size();) {
                std::cout << "  " << std::hex << std::setw(8) << std::setfill('0') << p << ": ";
                const auto byte = s.data[p];
                if (disassemble && (s.flags & 4)) {
                    if (auto decoded = x86_64::decode(std::span(s.data).subspan(p))) {
                        std::cout << x86_64::format(decoded->instruction); p += decoded->size;
                    } else { std::cout << ".byte 0x" << std::setw(2) << unsigned(byte); ++p; }
                } else {
                    const auto end = std::min(p + 16, s.data.size());
                    for (; p < end; ++p) std::cout << std::setw(2) << unsigned(s.data[p]) << ' ';
                }
                std::cout << std::dec << std::setfill(' ') << '\n';
            }
        }
    }
    for (const auto& s : object.symbols) {
        const char binding = s.binding == Binding::local ? 'l' : s.binding == Binding::weak ? 'w' : 'g';
        std::cout << std::hex << std::setw(16) << std::setfill('0') << s.value << std::dec << std::setfill(' ') << ' ' << binding << ' '
                  << (s.section == -1 ? "UND" : s.section == -2 ? "ABS" : object.sections[static_cast<std::size_t>(s.section)].name) << ' ' << s.name << '\n';
    }
    if (tool != "mlvm-nm") for (const auto& r : object.relocations)
        std::cout << "reloc " << object.sections[r.section].name << '+' << r.offset << " type=" << r.type << ' ' << object.symbols[r.symbol].name << " addend=" << r.addend << '\n';
}
bool transform(Object& object, const Options& options, bool strip, DiagnosticEngine& diagnostics) {
    std::vector<std::int32_t> sections(object.sections.size(), -1);
    Object result;
    for (std::size_t i = 0; i < object.sections.size(); ++i) {
        auto section = object.sections[i];
        if ((!options.remove.empty() && section.name == options.remove) || (!options.only.empty() && section.name != options.only)) continue;
        if (!options.rename.empty()) {
            const auto eq = options.rename.find('=');
            if (eq == std::string::npos || eq == 0 || eq + 1 == options.rename.size()) throw Error(2, "expected --rename-section=OLD=NEW");
            if (section.name == options.rename.substr(0, eq)) section.name = options.rename.substr(eq + 1);
        }
        sections[i] = static_cast<std::int32_t>(result.sections.size()); result.sections.push_back(std::move(section));
    }
    std::set<std::size_t> referenced;
    for (const auto& r : object.relocations) if (sections[r.section] >= 0) referenced.insert(r.symbol);
    std::vector<std::size_t> symbols(object.symbols.size(), SIZE_MAX);
    for (std::size_t i = 0; i < object.symbols.size(); ++i) {
        auto symbol = object.symbols[i];
        if (symbol.section >= 0 && sections[static_cast<std::size_t>(symbol.section)] < 0) {
            if (referenced.contains(i)) { diagnostics.error("object.section", "retained relocation refers to removed section"); return false; }
            continue;
        }
        if (strip && symbol.binding == Binding::local && !referenced.contains(i)) continue;
        if (symbol.section >= 0) symbol.section = sections[static_cast<std::size_t>(symbol.section)];
        symbols[i] = result.symbols.size(); result.symbols.push_back(std::move(symbol));
    }
    for (auto r : object.relocations) if (sections[r.section] >= 0) {
        r.section = static_cast<std::size_t>(sections[r.section]); r.symbol = symbols[r.symbol]; result.relocations.push_back(r);
    }
    if (!verify_object(result, diagnostics)) return false;
    object = std::move(result); return true;
}
int execute_tool(std::string_view name, const Options& opts, SourceManager& sources, DiagnosticEngine& diagnostics) {
    Context context; register_builtin_dialects(context, diagnostics);
    auto one = [&]() { if (opts.inputs.size() != 1) throw Error(2, "expected exactly one input file"); };
    auto protected_inputs = [&] {
        auto result = opts.inputs;
        result.insert(result.end(), opts.protected_inputs.begin(), opts.protected_inputs.end());
        for (Identifier id = 1; const auto* source = sources.file(id); ++id) result.push_back(source->name);
        return result;
    };
    if (name == "mlvm" || name == "mlvm-as" || name == "cephyr") {
        one(); const auto data = text(read(opts.inputs[0])); const auto file = sources.add(opts.inputs[0], data);
        if (name == "mlvm-as") {
            auto object = assemble(data, sources, file, diagnostics); if (!object) return 1;
            auto bytes = write_elf(*object, diagnostics); if (!bytes) return 1;
            write(opts.output, *bytes, protected_inputs()); return 0;
        }
        if (name == "mlvm") {
            auto module = parse_ir(context, sources, file, diagnostics); if (!module) return 1;
            for (const auto& path : opts.kernel_files) module->kernel_sources.push_back(text(read(path)));
            module->kernel_applications.insert(module->kernel_applications.end(), opts.kernel_applications.begin(), opts.kernel_applications.end());
            if (opts.mode != "--verify" && opts.mode != "--print" &&
                (!module->kernel_sources.empty() || !module->kernel_applications.empty())) {
#ifdef MLVM_HAS_OPTILISP
                std::filesystem::path root = opts.blackstone;
                if (root.empty()) root = std::filesystem::is_directory(MLVM_BLACKSTONE_SOURCE_DIR)
                    ? MLVM_BLACKSTONE_SOURCE_DIR : MLVM_BLACKSTONE_INSTALL_DIR;
                auto registry = root / "optilisp/stdlib";
                if (!std::filesystem::is_directory(registry)) registry = root / "stdlib";
                if (!optilisp::run_kernels(*module, registry, root / "kernels", diagnostics)) return 1;
#else
                diagnostics.error("optilisp.unavailable", "kernel execution requires MLVM_ENABLE_OPTILISP=ON"); return 1;
#endif
            }
            PassManager passes; passes.dump_ir = opts.dump;
            if (opts.dump) passes.instrumentation = [](const PassEvent& event) { std::cerr << "// before " << event.name << '\n' << event.before << "// after " << event.name << '\n' << event.after; };
            if (!opts.passes.empty()) {
                std::istringstream input(opts.passes); std::string pass;
                while (std::getline(input, pass, ',')) {
                    if (pass == "constant-fold") passes.add(constant_fold_pass());
                    else if (pass == "canonicalize") passes.add(canonicalize_pass());
                    else if (pass == "dce") passes.add(dead_code_elimination_pass());
                    else throw Error(2, "unknown pass: " + pass);
                }
            }
            if (!passes.run(*module, diagnostics)) return 1;
            if (opts.mode != "--verify") write_text(opts.output, print_ir(*module), protected_inputs());
            return 0;
        }
#ifdef MLVM_HAS_CEPHYR
        if (opts.mode.empty()) throw Error(2, "select -E, --parse-only, -fsyntax-only, --emit-ir, -S or -c; hosted linking is unsupported");
        auto preprocessed = cephyr::preprocess(sources, file, opts.preprocessor, diagnostics); if (!preprocessed) return 1;
        if (opts.mode == "-E") { write_text(opts.output, *preprocessed, protected_inputs()); return 0; }
        const auto expanded = sources.add(opts.inputs[0] + "<preprocessed>", *preprocessed);
        if (opts.mode == "--parse-only") return cephyr::parse(sources, expanded, diagnostics) ? 0 : 1;
        auto module = cephyr::lower(context, sources, expanded, diagnostics); if (!module) return 1;
        if (opts.mode == "-fsyntax-only") return 0;
        if (opts.mode == "--emit-ir") { write_text(opts.output, print_ir(*module), protected_inputs()); return 0; }
        auto assembly = cephyr::emit_assembly(*module, diagnostics); if (!assembly) return 1;
        if (opts.mode == "-S") { write_text(opts.output, *assembly, protected_inputs()); return 0; }
        const auto asm_file = sources.add(opts.inputs[0] + "<assembly>", *assembly);
        auto object = assemble(*assembly, sources, asm_file, diagnostics); if (!object) return 1;
        auto bytes = write_elf(*object, diagnostics); if (!bytes) return 1;
        write(opts.output, *bytes, protected_inputs()); return 0;
#else
        throw Error(2, "Cephyr was disabled at build time");
#endif
    }
    if (name == "mlvm-strings") {
        for (const auto& input : opts.inputs) {
            auto bytes = read(input); std::string current;
            auto flush = [&] { if (current.size() >= opts.minimum) std::cout << current << '\n'; current.clear(); };
            for (auto byte : bytes) { if (byte >= 32 && byte <= 126) current += static_cast<char>(byte); else flush(); } flush();
        }
        return 0;
    }
    if (name == "mlvm-ar") {
        if (opts.archive_command == "t") {
            one(); auto members = read_archive(read(opts.inputs[0]), diagnostics); if (!members) return 3;
            for (const auto& member : *members) std::cout << member.name << '\n';
            return 0;
        }
        if (opts.archive_command != "q") throw Error(2, "expected archive command t or q");
        std::vector<ArchiveMember> members;
        for (const auto& input : opts.inputs) members.push_back({std::filesystem::path(input).filename().string(), read(input)});
        auto bytes = write_archive(members, diagnostics); if (!bytes) return 1;
        write(opts.output, *bytes, protected_inputs()); return 0;
    }
    if (name == "mlvm-ld") {
        std::vector<Object> objects;
        for (const auto& input : opts.inputs) {
            auto bytes = read(input);
            if (bytes.size() >= 8 && std::string_view(reinterpret_cast<const char*>(bytes.data()), 8) == "!<arch>\n") {
                auto members = read_archive(bytes, diagnostics); if (!members) return 3;
                std::vector<Object> candidates;
                for (const auto& member : *members) { auto object = read_elf(member.data, diagnostics); if (!object) return 3; candidates.push_back(std::move(*object)); }
                std::set<std::size_t> extracted;
                bool changed = true;
                while (changed) {
                    changed = false;
                    auto combined = link_relocatable(objects, diagnostics); if (!combined) return 1;
                    std::set<std::string> needed;
                    for (const auto& symbol : combined->symbols) if (symbol.section == -1 && symbol.binding == Binding::global) needed.insert(symbol.name);
                    for (std::size_t i = 0; i < candidates.size(); ++i) if (!extracted.contains(i)) {
                        bool required = false;
                        for (const auto& symbol : candidates[i].symbols) if (symbol.section != -1 && symbol.binding != Binding::local && needed.contains(symbol.name)) required = true;
                        if (required) { objects.push_back(candidates[i]); extracted.insert(i); changed = true; break; }
                    }
                }
            } else { auto object = read_elf(bytes, diagnostics); if (!object) return 3; objects.push_back(std::move(*object)); }
        }
        std::optional<std::vector<std::uint8_t>> output;
        if (opts.relocatable) { auto object = link_relocatable(objects, diagnostics); if (object) output = write_elf(*object, diagnostics); }
        else output = link_executable(objects, opts.entry, diagnostics);
        if (!output) return 1;
        write(opts.output, *output, protected_inputs(), !opts.relocatable); return 0;
    }
    if (name == "mlvm-objcopy" || name == "mlvm-strip") {
        one(); auto object = read_elf(read(opts.inputs[0]), diagnostics); if (!object) return 3;
        if (!transform(*object, opts, name == "mlvm-strip", diagnostics)) return 1;
        if (opts.raw) {
            if (object->sections.size() != 1 || !object->relocations.empty()) throw Error(1, "raw extraction requires one selected section without relocations");
            write(opts.output, object->sections[0].data, protected_inputs()); return 0;
        }
        auto bytes = write_elf(*object, diagnostics); if (!bytes) return 1;
        write(opts.output, *bytes, protected_inputs()); return 0;
    }
    for (const auto& input : opts.inputs) {
        auto object = read_elf(read(input), diagnostics); if (!object) return 3;
        if (opts.inputs.size() > 1) std::cout << input << ":\n";
        inspect(*object, name, opts.disassemble);
    }
    return 0;
}
}
int run_tool(std::string_view name, int argc, char** argv) {
    SourceManager sources; DiagnosticEngine diagnostics;
    int status = 0;
    try {
        std::vector<std::string> args, response_inputs; std::set<std::string> active;
        for (int i = 1; i < argc; ++i) expand_response(argv[i], args, active, response_inputs);
        if (args.size() == 1 && (args[0] == "--help" || args[0] == "-h")) { help(name); return 0; }
        if (args.size() == 1 && (args[0] == "--version" || args[0] == "-V")) { std::cout << name << " (MLVM) " << version << '\n'; return 0; }
        auto opts = options(name, args);
        opts.protected_inputs.insert(opts.protected_inputs.end(), response_inputs.begin(), response_inputs.end());
        status = execute_tool(name, opts, sources, diagnostics);
    } catch (const Error& error) { std::cerr << name << ": " << error.what() << '\n'; status = error.status; }
    catch (const std::filesystem::filesystem_error& error) { std::cerr << name << ": " << error.what() << '\n'; status = 3; }
    catch (const std::exception& error) { std::cerr << name << ": " << error.what() << '\n'; status = 1; }
    for (const auto& diagnostic : diagnostics.diagnostics()) std::cerr << sources.format(diagnostic) << '\n';
    if (!std::cout) return 3;
    return status;
}
} // namespace mlvm
