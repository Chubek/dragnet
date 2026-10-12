#include <mlvm/object.hpp>
#include <charconv>
#include <set>

namespace mlvm {
std::optional<std::vector<ArchiveMember>> read_archive(std::span<const std::uint8_t> bytes, DiagnosticEngine& diagnostics) {
    auto fail = [&](const char* message) -> std::optional<std::vector<ArchiveMember>> { diagnostics.error("archive.read", message); return std::nullopt; };
    const std::string_view text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    constexpr std::size_t limit = 256 * 1024 * 1024;
    if (bytes.size() > limit) return fail("archive exceeds 256 MiB");
    if (text.starts_with("!<thin>\n")) return fail("thin archives require an external member resolver");
    if (!text.starts_with("!<arch>\n")) return fail("expected Unix archive magic");
    std::vector<ArchiveMember> members;
    std::size_t p = 8, expanded = 0;
    std::string_view name_table; bool has_name_table = false;
    std::set<std::size_t> name_offsets;
    auto trim = [](std::string_view field) { while (!field.empty() && field.back() == ' ') field.remove_suffix(1); return field; };
    auto number = [&](std::string_view field, std::size_t& value) {
        field = trim(field); const auto r = std::from_chars(field.data(), field.data() + field.size(), value);
        return !field.empty() && r.ec == std::errc{} && r.ptr == field.data() + field.size();
    };
    auto valid_name = [](std::string_view name) { return !name.empty() && name.find_first_of("\n\r") == std::string_view::npos && name.find('\0') == std::string_view::npos; };
    auto index_name = [](std::string_view name) { return name == "/" || name == "/SYM64/" || name == "__.SYMDEF" || name == "__.SYMDEF SORTED" || name == "__.SYMDEF_64" || name == "__.SYMDEF_64 SORTED"; };
    while (p < bytes.size()) {
        if (bytes.size() - p < 60 || text.substr(p + 58, 2) != "`\n") return fail("truncated or invalid archive header");
        auto name = trim(text.substr(p, 16));
        std::size_t size = 0;
        if (!number(text.substr(p + 48, 10), size)) return fail("invalid archive member size");
        p += 60;
        if (size > bytes.size() - p) return fail("archive member exceeds input size");
        auto payload = text.substr(p, size);
        p += size;
        if (size & 1) { if (p == bytes.size() || bytes[p] != '\n') return fail("missing archive padding"); ++p; }
        if (name == "//") {
            if (has_name_table) return fail("duplicate GNU archive name table");
            has_name_table = true; name_table = payload;
            for (std::size_t offset = 0; offset < name_table.size();) {
                const auto end = name_table.find("/\n", offset);
                if (end == std::string_view::npos || !valid_name(name_table.substr(offset, end - offset))) return fail("invalid GNU archive name table");
                if (name_offsets.size() >= 4096) return fail("GNU archive name count exceeds 4096");
                name_offsets.insert(offset); offset = end + 2;
            }
            continue;
        }
        if (index_name(name)) continue; // Symbol indexes are metadata, not members.
        if (name.starts_with("#1/")) {
            std::size_t length = 0;
            if (!number(name.substr(3), length) || length > payload.size()) return fail("invalid BSD archive name size");
            name = payload.substr(0, length); payload.remove_prefix(length);
            while (!name.empty() && name.back() == '\0') name.remove_suffix(1);
        } else if (name.starts_with('/')) {
            std::size_t offset = 0;
            if (!number(name.substr(1), offset) || !name_offsets.contains(offset)) return fail("invalid GNU archive name offset");
            name = name_table.substr(offset, name_table.find("/\n", offset) - offset);
        } else if (name.ends_with('/')) name.remove_suffix(1);
        if (index_name(name)) continue;
        if (!valid_name(name)) return fail("invalid archive member name");
        if (members.size() >= 4096) return fail("archive member count exceeds 4096");
        if (name.size() > limit - expanded) return fail("archive expanded byte budget exceeded");
        expanded += name.size();
        if (payload.size() > limit - expanded) return fail("archive expanded byte budget exceeded");
        expanded += payload.size();
        members.push_back({std::string(name), {payload.begin(), payload.end()}});
    }
    return members;
}
std::optional<std::vector<std::uint8_t>> write_archive(std::span<const ArchiveMember> members, DiagnosticEngine& diagnostics) {
    std::string header = "!<arch>\n";
    std::vector<std::uint8_t> bytes(header.begin(), header.end());
    std::set<std::string> names;
    for (const auto& member : members) {
        if (member.name.empty() || member.name.size() > 15 || member.name.find_first_of("/\\ \t\n\r") != std::string::npos ||
            member.name == "." || member.name == ".." || !names.insert(member.name).second || member.data.size() > 9999999999ULL) {
            diagnostics.error("archive.write", "archive requires unique simple filenames of at most 15 characters"); return std::nullopt;
        }
        auto field = [](std::string text, std::size_t width) { text.resize(width, ' '); return text; };
        header = field(member.name + '/', 16) + field("0", 12) + field("0", 6) + field("0", 6) + field("100644", 8) + field(std::to_string(member.data.size()), 10) + "`\n";
        bytes.insert(bytes.end(), header.begin(), header.end()); bytes.insert(bytes.end(), member.data.begin(), member.data.end());
        if (member.data.size() & 1) bytes.push_back('\n');
    }
    return bytes;
}
} // namespace mlvm
