#pragma once
#include "cli.hpp"
#include <iostream>

namespace mlvm::tools {
inline int run_reader(std::string_view name, int argc, char** argv) {
    return run_cli(name, argc, argv, [name](const Arguments& args, DiagnosticEngine& diagnostics) {
        enum { header = 256, groups };
        auto options = parse_options(args, "Ssra", {{"file-header", header}, {"sections", 'S'}, {"symbols", 's'}, {"relocs", 'r'}, {"groups", groups}, {"all", 'a'}});
        if (show_info(name, options, "[options] OBJECT|ARCHIVE...\n  --file-header, -S --sections, -s --symbols, -r --relocs, --groups\n  -a --all (default); ELF32/ELF64 relocatable objects, both byte orders")) return 0;
        require_inputs(options);
        bool all = options.options.empty(), headers = false, sections = false, symbols = false, relocs = false, group_view = false;
        for (const auto& o : options.options) {
            if (o.id == 'a') all = true;
            else if (o.id == header) headers = true;
            else if (o.id == 'S') sections = true;
            else if (o.id == 's') symbols = true;
            else if (o.id == 'r') relocs = true;
            else if (o.id == groups) group_view = true;
        }
        for (const auto& input : options.inputs) {
            auto objects = load_objects(input, diagnostics); if (!objects) return 3;
            for (const auto& item : *objects) {
                if (options.inputs.size() > 1 || item.name != input) std::cout << item.name << ":\n";
                if (all || headers) print_header(item.object);
                if (all || sections) print_sections(item.object);
                if (all || symbols) print_symbols(item.object);
                if (all || relocs) print_relocations(item.object);
                if (all || group_view) print_groups(item.object);
            }
        }
        return 0;
    });
}
} // namespace mlvm::tools
