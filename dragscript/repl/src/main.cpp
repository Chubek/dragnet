#include <dragscript/repl.hpp>
#include <iostream>

int main(int argc, char** argv) {
    dragscript::ReplConfig config;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: dragscript-repl [--backend dsir|mlvm] [--history FILE]\n"
                         "                       [--no-history] [--no-multiline]\n";
            return 0;
        } else if (arg == "--no-history") {
            config.history_file.clear();
        } else if (arg == "--no-multiline") {
            config.multiline = false;
        } else if ((arg == "--history" || arg == "--backend") && i + 1 < argc) {
            std::string value = argv[++i];
            if (arg == "--history") config.history_file = value;
            else if (value == "dsir") config.backend = dragscript::Backend::dsir;
            else if (value == "mlvm" || value == "mlvm_ir") config.backend = dragscript::Backend::mlvm_ir;
            else { std::cerr << "Unknown backend: " << value << '\n'; return 1; }
        } else {
            std::cerr << "Unknown option or missing value: " << arg << '\n';
            return 1;
        }
    }
    try {
        dragscript::Repl repl(std::move(config));
        repl.run();
    } catch (const std::exception& error) {
        std::cerr << "REPL error: " << error.what() << '\n';
        return 1;
    }
}
