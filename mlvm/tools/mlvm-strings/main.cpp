#include "cli.hpp"
#include <iomanip>
#include <iostream>
#include <limits>

using namespace mlvm;
using namespace mlvm::tools;

int main(int argc, char** argv) {
    return run_cli("mlvm-strings", argc, argv, [](const Arguments& args, DiagnosticEngine& diagnostics) {
        auto options = parse_options(args, "n:t:e:f", {{"bytes", 'n', true}, {"radix", 't', true}, {"encoding", 'e', true}, {"print-file-name", 'f'}});
        if (show_info("mlvm-strings", options, "[options] FILE...\n  -n --bytes LENGTH (default 4), -t --radix d|o|x (byte offsets)\n  -e --encoding s|l|b (ASCII / aligned UTF-16LE / UTF-16BE ASCII)\n  -f --print-file-name; printable code points 32..126, scans entire file")) return 0;
        require_inputs(options); StringOptions config; bool filename = false; int radix = 0;
        for (const auto& o : options.options) {
            if (o.id == 'n') {
                auto value = unsigned_number(o.value, "minimum string length", false);
                if (!value || value > std::numeric_limits<std::size_t>::max()) throw Error(2, "string length must be positive and fit size_t");
                config.minimum = static_cast<std::size_t>(value);
            } else if (o.id == 't') {
                if (o.value != "d" && o.value != "o" && o.value != "x") throw Error(2, "radix must be d, o or x");
                radix = o.value == "d" ? 10 : o.value == "o" ? 8 : 16;
            } else if (o.id == 'e') {
                if (o.value != "s" && o.value != "l" && o.value != "b") throw Error(2, "encoding must be s, l or b");
                config.encoding = o.value == "s" ? StringEncoding::ascii : o.value == "l" ? StringEncoding::utf16le_ascii : StringEncoding::utf16be_ascii;
            } else filename = true;
        }
        for (const auto& input : options.inputs) {
            auto bytes = read_file(input);
            if (!scan_binary_strings(bytes, config, [&](std::uint64_t offset, std::string_view value) {
                if (filename) std::cout << input << ": ";
                if (radix) std::cout << std::setbase(radix) << offset << std::dec << ' ';
                std::cout << value << '\n';
            }, diagnostics)) return 1;
        }
        return 0;
    });
}
