#pragma once
#include <mlvm/binutils.hpp>
#include <filesystem>
#include <stdexcept>

namespace mlvm::tools {
struct Error : std::runtime_error {
    int status;
    Error(int status, std::string message) : std::runtime_error(std::move(message)), status(status) {}
};
struct Arguments { std::vector<std::string> values, protected_inputs; };
struct OptionSpec { std::string name; int id; bool argument = false; };
struct Option { int id; std::string value; };
struct Options {
    std::vector<Option> options;
    std::vector<std::string> inputs;
    bool help = false, version = false;
};
Options parse_options(const Arguments&, std::string_view short_options, std::initializer_list<OptionSpec>);
bool show_info(std::string_view name, const Options&, std::string_view usage);
void require_inputs(const Options&, bool exactly_one = false);
std::uint64_t unsigned_number(std::string_view value, std::string_view description, bool allow_hex = true);
std::vector<std::uint8_t> read_file(const std::string&);
bool same_file(const std::filesystem::path&, const std::filesystem::path&);
void check_output(const std::string&, std::span<const std::string> protected_inputs);
void write_file(const std::string&, std::span<const std::uint8_t>, std::span<const std::string> protected_inputs);
std::vector<std::string> protected_files(const Arguments&, const Options&);
bool is_archive(std::span<const std::uint8_t>);
struct NamedObject { std::string name; BinaryObject object; };
std::optional<std::vector<NamedObject>> load_objects(const std::string&, DiagnosticEngine&);
int run_cli(std::string_view name, int argc, char** argv,
    const std::function<int(const Arguments&, DiagnosticEngine&)>& command);

void print_header(const BinaryObject&);
void print_sections(const BinaryObject&, const std::set<std::string>& filter = {});
void print_symbols(const BinaryObject&, std::span<const std::size_t> indexes, bool metadata = false);
void print_symbols(const BinaryObject&);
void print_relocations(const BinaryObject&, const std::set<std::string>& filter = {});
void print_groups(const BinaryObject&);
void print_contents(const BinaryObject&, bool disassemble, const std::set<std::string>& filter = {});
} // namespace mlvm::tools
