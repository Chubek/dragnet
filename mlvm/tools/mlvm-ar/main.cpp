#include "cli.hpp"
#include <algorithm>
#include <iostream>

using namespace mlvm;
using namespace mlvm::tools;

namespace {
bool safe_name(std::string_view name) {
    return !name.empty() && name != "." && name != ".." && name.find_first_of("/\\\n\r") == std::string_view::npos && name.find('\0') == std::string_view::npos;
}
}
int main(int argc, char** argv) {
    return run_cli("mlvm-ar", argc, argv, [](const Arguments& args, DiagnosticEngine& diagnostics) {
        enum { directory = 256 };
        auto options = parse_options(args, "o:", {{"output-dir", directory, true}});
        if (show_info("mlvm-ar", options, "COMMAND [options] FILE...\n  q FILE... -o ARCHIVE: create archive with ELF symbol index\n  t ARCHIVE: list members\n  r ARCHIVE FILE... -o NEW_ARCHIVE: replace/append members\n  d ARCHIVE MEMBER... -o NEW_ARCHIVE: delete members\n  p ARCHIVE [MEMBER...]: member bytes to binary stdout\n  x ARCHIVE [MEMBER...] --output-dir DIR: extract member bytes\n  GNU/BSD long names are accepted; output is deterministic GNU ar")) return 0;
        if (options.inputs.empty()) throw Error(2, "expected archive command q, t, r, d, p or x");
        const auto command = options.inputs.front();
        if (command != "q" && command != "t" && command != "r" && command != "d" && command != "p" && command != "x") throw Error(2, "unsupported archive command: " + command);
        options.inputs.erase(options.inputs.begin()); require_inputs(options);
        std::string output, output_directory;
        for (const auto& o : options.options) { if (o.id == 'o') output = o.value; else output_directory = o.value; }
        const bool writes_archive = command == "q" || command == "r" || command == "d";
        if (!writes_archive && !output.empty()) throw Error(2, "-o is only supported by q, r and d");
        if (command != "x" && !output_directory.empty()) throw Error(2, "--output-dir is only supported by x");
        if (command == "x" && output_directory.empty()) throw Error(2, "extraction requires --output-dir DIR");
        const auto inputs = protected_files(args, options);
        if (writes_archive) check_output(output, inputs);
        std::vector<ArchiveMember> archive; std::size_t first_object = 0;
        if (command != "q") {
            auto loaded = read_archive(read_file(options.inputs[0]), diagnostics); if (!loaded) return 3;
            archive = std::move(*loaded); first_object = 1;
        }
        if (command == "q" || command == "r") {
            std::set<std::string> replacements, names; std::uint64_t total = 0;
            for (const auto& m : archive) if (!names.insert(m.name).second) throw Error(1, "duplicate archive member: " + m.name);
            for (const auto& m : archive) total += m.data.size();
            for (std::size_t i = first_object; i < options.inputs.size(); ++i) {
                const auto& input = options.inputs[i]; auto name = std::filesystem::path(input).filename().string();
                if (!safe_name(name)) throw Error(2, "unsafe archive member name: " + name);
                if (!replacements.insert(name).second) throw Error(2, "duplicate input member name: " + name);
                auto bytes = read_file(input);
                auto found = std::find_if(archive.begin(), archive.end(), [&](const auto& m) { return m.name == name; });
                if (found != archive.end()) total -= found->data.size();
                if (bytes.size() > 256 * 1024 * 1024 - total) throw Error(1, "archive input byte budget exceeded");
                total += bytes.size();
                if (found == archive.end()) archive.push_back({std::move(name), std::move(bytes)});
                else found->data = std::move(bytes);
            }
        } else if (command == "t") {
            require_inputs(options, true);
            for (const auto& member : archive) std::cout << member.name << '\n';
            return 0;
        } else {
            std::set<std::string> selected(options.inputs.begin() + 1, options.inputs.end());
            for (const auto& name : selected)
                if (std::none_of(archive.begin(), archive.end(), [&](const auto& m) { return m.name == name; })) throw Error(1, "archive member not found: " + name);
            if (command == "d") {
                if (selected.empty()) throw Error(2, "deletion requires a member name");
                std::erase_if(archive, [&](const auto& m) { return selected.contains(m.name); });
            } else {
                std::vector<std::pair<std::string, const ArchiveMember*>> extracted; std::set<std::string> destinations;
                for (const auto& m : archive) {
                    if (!selected.empty() && !selected.contains(m.name)) continue;
                    std::string path;
                    if (command == "x") {
                        if (!safe_name(m.name)) throw Error(1, "unsafe extraction member name: " + m.name);
                        path = (std::filesystem::path(output_directory) / m.name).string(); check_output(path, inputs);
                        if (!destinations.insert(std::filesystem::weakly_canonical(path).string()).second) throw Error(1, "duplicate extraction destination: " + m.name);
                    }
                    extracted.emplace_back(std::move(path), &m);
                }
                if (command == "x" && !std::filesystem::is_directory(output_directory)) throw Error(3, "output directory does not exist: " + output_directory);
                for (const auto& [path, member] : extracted) {
                    const auto& bytes = member->data;
                    if (command == "x") write_file(path, bytes, inputs);
                    else std::cout.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
                }
                return 0;
            }
        }
        auto bytes = write_indexed_archive(archive, diagnostics); if (!bytes) return 1;
        write_file(output, *bytes, inputs); return 0;
    });
}
