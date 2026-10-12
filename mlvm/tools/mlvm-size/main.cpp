#include "cli.hpp"
#include <iomanip>
#include <iostream>

using namespace mlvm;
using namespace mlvm::tools;

int main(int argc, char** argv) {
    return run_cli("mlvm-size", argc, argv, [](const Arguments& args, DiagnosticEngine& diagnostics) {
        enum { format = 256, radix };
        auto options = parse_options(args, "AB", {{"format", format, true}, {"radix", radix, true}});
        if (show_info("mlvm-size", options, "[options] OBJECT|ARCHIVE...\n  -A --format=sysv (default): section/total sizes, including NOBITS\n  -B --format=berkeley: allocated text/data/bss/total\n  --radix=d|o|x (default d)")) return 0;
        require_inputs(options); bool berkeley = false; int base = 10;
        for (const auto& o : options.options) {
            if (o.id == 'A' || o.id == 'B') berkeley = o.id == 'B';
            else if (o.id == format) {
                if (o.value != "sysv" && o.value != "berkeley") throw Error(2, "size format must be sysv or berkeley");
                berkeley = o.value == "berkeley";
            } else {
                if (o.value != "d" && o.value != "o" && o.value != "x") throw Error(2, "radix must be d, o or x");
                base = o.value == "d" ? 10 : o.value == "o" ? 8 : 16;
            }
        }
        for (const auto& input : options.inputs) {
            auto objects = load_objects(input, diagnostics); if (!objects) return 3;
            for (const auto& item : *objects) {
                if (options.inputs.size() > 1 || item.name != input) std::cout << item.name << ":\n";
                std::cout << std::setbase(base); std::uint64_t total = 0;
                if (berkeley) {
                    std::uint64_t text = 0, data = 0, bss = 0;
                    for (const auto& s : item.object.sections) if (s.flags & 2) {
                        if (s.type == 8) bss += s.size();
                        else if (s.flags & 1) data += s.size();
                        else text += s.size();
                    }
                    std::cout << "text\tdata\tbss\ttotal\n" << text << '\t' << data << '\t' << bss << '\t' << text + data + bss << '\n';
                } else {
                    std::cout << "section\tsize\n";
                    for (const auto& s : item.object.sections) { std::cout << s.name << '\t' << s.size() << '\n'; total += s.size(); }
                    std::cout << "total\t" << total << '\n';
                }
                std::cout << std::dec;
            }
        }
        return 0;
    });
}
