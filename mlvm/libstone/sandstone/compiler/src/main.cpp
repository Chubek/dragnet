// DomSymAsCC — the DomSymAs compiler driver.
//
// Compiles `.dsymspec` files (a Lua DSL run under the embedded DomMune
// interpreter) into SymAS databanks (`.symas`). The pipeline is:
//
//   .dsymspec (Lua DSL)  ->  JSON document  ->  domsymas::Builder  ->  .symas
//        (DomMune)            (this file)        (spec_document.cpp)
//
// Multiple `.dsymspec` inputs are folded into a single databank, so one bank
// may describe several instruction sets, syntaxes, and object formats.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include <DomCLI.hpp>

#include "mune/mune.h"

#include "domsymascc/domsymascc.hpp"

namespace fs = std::filesystem;

namespace {

std::string read_file(const std::string &path, std::string &err) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        err = "cannot open '" + path + "'";
        return {};
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

std::string trim(const std::string &s) {
    size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return {};
    size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

// Run one `.dsymspec` under DomMune; returns the JSON document on stdout.
bool run_spec(mune_runtime *rt, const std::string &path, const std::string &spec_dir,
              std::string &json_out, std::string &diag) {
    // Ensure the DSL module resolves: prepend the stdspec dir and the spec
    // file's own directory to DOMMUNEPATH (DomMune splits on ':').
    std::vector<std::string> path_entries;
    fs::path p(path);
    if (p.has_parent_path()) path_entries.push_back(p.parent_path().string());
    if (!spec_dir.empty()) path_entries.push_back(spec_dir);
    const char *existing = std::getenv("DOMMUNEPATH");
    if (existing && *existing) path_entries.push_back(existing);
    std::string joined;
    for (size_t i = 0; i < path_entries.size(); ++i) {
        if (i) joined += ':';
        joined += path_entries[i];
    }
    setenv("DOMMUNEPATH", joined.c_str(), 1);

    mune_result_t result;
    mune_status_t st = mune_run_file(rt, path.c_str(), &result);
    std::string stdout_text = result.captured_stdout ? result.captured_stdout : "";
    std::string stderr_text = result.captured_stderr ? result.captured_stderr : "";
    std::string err_text = result.error_message ? result.error_message : "";
    int exit_code = result.exit_code;
    mune_result_free(&result);

    if (st != MUNE_OK) {
        diag = stderr_text;
        if (diag.empty()) diag = err_text.empty() ? "DomMune error" : err_text;
        if (diag.empty()) diag = "DomMune returned status " + std::to_string(st);
        return false;
    }
    if (exit_code != 0) {
        diag = stderr_text.empty() ? "spec exited with error" : stderr_text;
        return false;
    }
    json_out = trim(stdout_text);
    if (json_out.empty()) {
        diag = "spec produced no output (expected a JSON document on stdout)";
        return false;
    }
    return true;
}

int list_arches(const std::string &spec_dir) {
    fs::path dir = spec_dir.empty() ? fs::path(".") : fs::path(spec_dir);
    if (!fs::is_directory(dir)) {
        std::cerr << "domsymascc: stdspec directory not found: " << dir << "\n";
        return 1;
    }
    for (const auto &entry : fs::directory_iterator(dir)) {
        if (!entry.is_regular_file()) continue;
        std::string ext = entry.path().extension().string();
        if (ext != ".dsymspec") continue;
        std::cout << entry.path().stem().string() << "\n";
    }
    return 0;
}

} // namespace

int main(int argc, char **argv) {
    cli::App app{"domsymascc", "0.1.0"};
    app.description(
        "Compile DomSymAs .dsymspec specifications into SymAS (.symas) databanks.");

    auto &inputs = app.positional<std::string>("inputs", ".dsymspec files to compile");
    inputs.required();
    inputs.multi();
    auto &output = app.option<std::string>("output", 'o', "output .symas path");
    auto &emit_spec = app.option<std::string>(
        "emit-spec", '\0', "write the JSON spec document to a path ('-' = stdout)");
    auto &spec_dir = app.option<std::string>(
        "spec-dir", '\0', "directory containing dsl.lua and standard specs");
    auto &compression = app.option<std::string>(
        "compression", '\0', "payload compression: none, zlib, or zstd");
    compression.default_value("zlib");
    auto &list_flag = app.flag("list-arches", '\0', "list available .dsymspec files");
    auto &validate_flag = app.flag("validate", '\0', "validate the output bank");

    app.action([&](cli::Command &) {
        std::string sd = *spec_dir;
        if (list_flag.on()) {
            cli::exit(list_arches(sd));
        }
        if (inputs.all().empty()) {
            cli::fail("at least one .dsymspec input is required");
        }

        // Locate a default stdspec dir relative to the binary when not given.
        std::string argv0 = argv[0];
        if (sd.empty()) {
            fs::path bin = fs::absolute(argv0);
            for (int i = 0; i < 8; ++i) {
                fs::path cand = bin / "domlibs" / "domsymas" / "compiler" / "stdspec";
                if (fs::is_directory(cand)) { sd = cand.string(); break; }
                if (!bin.has_parent_path()) break;
                bin = bin.parent_path();
            }
        }

        // Compile all inputs under one embedded DomMune runtime.
        mune_runtime *rt = nullptr;
        if (mune_runtime_create(argv0.c_str(), &rt) != MUNE_OK || !rt) {
            std::cerr << "domsymascc: failed to initialize DomMune runtime\n";
            cli::exit(1);
        }

        std::vector<std::string> documents;
        std::string diag;
        for (const auto &input : inputs.all()) {
            std::string json;
            if (!run_spec(rt, input, sd, json, diag)) {
                std::cerr << "domsymascc: " << input << ": " << diag << "\n";
                mune_runtime_destroy(rt);
                cli::exit(1);
            }
            documents.push_back(std::move(json));
        }
        mune_runtime_destroy(rt);

        // --emit-spec short-circuits before the builder.
        if (emit_spec.seen()) {
            std::string joined;
            for (size_t i = 0; i < documents.size(); ++i) {
                if (i) joined += "\n";
                joined += documents[i];
            }
            std::string dest = *emit_spec;
            if (dest == "-") {
                std::cout << joined;
                if (!joined.empty() && joined.back() != '\n') std::cout << "\n";
            } else {
                std::ofstream f(dest, std::ios::trunc | std::ios::binary);
                if (!f) {
                    std::cerr << "domsymascc: cannot write " << dest << "\n";
                    cli::exit(1);
                }
                f << joined << "\n";
            }
            cli::exit(0);
        }

        // Map every document onto a single builder.
        domsymas::Builder builder(true);
        std::string comp = *compression;
        if (comp == "none") builder.set_compression(domsymas::Compression::None);
        else if (comp == "zstd") builder.set_compression(domsymas::Compression::Zstd);
        else if (comp == "zlib") builder.set_compression(domsymas::Compression::Zlib);
        else {
            std::cerr << "domsymascc: unknown compression '" << comp << "'\n";
            cli::exit(2);
        }

        for (const auto &doc : documents) {
            domsymas::cc::Result r = domsymas::cc::compile_document(doc, builder);
            if (!r.ok) {
                std::cerr << "domsymascc: " << r.error << "\n";
                cli::exit(1);
            }
        }

        std::string out_path = output.seen()
                                   ? *output
                                   : (inputs.all().size() == 1
                                          ? ([] (const std::string &in) {
                                                std::string o = in;
                                                if (o.size() >= 9 &&
                                                    o.compare(o.size() - 9, 9, ".dsymspec") == 0)
                                                    o = o.substr(0, o.size() - 9) + ".symas";
                                                else
                                                    o += ".symas";
                                                return o;
                                            })(inputs.all()[0])
                                          : std::string());

        if (out_path.empty()) {
            std::cerr << "domsymascc: multiple inputs require -o/--output\n";
            cli::exit(2);
        }

        domsymas::Error e = builder.save_file(out_path);
        if (!e.ok()) {
            std::cerr << "domsymascc: " << e.message << "\n";
            cli::exit(1);
        }

        if (validate_flag.on()) {
            domsymas::Databank bank;
            domsymas::Error le = bank.load_file(out_path);
            if (!le.ok()) {
                std::cerr << "domsymascc: validate failed: " << le.message << "\n";
                cli::exit(1);
            }
            std::string dg;
            domsymas::Error ve = bank.validate(true, &dg);
            if (!ve.ok()) {
                std::cerr << "domsymascc: validate failed: " << dg << "\n";
                cli::exit(1);
            }
            std::cout << "validated: " << out_path << "\n";
        } else {
            std::cout << "wrote: " << out_path << "\n";
        }
        cli::exit(0);
    });

    return app.run(argc, argv);
}
