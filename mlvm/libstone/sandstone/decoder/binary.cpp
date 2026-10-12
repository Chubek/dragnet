// DomSymAs binary cursor + CRC32 (pure helpers, no I/O).
#include "detail.hpp"
#include <array>

namespace domsymas {

uint32_t crc32(const uint8_t *data, size_t len) {
    static const auto tab = [] {
        std::array<uint32_t, 256> values{};
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++)
                c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            values[i] = c;
        }
        return values;
    }();
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++)
        c = tab[(c ^ data[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

namespace detail {

void Writer::u16(uint16_t v) {
    if (be_) { buf.push_back((uint8_t)(v >> 8)); buf.push_back((uint8_t)v); }
    else { buf.push_back((uint8_t)v); buf.push_back((uint8_t)(v >> 8)); }
}
void Writer::u32(uint32_t v) {
    if (be_) { for (int i = 3; i >= 0; i--) buf.push_back((uint8_t)(v >> (i * 8))); }
    else { for (int i = 0; i < 4; i++) buf.push_back((uint8_t)(v >> (i * 8))); }
}
bool Reader::u8(uint8_t &v) {
    if (pos_ >= n_) return false;
    v = p_[pos_++];
    return true;
}
bool Reader::u16(uint16_t &v) {
    if (left() < 2) return false;
    if (be_) v = (uint16_t)((p_[pos_] << 8) | p_[pos_ + 1]);
    else v = (uint16_t)(p_[pos_] | (p_[pos_ + 1] << 8));
    pos_ += 2; return true;
}
bool Reader::u32(uint32_t &v) {
    if (left() < 4) return false;
    if (be_) v = ((uint32_t)p_[pos_] << 24) | ((uint32_t)p_[pos_ + 1] << 16) |
                 ((uint32_t)p_[pos_ + 2] << 8) | p_[pos_ + 3];
    else v = (uint32_t)p_[pos_] | ((uint32_t)p_[pos_ + 1] << 8) |
             ((uint32_t)p_[pos_ + 2] << 16) | ((uint32_t)p_[pos_ + 3] << 24);
    pos_ += 4; return true;
}
bool Reader::i64(int64_t &v) {
    uint32_t lo, hi;
    if (!u32(lo) || !u32(hi)) return false;
    v = (int64_t)(((uint64_t)hi << 32) | lo); return true;
}
bool Reader::bytes(size_t n, const uint8_t *&out) {
    if (left() < n) return false;
    out = p_ + pos_;
    pos_ += n;
    return true;
}
bool Reader::skip(size_t n) {
    if (left() < n) return false;
    pos_ += n;
    return true;
}
bool Reader::str(std::string &out) {
    uint32_t len = 0;
    if (!u32(len)) return false;
    if (len > (16u << 20)) return false; // 16 MiB cap per string
    const uint8_t *q = nullptr;
    if (!bytes(len, q)) return false;
    out.assign((const char *)q, len); return true;
}

uint32_t intern(std::vector<std::string> &table, const std::string &s) {
    for (uint32_t i = 0; i < table.size(); i++)
        if (table[i] == s) return i;
    table.push_back(s);
    return (uint32_t)(table.size() - 1);
}
bool fetch_str(const std::vector<std::string> &t, uint32_t idx, std::string &out) {
    if (idx >= t.size()) return false;
    out = t[idx];
    return true;
}
bool read_section_prefix(Reader &r, size_t size, uint32_t &count, std::string &diag) {
    if (size < 4) { diag = "section smaller than count prefix"; return false; }
    if (!r.u32(count)) { diag = "truncated section count"; return false; }
    if (count > 1000000) { diag = "absurd record count"; return false; }
    return true;
}
void write_section_prefix(Writer &w, uint32_t count) { w.u32(count); }
bool read_u32vec(Reader &r, uint32_t max, std::vector<uint32_t> &out, std::string &diag) {
    uint32_t n = 0;
    if (!r.u32(n)) { diag = "truncated vector length"; return false; }
    if (n > max) { diag = "vector length exceeds limit"; return false; }
    out.resize(n);
    for (uint32_t i = 0; i < n; i++)
        if (!r.u32(out[i])) { diag = "truncated vector element"; return false; }
    return true;
}
void write_u32vec(Writer &w, const std::vector<uint32_t> &v) {
    w.u32((uint32_t)v.size());
    for (uint32_t x : v) w.u32(x);
}

} // namespace detail
} // namespace domsymas
