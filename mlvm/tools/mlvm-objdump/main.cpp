#include "cli.hpp"
#include <iostream>

using namespace mlvm;
using namespace mlvm::tools;

int main(int argc, char** argv) {
    return run_cli("mlvm-objdump", argc, argv, [](const Arguments& args, DiagnosticEngine& diagnostics) {
        enum { sections = 256 };
        auto options = parse_options(args, "fdstrj:", {{"file-header", 'f'}, {"section-headers", sections}, {"disassemble", 'd'}, {"full-contents", 's'}, {"syms", 't'}, {"relocs", 'r'}, {"section", 'j', true}});
        if (show_info("mlvm-objdump", options, "[options] OBJECT|ARCHIVE...\n  -f --file-header, --section-headers, -s --full-contents, -t --syms\n  -r --relocs, -d --disassemble (x86-64 subset), -j --section NAME\n  Default: headers, section bytes, symbols, relocations, groups")) return 0;
        require_inputs(options);
        bool headers = false, section_headers = false, contents = false, symbols = false, relocs = false, disassemble = false, requested = false;
        std::set<std::string> filter;
        for (const auto& o : options.options) {
            if (o.id == 'j') { if (o.value.empty()) throw Error(2, "section name must not be empty"); filter.insert(o.value); continue; }
            requested = true;
            if (o.id == 'f') headers = true;
            else if (o.id == sections) section_headers = true;
            else if (o.id == 's') contents = true;
            else if (o.id == 't') symbols = true;
            else if (o.id == 'r') relocs = true;
            else if (o.id == 'd') disassemble = true;
        }
        for (const auto& input : options.inputs) {
            auto objects = load_objects(input, diagnostics); if (!objects) return 3;
            for (const auto& item : *objects) {
                if (options.inputs.size() > 1 || item.name != input) std::cout << item.name << ":\n";
                if (!requested || headers) print_header(item.object);
                if (!requested || section_headers) print_sections(item.object, filter);
                if (!requested || contents) print_contents(item.object, false, filter);
                if (disassemble) print_contents(item.object, true, filter);
                if (!requested || symbols) print_symbols(item.object);
                if (!requested || relocs) print_relocations(item.object, filter);
                if (!requested) print_groups(item.object);
            }
        }
        return 0;
    });
}
