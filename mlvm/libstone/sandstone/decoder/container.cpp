// DomSymAs container: 23-byte file header read/write/validate.
// Header is always little-endian; bit0 of flags selects payload endianness.
#include "detail.hpp"

namespace domsymas {

bool read_file_header(const uint8_t *data, size_t len, FileHeader &out, Error &err) {
    if (!data) {
        err = {DOMSYMAS_ERR_TRUNCATED, "null SymAS input", 0};
        return false;
    }
    if (len < kFileHeaderSize) {
        err = {DOMSYMAS_ERR_TRUNCATED, "file smaller than SymAS header", 0};
        return false;
    }
    if (data[0] != 0xCE || data[1] != 0xEB || data[2] != 0x09) {
        err = {DOMSYMAS_ERR_BAD_MAGIC, "bad SymAS magic", 0};
        return false;
    }
    detail::Reader r(data + 3, len - 3, false); // header fields are LE
    uint16_t ver = 0, flags = 0;
    uint8_t comp = 0, r0 = 0, r1 = 0, r2 = 0;
    if (!r.u16(ver) || !r.u16(flags) || !r.u8(comp) || !r.u8(r0) || !r.u8(r1) ||
        !r.u8(r2) || !r.u32(out.uncompressed_size) || !r.u32(out.compressed_size) ||
        !r.u32(out.checksum)) {
        err = {DOMSYMAS_ERR_TRUNCATED, "truncated SymAS header", 3};
        return false;
    }
    if (r0 || r1 || r2) {
        err = {DOMSYMAS_ERR_BAD_FLAGS, "nonzero reserved header bytes", 8};
        return false;
    }
    if (ver != kVersion) {
        err = {DOMSYMAS_ERR_BAD_VERSION, "unsupported SymAS version", 3};
        return false;
    }
    if (comp > (uint8_t)Compression::Zstd) {
        err = {DOMSYMAS_ERR_BAD_COMPRESSION, "unknown compression id", 7};
        return false;
    }
    // Unknown mandatory flags: only bits 0..4 are defined; bits 8..15 alter
    // interpretation (offsets/checksums) and must be rejected, bits 5..7 are
    // optional informational and ignored.
    if (flags & 0xFF00u) {
        err = {DOMSYMAS_ERR_BAD_FLAGS, "unknown mandatory header flags", 5};
        return false;
    }
    out.version = ver;
    out.flags = flags;
    out.compression = (Compression)comp;
    out.big_endian = (flags & DOMSYMAS_FLAG_BIG_ENDIAN) != 0;
    if (out.uncompressed_size > (64u << 20)) {
        err = {DOMSYMAS_ERR_OVERFLOW, "declared payload size exceeds 64 MiB cap", 11};
        return false;
    }
    if ((uint64_t)out.compressed_size + kFileHeaderSize < out.compressed_size ||
        (uint64_t)kFileHeaderSize + out.compressed_size > len) {
        err = {DOMSYMAS_ERR_TRUNCATED, "compressed body truncated", kFileHeaderSize};
        return false;
    }
    return true;
}

void write_file_header(const FileHeader &h, std::vector<uint8_t> &out) {
    out.push_back(0xCE); out.push_back(0xEB); out.push_back(0x09);
    detail::Writer w(false);
    w.u16(h.version);
    uint16_t flags = h.flags;
    if (h.big_endian) flags |= DOMSYMAS_FLAG_BIG_ENDIAN;
    else flags &= (uint16_t)~DOMSYMAS_FLAG_BIG_ENDIAN;
    w.u16(flags);
    w.u8((uint8_t)h.compression);
    w.u8(0); w.u8(0); w.u8(0);
    w.u32(h.uncompressed_size);
    w.u32(h.compressed_size);
    w.u32(h.checksum);
    out.insert(out.end(), w.buf.begin(), w.buf.end());
}

} // namespace domsymas
