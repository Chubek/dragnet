#pragma once
#include "cli.hpp"

namespace mlvm::tools {
inline int run_objcopy(std::string_view name, bool strip, int argc, char** argv) {
    return run_cli(name, argc, argv, [name, strip](const Arguments& args, DiagnosticEngine& diagnostics) {
        enum { remove = 256, only, rename, debug, all, locals };
        auto options = parse_options(args, "o:O:", {{"remove-section", remove, true}, {"only-section", only, true}, {"rename-section", rename, true},
            {"strip-debug", debug}, {"strip-all", all}, {"discard-all", locals}});
        if (show_info(name, options, "[options] OBJECT -o OUTPUT\n  --remove-section=NAME, --only-section=NAME (repeatable)\n  --rename-section=OLD=NEW, --strip-debug, --strip-all, --discard-all\n  -O binary: one file-backed section without relocations\n  mlvm-strip defaults to --discard-all; output must differ from input")) return 0;
        require_inputs(options, true); BinaryTransform transform; transform.strip = strip ? StripPolicy::locals : StripPolicy::none;
        std::string output; bool raw = false; std::optional<StripPolicy> requested_policy;
        for (const auto& o : options.options) {
            if (o.id == 'o') output = o.value;
            else if (o.id == 'O') { if (o.value != "binary") throw Error(2, "only binary output conversion is supported"); raw = true; }
            else if (o.id == remove || o.id == only) {
                if (o.value.empty()) throw Error(2, "section name must not be empty");
                (o.id == remove ? transform.remove_sections : transform.only_sections).insert(o.value);
            } else if (o.id == rename) {
                const auto eq = o.value.find('=');
                if (eq == std::string::npos || !eq || eq + 1 == o.value.size()) throw Error(2, "expected --rename-section=OLD=NEW");
                auto old = o.value.substr(0, eq), replacement = o.value.substr(eq + 1);
                auto [it, added] = transform.rename_sections.emplace(old, replacement);
                if (!added && it->second != replacement) throw Error(2, "conflicting section renames: " + old);
            } else {
                const auto policy = o.id == debug ? StripPolicy::debug : o.id == all ? StripPolicy::all : StripPolicy::locals;
                if (requested_policy && *requested_policy != policy) throw Error(2, "conflicting strip policies");
                requested_policy = policy; transform.strip = policy;
            }
        }
        const auto inputs = protected_files(args, options); check_output(output, inputs);
        auto object = read_binary_object(read_file(options.inputs[0]), diagnostics); if (!object) return 3;
        if (!transform_binary_object(*object, transform, diagnostics)) return 1;
        if (raw) {
            if (object->sections.size() != 1 || object->sections[0].type == 8 || !object->relocations.empty())
                throw Error(1, "raw extraction requires one file-backed section without relocations");
            write_file(output, object->sections[0].data, inputs);
        } else {
            auto bytes = write_binary_object(*object, diagnostics); if (!bytes) return 1;
            write_file(output, *bytes, inputs);
        }
        return 0;
    });
}
} // namespace mlvm::tools
