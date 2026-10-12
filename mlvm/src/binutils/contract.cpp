#include "adapter.hpp"
#include <toml.h>
#include <algorithm>
#include <cstdlib>
#include <memory>
#include <stdexcept>

namespace mlvm {
namespace {
struct Invalid : std::runtime_error { using std::runtime_error::runtime_error; };
void known(const toml_table_t* table, std::initializer_list<std::string_view> keys) {
    for (int i = 0; const auto* key = toml_key_in(table, i); ++i)
        if (std::find(keys.begin(), keys.end(), key) == keys.end()) throw Invalid("unknown target-contract field: " + std::string(key));
}
std::int64_t integer(const toml_table_t* table, const char* key) {
    auto value = toml_int_in(table, key);
    if (!value.ok) throw Invalid("target-contract field must be an integer: " + std::string(key));
    return value.u.i;
}
std::uint64_t number(const toml_table_t* table, const char* key, std::uint64_t bound = INT64_MAX) {
    auto value = integer(table, key);
    if (value < 0 || static_cast<std::uint64_t>(value) > bound) throw Invalid("target-contract field out of range: " + std::string(key));
    return static_cast<std::uint64_t>(value);
}
bool boolean(const toml_table_t* table, const char* key) {
    auto value = toml_bool_in(table, key);
    if (!value.ok) throw Invalid("target-contract field must be Boolean: " + std::string(key));
    return value.u.b != 0;
}
std::string string(const toml_table_t* table, const char* key) {
    auto value = toml_string_in(table, key);
    if (!value.ok) throw Invalid("target-contract field must be a string: " + std::string(key));
    std::unique_ptr<char, decltype(&std::free)> owner(value.u.s, std::free);
    return owner.get();
}
}
std::optional<LinkContract> parse_link_contract(std::string_view text, DiagnosticEngine& diagnostics) {
    try {
        if (text.size() > 1024 * 1024 || text.find('\0') != std::string_view::npos) throw Invalid("target contract exceeds 1 MiB or contains NUL");
        // tomlc99's string API is NUL-terminated. Reject NUL escapes before
        // decoding so a contract name cannot be silently truncated.
        if (text.find("\\u0000") != std::string_view::npos || text.find("\\U00000000") != std::string_view::npos) throw Invalid("NUL escapes are unsupported in target contracts");
        std::string input(text); char error[256]{};
        std::unique_ptr<toml_table_t, decltype(&toml_free)> table(toml_parse(input.data(), error, sizeof(error)), toml_free);
        if (!table) throw Invalid(error);
        known(table.get(), {"version", "class", "machine", "endianness", "flags", "osabi", "abi_version", "text_alignment", "relocations"});
        if (integer(table.get(), "version") != 1) throw Invalid("unsupported target-contract version");
        LinkContract result; auto& format = result.format;
        format.bits = static_cast<unsigned>(number(table.get(), "class", 64));
        format.machine = static_cast<std::uint16_t>(number(table.get(), "machine", UINT16_MAX));
        format.flags = static_cast<std::uint32_t>(number(table.get(), "flags", UINT32_MAX));
        format.osabi = static_cast<std::uint8_t>(number(table.get(), "osabi", UINT8_MAX));
        format.abi_version = static_cast<std::uint8_t>(number(table.get(), "abi_version", UINT8_MAX));
        auto order = string(table.get(), "endianness");
        if (order != "little" && order != "big") throw Invalid("target endianness must be little or big");
        format.endianness = order == "little" ? Endianness::little : Endianness::big;
        result.text_alignment = number(table.get(), "text_alignment");
        auto* relocations = toml_array_in(table.get(), "relocations");
        if (!relocations || toml_array_nelem(relocations) > 4096) throw Invalid("target contract requires a relocation array of at most 4096 entries");
        for (int i = 0; i < toml_array_nelem(relocations); ++i) {
            auto* record = toml_table_at(relocations, i);
            if (!record) throw Invalid("relocation entry must be a table");
            known(record, {"type", "name", "kind", "storage_bytes", "bit_offset", "bits", "scale", "signed", "pc_bias", "implicit_addend_signed"});
            RelocationEncoding r; r.type = static_cast<std::uint32_t>(number(record, "type", UINT32_MAX));
            r.name = string(record, "name"); const auto kind = string(record, "kind");
            if (kind != "absolute" && kind != "pc_relative") throw Invalid("unsupported relocation kind: " + kind);
            r.pc_relative = kind == "pc_relative"; r.signed_value = boolean(record, "signed");
            r.storage_bytes = static_cast<std::uint32_t>(number(record, "storage_bytes", 8));
            r.bit_offset = static_cast<std::uint32_t>(number(record, "bit_offset", 63));
            r.bits = static_cast<std::uint32_t>(number(record, "bits", 64)); r.scale = number(record, "scale");
            if (toml_key_exists(record, "pc_bias")) r.pc_bias = integer(record, "pc_bias");
            if (toml_key_exists(record, "implicit_addend_signed")) r.implicit_addend_signed = boolean(record, "implicit_addend_signed");
            result.relocations.push_back(std::move(r));
        }
        auto valid = detail::stone::validate(detail::to_stone(result));
        if (!valid) throw Invalid(valid.error().message);
        return result;
    } catch (const std::exception& error) { diagnostics.error("binary.target", error.what()); return std::nullopt; }
}
} // namespace mlvm
