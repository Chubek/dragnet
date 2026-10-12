#include <iostream>
#include <string_view>

#ifndef MLVM_TOOL_NAME
#define MLVM_TOOL_NAME "mlvm-tool"
#endif

int main(int argc, char** argv) {
    constexpr std::string_view name{MLVM_TOOL_NAME};
    if (argc > 1) {
        const std::string_view arg{argv[1]};
        if (arg == "--help" || arg == "-h") {
            std::cout << name << " — MLVM toolchain scaffold\n"
                      << "Usage: " << name << " [options] input...\n\n"
                      << "This executable is a scaffold, not a functional compiler or "
                         "binary utility yet.\n"
                      << "See AGENTS.md for the implementation contract.\n";
            return 0;
        }
        if (arg == "--version" || arg == "-V") {
            std::cout << name << " (MLVM) 0.1.0-scaffold\n";
            return 0;
        }
    }
    std::cerr << name << ": not implemented yet; this is an MLVM scaffold.\n"
              << "Run '" << name << " --help' for usage and AGENTS.md for scope.\n";
    return argc > 1 ? 1 : 2;
}
