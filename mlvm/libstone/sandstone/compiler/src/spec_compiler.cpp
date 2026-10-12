// sandstone-spec-compiler — JSON spec documents -> SymAS databank (.symas).
//
// Pipeline:
//
//   .dsymspec (Lua DSL, run by scripts/spec-compiler.py under stock Lua)
//       -> JSON document(s) -> domsymas::Builder -> .symas
//                              (spec_document.cpp, no interpreter needed)
//
// Multiple JSON inputs are folded into a single databank, so one bank may
// describe several instruction sets, syntaxes, and object formats. This
// driver is intentionally dependency-free (C++20 + the sandstone library
// only); the DomMune-embedded driver in main.cpp is built only when the
// SANDSTONE_SPEC_COMPILER_MUNE option is enabled and DomMune is available.
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "domsymascc/domsymascc.hpp"
#include "sandstone.hpp"

namespace {

void usage(const char *argv0) {
    std::cerr
        << "usage: " << argv0
        << " [-o OUT.symas] [--compression none|zlib|zstd] [--validate]\n"
        << "                  [--emit-spec [FILE]] [--spec-dir DIR] inputs...\n"
        << "\n"
        << "Compile JSON spec documents (as emitted by the dsl.lua DSL,\n"
        << "see compiler/stdspec/README.md) into a SymAS (.symas) databank.\n"
        << "Use scripts/spec-compiler.py to compile .dsymspec sources; it\n"
        << "drives stock Lua to produce the JSON this tool consumes.\n"
        << "\n"
        << "  -o OUT            output .symas path (required with several inputs\n"
        << "                    or --emit-spec to a file; with one input the\n"
        << "                    default replaces the input suffix with .symas)\n"
        << "  --compression C   none, zlib (default), or zstd\n"
        << "  --validate        reload the bank and run semantic validation\n"
        << "  --emit-spec [F]   write the merged JSON document to F ('-' or\n"
        << "                    absent = stdout) instead of building a bank\n"
        << "  --spec-dir DIR    accepted for CLI compatibility with\n"
        << "                    scripts/spec-compiler.py; otherwise unused\n"
        << "  --help            print this text\n";
}

std::string read_file(const std::string &path, bool &ok) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        ok = false;
        return {};
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    ok = !f.bad();
    return ss.str();
}

std::string default_output(const std::string &input) {
    const std::string kSuffix = ".json";
    if (input.size() >= kSuffix.size() &&
        input.compare(input.size() - kSuffix.size(), kSuffix.size(), kSuffix) ==
            0)
        return input.substr(0, input.size() - kSuffix.size()) + ".symas";
    return input + ".symas";
}

} // namespace

int main(int argc, char **argv) {
    std::string output;
    std::string compression = "zlib";
    std::string spec_dir;
    bool validate = false;
    bool emit_spec = false;
    std::string emit_dest = "-";
    std::vector<std::string> inputs;

    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto need_value = [&](const char *flag) -> std::string {
            if (i + 1 >= argc) {
                std::cerr << "sandstone-spec-compiler: " << flag
                          << " requires a value\n";
                std::exit(2);
            }
            return argv[++i];
        };
        if (a == "--help" || a == "-h") {
            usage(argv[0]);
            return 0;
        } else if (a == "-o" || a == "--output") {
            output = need_value("-o");
        } else if (a == "--compression") {
            compression = need_value("--compression");
        } else if (a == "--validate") {
            validate = true;
        } else if (a == "--emit-spec") {
            emit_spec = true;
            if (i + 1 < argc && argv[i + 1][0] != '-')
                emit_dest = argv[++i];
        } else if (a == "--spec-dir") {
            spec_dir = need_value("--spec-dir");
        } else if (!a.empty() && a[0] == '-') {
            std::cerr << "sandstone-spec-compiler: unknown option '" << a
                      << "'\n";
            return 2;
        } else {
            inputs.push_back(a);
        }
    }
    (void)spec_dir; // informational only for this driver

    if (inputs.empty()) {
        std::cerr << "sandstone-spec-compiler: at least one JSON input is "
                     "required\n";
        return 2;
    }

    std::vector<std::string> documents;
    for (const auto &input : inputs) {
        bool ok = false;
        std::string text = read_file(input, ok);
        if (!ok) {
            std::cerr << "sandstone-spec-compiler: cannot read '" << input
                      << "'\n";
            return 1;
        }
        documents.push_back(std::move(text));
    }

    if (emit_spec) {
        std::string joined;
        for (size_t i = 0; i < documents.size(); i++) {
            if (i)
                joined += "\n";
            joined += documents[i];
        }
        if (emit_dest == "-") {
            std::cout << joined;
            if (!joined.empty() && joined.back() != '\n')
                std::cout << "\n";
        } else {
            std::ofstream f(emit_dest, std::ios::trunc | std::ios::binary);
            if (!f) {
                std::cerr << "sandstone-spec-compiler: cannot write '"
                          << emit_dest << "'\n";
                return 1;
            }
            f << joined << "\n";
        }
        return 0;
    }

    domsymas::Builder builder(true);
    if (compression == "none")
        builder.set_compression(domsymas::Compression::None);
    else if (compression == "zlib")
        builder.set_compression(domsymas::Compression::Zlib);
    else if (compression == "zstd")
        builder.set_compression(domsymas::Compression::Zstd);
    else {
        std::cerr << "sandstone-spec-compiler: unknown compression '"
                  << compression << "' (none, zlib, or zstd)\n";
        return 2;
    }

    for (size_t i = 0; i < documents.size(); i++) {
        domsymas::cc::Result r =
            domsymas::cc::compile_document(documents[i], builder);
        if (!r.ok) {
            std::cerr << "sandstone-spec-compiler: " << inputs[i] << ": "
                      << r.error << "\n";
            return 1;
        }
    }

    std::string out_path = output.empty() && inputs.size() == 1
                               ? default_output(inputs[0])
                               : output;
    if (out_path.empty()) {
        std::cerr << "sandstone-spec-compiler: multiple inputs require "
                     "-o/--output\n";
        return 2;
    }

    domsymas::Error e = builder.save_file(out_path);
    if (!e.ok()) {
        std::cerr << "sandstone-spec-compiler: " << e.message << "\n";
        return 1;
    }

    if (validate) {
        domsymas::Databank bank;
        domsymas::Error le = bank.load_file(out_path);
        if (!le.ok()) {
            std::cerr << "sandstone-spec-compiler: validate failed: "
                      << le.message << "\n";
            return 1;
        }
        std::string dg;
        domsymas::Error ve = bank.validate(true, &dg);
        if (!ve.ok()) {
            std::cerr << "sandstone-spec-compiler: validate failed: " << dg
                      << "\n";
            return 1;
        }
        std::cout << "validated: " << out_path << "\n";
    } else {
        std::cout << "wrote: " << out_path << "\n";
    }
    return 0;
}
