#include "cli.hpp"
#include <algorithm>
#include <iostream>

using namespace mlvm;
using namespace mlvm::tools;

int main(int argc, char** argv) {
    return run_cli("mlvm-nm", argc, argv, [](const Arguments& args, DiagnosticEngine& diagnostics) {
        enum { defined = 256 };
        auto options = parse_options(args, "gupn", {{"extern-only", 'g'}, {"undefined-only", 'u'}, {"defined-only", defined}, {"no-sort", 'p'}, {"numeric-sort", 'n'}});
        if (show_info("mlvm-nm", options, "[options] OBJECT|ARCHIVE...\n  -g --extern-only, -u --undefined-only, --defined-only\n  -n --numeric-sort, -p --no-sort (default: stable name order)")) return 0;
        require_inputs(options);
        bool external = false, undefined_only = false, defined_only = false, no_sort = false, numeric = false;
        for (const auto& o : options.options) {
            if (o.id == 'g') external = true;
            else if (o.id == 'u') undefined_only = true;
            else if (o.id == defined) defined_only = true;
            else if (o.id == 'p') no_sort = true;
            else if (o.id == 'n') numeric = true;
        }
        if ((undefined_only && defined_only) || (no_sort && numeric)) throw Error(2, "conflicting symbol filter/sort options");
        for (const auto& input : options.inputs) {
            auto objects = load_objects(input, diagnostics); if (!objects) return 3;
            for (const auto& item : *objects) {
                const auto& object = item.object;
                if (options.inputs.size() > 1 || item.name != input) std::cout << item.name << ":\n";
                std::vector<std::size_t> indexes;
                for (std::size_t i = 0; i < object.symbols.size(); ++i) {
                    const auto& s = object.symbols[i];
                    if ((external && s.binding == Binding::local) || (undefined_only && s.section != -1) || (defined_only && s.section == -1)) continue;
                    indexes.push_back(i);
                }
                if (!no_sort) std::stable_sort(indexes.begin(), indexes.end(), [&](auto a, auto b) {
                    const auto& left = object.symbols[a]; const auto& right = object.symbols[b];
                    if (numeric && left.value != right.value) return left.value < right.value;
                    return left.name < right.name;
                });
                print_symbols(object, indexes);
            }
        }
        return 0;
    });
}
