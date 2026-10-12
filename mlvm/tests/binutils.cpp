#include "test.hpp"
#include <mlvm/binutils.hpp>
#include <array>

namespace {
mlvm::BinaryObject fixture(unsigned bits, mlvm::Endianness order) {
    mlvm::BinaryObject object; object.format.bits = bits; object.format.endianness = order;
    object.format.machine = bits == 32 ? 3 : 62;
    object.sections.push_back({".text", 1, 6, 4, 0, 0, {0, 0, 0, 0, 0xc3}});
    object.sections.push_back({".bss", 8, 3, 8, 0, 24, {}});
    object.symbols.push_back({"entry", 0, 0, 5, mlvm::Binding::global, 2, 0});
    object.symbols.push_back({"target", -1, 0, 0, mlvm::Binding::global, 0, 0});
    object.symbols.push_back({"local", 0, 4, 1, mlvm::Binding::local, 0, 2});
    object.relocations.push_back({0, 1, 2, 0, bits == 32 ? 0 : -4, bits == 32});
    return object;
}
constexpr auto contract_text = R"(
version = 1
class = 64
machine = 62
endianness = "little"
flags = 0
osabi = 0
abi_version = 0
text_alignment = 4
[[relocations]]
type = 2
name = "pc32"
kind = "pc_relative"
storage_bytes = 4
bit_offset = 0
bits = 32
scale = 1
signed = true
)";
std::vector<std::uint8_t> archive_record(std::string name, std::span<const std::uint8_t> payload) {
    auto field = [](std::string value, std::size_t width) { value.resize(width, ' '); return value; };
    const auto header = field(std::move(name), 16) + field("0", 12) + field("0", 6) + field("0", 6) + field("100644", 8) + field(std::to_string(payload.size()), 10) + "`\n";
    std::vector<std::uint8_t> bytes(header.begin(), header.end()); bytes.insert(bytes.end(), payload.begin(), payload.end());
    if (payload.size() & 1) bytes.push_back('\n');
    return bytes;
}
}
int main() { return test_main([] {
    mlvm::DiagnosticEngine diagnostics;
    for (auto bits : {32U, 64U}) for (auto order : {mlvm::Endianness::little, mlvm::Endianness::big}) {
        auto object = fixture(bits, order);
        auto bytes = mlvm::write_binary_object(object, diagnostics); CHECK(bytes);
        auto decoded = mlvm::read_binary_object(*bytes, diagnostics); CHECK(decoded);
        CHECK(decoded->format.bits == bits); CHECK(decoded->format.endianness == order);
        CHECK(decoded->sections[1].size() == 24); CHECK(decoded->sections[1].data.empty());
        CHECK(decoded->relocations[0].implicit_addend == (bits == 32));
        auto encoded = mlvm::write_binary_object(*decoded, diagnostics); CHECK(encoded); CHECK(*encoded == *bytes);
        for (std::size_t n = 0; n < bytes->size(); ++n) {
            CHECK(!mlvm::read_binary_object(std::span(bytes->data(), n), diagnostics)); diagnostics.clear();
        }
    }
    auto object = fixture(64, mlvm::Endianness::little);
    mlvm::BinaryArchive members{{"a very long object name.o", object}, {"short.o", object}};
    auto archive = mlvm::write_binary_archive(members, diagnostics); CHECK(archive);
    auto decoded_archive = mlvm::read_binary_archive(*archive, diagnostics); CHECK(decoded_archive);
    CHECK(decoded_archive->size() == 2); CHECK(decoded_archive->at(0).name == members[0].name);
    auto legacy = mlvm::read_archive(*archive, diagnostics); CHECK(legacy); CHECK(legacy->at(0).name == members[0].name);
    CHECK(mlvm::read_elf(legacy->at(0).data, diagnostics) == std::nullopt); // BSS is outside the baseline contract.
    diagnostics.clear(); archive->pop_back(); CHECK(!mlvm::read_binary_archive(*archive, diagnostics)); diagnostics.clear();
    auto payload = mlvm::write_binary_object(object, diagnostics); CHECK(payload);
    std::string bsd_name = "bsd long member name.o";
    std::vector<std::uint8_t> bsd_payload(bsd_name.begin(), bsd_name.end());
    bsd_payload.insert(bsd_payload.end(), payload->begin(), payload->end());
    std::vector<std::uint8_t> bsd{'!', '<', 'a', 'r', 'c', 'h', '>', '\n'};
    auto record = archive_record("#1/" + std::to_string(bsd_name.size()), bsd_payload);
    bsd.insert(bsd.end(), record.begin(), record.end());
    auto bsd_read = mlvm::read_binary_archive(bsd, diagnostics); CHECK(bsd_read); CHECK(bsd_read->at(0).name == bsd_name);
    std::vector<std::uint8_t> names;
    for (unsigned i = 0; i < 4097; ++i) names.insert(names.end(), {'n', '/', '\n'});
    auto table = archive_record("//", names); bsd.resize(8); bsd.insert(bsd.end(), table.begin(), table.end());
    CHECK(!mlvm::read_archive(bsd, diagnostics)); diagnostics.clear();
    const std::array<mlvm::ArchiveMember, 2> original_members{{{"very long raw payload name.dat", {1, 2, 3}}, {"object.o", *payload}}};
    auto preserving = mlvm::write_indexed_archive(original_members, diagnostics); CHECK(preserving);
    auto preserved = mlvm::read_archive(*preserving, diagnostics); CHECK(preserved);
    CHECK(preserved->at(0).data == original_members[0].data); CHECK(preserved->at(1).data == original_members[1].data);
    // Tiny archive storage cannot multiply logical NOBITS budgets across members.
    mlvm::BinaryObject large; large.sections.push_back({".bss", 8, 3, 1, 0, 128 * 1024 * 1024, {}});
    auto large_bytes = mlvm::write_binary_object(large, diagnostics); CHECK(large_bytes);
    const std::array<mlvm::ArchiveMember, 2> amplified{{{"one.o", *large_bytes}, {"two.o", *large_bytes}}};
    auto amplified_bytes = mlvm::write_archive(amplified, diagnostics); CHECK(amplified_bytes);
    CHECK(!mlvm::read_binary_archive(*amplified_bytes, diagnostics)); diagnostics.clear();
    large.sections[0].zero_fill = UINT64_MAX; CHECK(!mlvm::write_binary_object(large, diagnostics)); diagnostics.clear();
    auto bad = object; bad.relocations[0].symbol = SIZE_MAX;
    CHECK(!mlvm::write_binary_object(bad, diagnostics)); diagnostics.clear();
    mlvm::BinaryTransform options; options.strip = mlvm::StripPolicy::locals;
    CHECK(mlvm::transform_binary_object(object, options, diagnostics)); CHECK(object.symbols.size() == 2);
    auto before = mlvm::write_binary_object(object, diagnostics); CHECK(before);
    options.strip = mlvm::StripPolicy::all;
    CHECK(!mlvm::transform_binary_object(object, options, diagnostics)); diagnostics.clear();
    CHECK(mlvm::write_binary_object(object, diagnostics) == before);
    options.strip = mlvm::StripPolicy::none; options.remove_sections.insert(".text");
    CHECK(mlvm::transform_binary_object(object, options, diagnostics)); CHECK(object.sections.size() == 1); CHECK(object.relocations.empty());
    CHECK(object.symbols.size() == 1); CHECK(object.symbols[0].name == "target");
    // A retained relocation to a removed definition must fail transactionally.
    object = fixture(64, mlvm::Endianness::little); object.symbols[1].section = 1;
    options.remove_sections = {".bss"}; before = mlvm::write_binary_object(object, diagnostics); CHECK(before);
    CHECK(!mlvm::transform_binary_object(object, options, diagnostics)); diagnostics.clear();
    CHECK(mlvm::write_binary_object(object, diagnostics) == before);
    // Group signatures and relocation members remain pinned during local strip.
    object = fixture(64, mlvm::Endianness::little); object.sections[0].flags |= 512;
    object.groups.push_back({".group", 2, 1, {{mlvm::BinaryGroupMember::Kind::content, 0}, {mlvm::BinaryGroupMember::Kind::rela, 0}}});
    options = {}; options.strip = mlvm::StripPolicy::locals;
    CHECK(mlvm::transform_binary_object(object, options, diagnostics)); CHECK(object.symbols.size() == 3);
    auto grouped = mlvm::write_binary_object(object, diagnostics); CHECK(grouped);
    auto group_back = mlvm::read_binary_object(*grouped, diagnostics); CHECK(group_back); CHECK(group_back->groups.size() == 1);
    auto contract = mlvm::parse_link_contract(contract_text, diagnostics); CHECK(contract);
    mlvm::RawLinkOptions group_link; group_link.externals["target"] = 0;
    const std::array duplicate_groups{object, object};
    auto deduplicated = mlvm::link_binary_objects(duplicate_groups, {}, *contract, group_link, diagnostics); CHECK(deduplicated);
    CHECK(deduplicated->discarded_groups == std::vector<std::string>{"local"});
    object = fixture(64, mlvm::Endianness::little);
    mlvm::RawLinkOptions link; link.base_address = 0x1000; link.externals["target"] = 0x1010;
    auto image = mlvm::link_binary_objects(std::span(&object, 1), {}, *contract, link, diagnostics); CHECK(image);
    CHECK(image->data[0] == 12); CHECK(image->data.size() == 32); CHECK(image->sections[1].size == 24);
    link.externals.clear(); CHECK(!mlvm::link_binary_objects(std::span(&object, 1), {}, *contract, link, diagnostics)); diagnostics.clear();
    link.externals["target"] = UINT64_MAX;
    CHECK(!mlvm::link_binary_objects(std::span(&object, 1), {}, *contract, link, diagnostics)); diagnostics.clear();
    mlvm::BinaryObject definition; definition.sections.push_back({".text", 1, 6, 4, 0, 0, {0xc3}});
    definition.symbols.push_back({"target", 0, 0, 1, mlvm::Binding::global, 2, 0});
    const std::array libraries{mlvm::BinaryArchive{{"target.o", definition}}}; link = {};
    image = mlvm::link_binary_objects(std::span(&object, 1), libraries, *contract, link, diagnostics); CHECK(image);
    CHECK(image->symbols.at("target") == 32); CHECK(image->data[0] == 28);
    object.symbols[1].binding = mlvm::Binding::weak;
    image = mlvm::link_binary_objects(std::span(&object, 1), libraries, *contract, link, diagnostics); CHECK(image);
    CHECK(!image->symbols.contains("target")); CHECK(image->data.size() == 32);
    const std::array duplicates{definition, definition};
    CHECK(!mlvm::link_binary_objects(duplicates, {}, *contract, link, diagnostics)); diagnostics.clear();
    // Strict TOML typing, unknown fields, duplicate encodings and versions.
    for (auto suffix : {"\nunknown = 1\n", "\npc_bias = \"bad\"\n"}) {
        CHECK(!mlvm::parse_link_contract(std::string(contract_text) + suffix, diagnostics)); diagnostics.clear();
    }
    auto invalid = std::string(contract_text); invalid.replace(invalid.find("version = 1"), 11, "version = 2");
    CHECK(!mlvm::parse_link_contract(invalid, diagnostics)); diagnostics.clear();
    invalid = contract_text; invalid.replace(invalid.find("bits = 32"), 9, "bits = 0");
    CHECK(!mlvm::parse_link_contract(invalid, diagnostics)); diagnostics.clear();
    CHECK(!mlvm::parse_link_contract("class = 64", diagnostics)); diagnostics.clear();
    invalid = contract_text; invalid.replace(invalid.find("name = \"pc32\""), 13, "name = \"pc\\u000032\"");
    CHECK(!mlvm::parse_link_contract(invalid, diagnostics)); diagnostics.clear();
    std::vector<std::pair<std::uint64_t, std::string>> strings;
    auto collect = [&](std::uint64_t offset, std::string_view value) { strings.emplace_back(offset, value); };
    const std::array<std::uint8_t, 11> ascii{0, 'h', 'e', 'l', 'l', 'o', 0, 't', 'a', 'i', 'l'};
    CHECK(mlvm::scan_binary_strings(ascii, {}, collect, diagnostics)); CHECK(strings.size() == 2); CHECK(strings[1].first == 7);
    strings.clear(); const std::array<std::uint8_t, 11> little{0, 0, 't', 0, 'a', 0, 'i', 0, 'l', 0, 'x'};
    CHECK(mlvm::scan_binary_strings(little, {4, mlvm::StringEncoding::utf16le_ascii}, collect, diagnostics));
    CHECK(strings.size() == 1); CHECK(strings[0].first == 2); CHECK(strings[0].second == "tail");
    CHECK(!mlvm::scan_binary_strings(ascii, {0, mlvm::StringEncoding::ascii}, collect, diagnostics));
}); }
