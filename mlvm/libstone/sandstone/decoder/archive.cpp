// DomSymAs archiving: whole-file save/load is container + payload codec.
// The outer archive layer (domarchive, e.g. zstd framing of stored files)
// is distinct from the SymAS payload compression id; helpers here keep the
// two responsibilities separate.
#include "sandstone.hpp"

#include <cstdio>

namespace domsymas {

namespace archive {

// Read an entire file with caps; shared by Databank::load_file users that
// want pre-archived (outer-compressed) inputs handled by the caller.
Error read_file(const std::string &path, std::vector<uint8_t> &out, size_t cap) {
    FILE *f = fopen(path.c_str(), "rb");
    if (!f) return Error{DOMSYMAS_ERR_IO, "cannot open " + path, 0};
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n < 0) { fclose(f); return Error{DOMSYMAS_ERR_IO, "seek failed", 0}; }
    if ((size_t)n > cap) { fclose(f); return Error{DOMSYMAS_ERR_OVERFLOW, "file too large", 0}; }
    out.resize((size_t)n);
    bool ok = n == 0 || fread(out.data(), 1, out.size(), f) == out.size();
    fclose(f);
    if (!ok) return Error{DOMSYMAS_ERR_IO, "read failed", 0};
    return Error{};
}

Error write_file(const std::string &path, const std::vector<uint8_t> &data) {
    FILE *f = fopen(path.c_str(), "wb");
    if (!f) return Error{DOMSYMAS_ERR_IO, "cannot write " + path, 0};
    bool ok = fwrite(data.data(), 1, data.size(), f) == data.size();
    fclose(f);
    if (!ok) return Error{DOMSYMAS_ERR_IO, "write failed", 0};
    return Error{};
}

} // namespace archive
} // namespace domsymas
