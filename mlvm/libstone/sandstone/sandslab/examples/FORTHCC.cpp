#include "Forth.hpp"

#include <iostream>
#include <iterator>

int main(int argc, char **argv) {
    try {
        std::filesystem::path isa_dir = SANDSLAB_ISA_DIR;
        std::string input_path, output_path;
        bool assembly = false;
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "--help" || arg == "-h") {
                std::cout << "Usage: forthcc [--isa-dir DIR] [--asm] [-o OUTPUT] [INPUT|-]\n"
                             "Compile a Forth expression to raw x86-64 function bytes (or assembly).\n"
                             "Reads stdin by default. Words: + - * and or xor negate dup drop swap over.\n";
                return 0;
            }
            if (arg == "--asm") { assembly = true; continue; }
            if (arg == "-o" || arg == "--isa-dir") {
                if (++i == argc) throw sandslab::Error("missing argument after " + arg);
                if (arg == "-o") output_path = argv[i]; else isa_dir = argv[i];
            } else if ((arg != "-" && arg.starts_with('-')) || !input_path.empty()) {
                throw sandslab::Error("unexpected argument: " + arg);
            } else input_path = arg;
        }
        std::ifstream file;
        if (!input_path.empty() && input_path != "-") {
            file.open(input_path);
            if (!file) throw sandslab::Error("cannot open input: " + input_path);
        }
        std::istream &input = file.is_open() ? file : std::cin;
        std::string source{std::istreambuf_iterator<char>(input), {}};
        if (input.bad()) throw sandslab::Error("failed reading input");
        auto program = sandslab::examples::compile_forth(source, sandslab::isa::load_arch("x86", isa_dir));
        auto code = program.compile();
        std::ofstream output_file;
        if (!output_path.empty() && output_path != "-") {
            output_file.open(output_path, std::ios::binary);
            if (!output_file) throw sandslab::Error("cannot open output: " + output_path);
        }
        std::ostream &output = output_file.is_open() ? output_file : std::cout;
        if (assembly)
            output << ".intel_syntax noprefix\n.text\n.globl forth_entry\nforth_entry:\n" << code.assembly;
        else output.write(reinterpret_cast<const char *>(code.bytes.data()), static_cast<std::streamsize>(code.bytes.size()));
        output.flush();
        if (!output) throw sandslab::Error("failed writing output");
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "forthcc: " << e.what() << '\n';
        return 1;
    }
}
