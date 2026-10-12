#include "cli.hpp"
#include "driver.hpp"
#include <algorithm>
#include <iostream>
#include <sstream>

using namespace mlvm;
using namespace mlvm::tools;

int main(int argc, char** argv) {
    return run_cli("mlvm-ld", argc, argv, [argc, argv](const Arguments& args, DiagnosticEngine& diagnostics) {
        enum { config = 256, base, defsym, map };
        auto options = parse_options(args, "o:O:re:", {{"target-config", config, true}, {"output-format", 'O', true},
            {"base-address", base, true}, {"defsym", defsym, true}, {"map", map, true}, {"entry", 'e', true}});
        if (show_info("mlvm-ld", options, "[options] OBJECT|ARCHIVE... -o OUTPUT\n  -r: x86-64 relocatable link; -e SYMBOL: static entry (default _start)\n  --target-config FILE -O binary: metadata-driven raw addressed image\n  --base-address ADDRESS, --defsym=NAME=ADDRESS (repeatable), --map FILE\n  Raw linking accepts ELF32/64, REL/RELA and GNU/BSD indexed archives")) return 0;
        const bool extended = std::any_of(options.options.begin(), options.options.end(), [](const auto& o) {
            return o.id == config || o.id == 'O' || o.id == base || o.id == defsym || o.id == map;
        });
        if (!extended) return mlvm::run_tool("mlvm-ld", argc, argv);
        require_inputs(options); std::string target_path, output, map_path; bool raw = false; RawLinkOptions link_options;
        for (const auto& o : options.options) {
            if (o.id == config) target_path = o.value;
            else if (o.id == 'o') output = o.value;
            else if (o.id == 'O') { if (o.value != "binary") throw Error(2, "only binary output is supported with a target contract"); raw = true; }
            else if (o.id == base) link_options.base_address = unsigned_number(o.value, "base address");
            else if (o.id == map) { if (o.value.empty()) throw Error(2, "map path must not be empty"); map_path = o.value; }
            else if (o.id == defsym) {
                const auto eq = o.value.find('=');
                if (eq == std::string::npos || !eq || eq + 1 == o.value.size()) throw Error(2, "expected --defsym=NAME=ADDRESS");
                const auto name = o.value.substr(0, eq); auto value = unsigned_number(o.value.substr(eq + 1), "external symbol address");
                if (!link_options.externals.emplace(name, value).second) throw Error(2, "duplicate external symbol: " + name);
            } else throw Error(2, "-r and entry selection are unsupported for raw addressed images");
        }
        if (!raw || target_path.empty()) throw Error(2, "raw linking requires --target-config FILE and -O binary");
        auto inputs = protected_files(args, options); inputs.push_back(target_path);
        check_output(output, inputs);
        if (!map_path.empty()) { check_output(map_path, inputs); if (same_file(map_path, output)) throw Error(2, "map and image output must differ"); }
        auto text = read_file(target_path); auto contract = parse_link_contract({reinterpret_cast<const char*>(text.data()), text.size()}, diagnostics);
        if (!contract) return 1;
        std::vector<BinaryObject> objects; std::vector<BinaryArchive> archives; std::uint64_t input_size = 0;
        for (const auto& input : options.inputs) {
            auto bytes = read_file(input);
            if (bytes.size() > 256 * 1024 * 1024 - input_size) throw Error(1, "link input byte budget exceeded");
            input_size += bytes.size();
            if (is_archive(bytes)) { auto archive = read_binary_archive(bytes, diagnostics); if (!archive) return 3; archives.push_back(std::move(*archive)); }
            else { auto object = read_binary_object(bytes, diagnostics); if (!object) return 3; objects.push_back(std::move(*object)); }
        }
        auto image = link_binary_objects(objects, archives, *contract, link_options, diagnostics); if (!image) return 1;
        std::ostringstream listing;
        if (!map_path.empty()) {
            listing << "MLVM raw link map 1\nbase " << link_options.base_address << "\n";
            for (const auto& s : image->sections) listing << "section " << s.object << ':' << s.section << ' ' << s.name << " address=" << s.address << " offset=" << s.offset << " size=" << s.size << " flags=" << s.flags << '\n';
            for (const auto& [name, address] : image->symbols) listing << "symbol " << name << ' ' << address << '\n';
            for (const auto& name : image->discarded_groups) listing << "discarded-group " << name << '\n';
        }
        write_file(output, image->data, inputs);
        if (!map_path.empty()) {
            const auto value = listing.str(); write_file(map_path, {reinterpret_cast<const std::uint8_t*>(value.data()), value.size()}, inputs);
        }
        return 0;
    });
}
