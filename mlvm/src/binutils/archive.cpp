#include "adapter.hpp"
#include <charconv>
#include <map>
#include <stdexcept>

namespace mlvm {
std::optional<std::vector<std::uint8_t>> write_indexed_archive(std::span<const ArchiveMember> members, DiagnosticEngine& diagnostics) {
    try {
        auto fail = [](std::string message) -> void { throw std::invalid_argument(std::move(message)); };
        if (members.size() > detail::archive_limits().members) fail("archive member count exceeds 4096");
        std::uint64_t input_size = 0, expanded = 0; auto limits = detail::object_limits();
        auto account = [&](std::uint64_t size) {
            if (size > detail::binary_byte_limit - expanded) fail("archive expanded byte budget exceeded");
            expanded += size;
        };
        detail::stone::ObjectArchive archive;
        for (const auto& member : members) {
            if (member.data.size() > detail::binary_byte_limit - input_size) fail("archive input byte budget exceeded");
            input_size += member.data.size();
            if (member.name.size() > detail::binary_byte_limit - input_size) fail("archive input byte budget exceeded");
            input_size += member.name.size(); account(member.name.size());
            detail::stone::ObjectFile object; object.format.machine = 62;
            const auto& bytes = member.data;
            if (bytes.size() >= 4 && bytes[0] == 0x7f && bytes[1] == 'E' && bytes[2] == 'L' && bytes[3] == 'F') {
                limits.bytes = detail::binary_byte_limit - expanded;
                auto decoded = detail::stone::load_elf(bytes, member.name, limits);
                if (!decoded) fail(decoded.error().message);
                object = std::move(decoded.value());
                limits.sections -= object.sections.size(); limits.symbols -= object.symbols.size();
                limits.relocations -= object.relocations.size(); limits.groups -= object.groups.size();
                for (const auto& s : object.sections) { account(s.size()); account(s.name.size()); }
                for (const auto& s : object.symbols) account(s.name.size());
                for (const auto& g : object.groups) account(g.name.size());
            } else account(bytes.size());
            archive.members.push_back({member.name, std::move(object)});
        }
        // Bin2Bin supplies deterministic GNU headers, long names and a symbol
        // index. Its object writer canonicalizes ELF payloads. Substitute the
        // original bytes and remap index offsets in this MLVM-owned adapter.
        auto canonical = detail::stone::emit_archive(archive, detail::archive_limits());
        if (!canonical) fail(canonical.error().message);
        const auto& source = canonical.value(); std::vector<std::uint8_t> output(source.begin(), source.begin() + 8);
        std::size_t index_offset = 0, index_size = 0, member_index = 0;
        std::map<std::uint32_t, std::uint32_t> offsets;
        for (std::size_t p = 8; p < source.size();) {
            const auto start = p; const auto header = std::span(source).subspan(p, 60);
            std::string_view size_field(reinterpret_cast<const char*>(header.data() + 48), 10);
            while (size_field.ends_with(' ')) size_field.remove_suffix(1);
            std::size_t size = 0; auto parsed = std::from_chars(size_field.data(), size_field.data() + size_field.size(), size);
            if (parsed.ec != std::errc{} || parsed.ptr != size_field.data() + size_field.size()) fail("invalid upstream archive size");
            p += 60; const auto payload = std::span(source).subspan(p, size); p += size + (size & 1);
            std::string_view name(reinterpret_cast<const char*>(header.data()), 16);
            while (name.ends_with(' ')) name.remove_suffix(1);
            const bool metadata = name == "/" || name == "//";
            if (!metadata && member_index >= members.size()) fail("invalid upstream archive member count");
            const auto data = metadata ? payload : std::span(members[member_index].data);
            const auto growth = 60 + data.size() + (data.size() & 1);
            if (growth > detail::binary_byte_limit - output.size()) fail("archive output byte budget exceeded");
            if (!metadata) { offsets.emplace(static_cast<std::uint32_t>(start), static_cast<std::uint32_t>(output.size())); ++member_index; }
            const auto out_header = output.size(); output.insert(output.end(), header.begin(), header.end());
            if (name == "/") { index_offset = output.size(); index_size = data.size(); }
            const auto field = std::to_string(data.size());
            if (field.size() > 10) fail("archive member size exceeds header field");
            std::fill(output.begin() + static_cast<std::ptrdiff_t>(out_header + 48), output.begin() + static_cast<std::ptrdiff_t>(out_header + 58), ' ');
            std::copy(field.begin(), field.end(), output.begin() + static_cast<std::ptrdiff_t>(out_header + 48));
            output.insert(output.end(), data.begin(), data.end()); if (data.size() & 1) output.push_back('\n');
        }
        if (member_index != members.size() || index_size < 4) fail("invalid upstream archive member/index layout");
        auto word = [&](std::size_t p) {
            std::uint32_t value = 0; for (unsigned i = 0; i < 4; ++i) value = (value << 8) | output.at(p + i); return value;
        };
        const auto count = word(index_offset);
        if (count > (index_size - 4) / 4) fail("invalid upstream archive index count");
        for (std::size_t i = 0; i < count; ++i) {
            const auto p = index_offset + 4 + 4 * i; auto found = offsets.find(word(p));
            if (found == offsets.end()) fail("invalid upstream archive index reference");
            for (unsigned j = 0; j < 4; ++j) output[p + j] = static_cast<std::uint8_t>(found->second >> (24 - 8 * j));
        }
        return output;
    } catch (const std::exception& error) { diagnostics.error("binary.archive.write", error.what()); return std::nullopt; }
}
} // namespace mlvm
