// DomSymAs internal helpers: endian-aware binary cursor + section codecs.
// Not part of the public API.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "sandstone.hpp"

namespace domsymas::detail {

class Writer {
  public:
    explicit Writer(bool be) : be_(be) {}
    std::vector<uint8_t> buf;
    void u8(uint8_t v) { buf.push_back(v); }
    void u16(uint16_t v);
    void u32(uint32_t v);
    void i64(int64_t v) { u32((uint32_t)(v & 0xFFFFFFFFu)); u32((uint32_t)((v >> 32) & 0xFFFFFFFFu)); }
    void bytes(const uint8_t *p, size_t n) { buf.insert(buf.end(), p, p + n); }
    void str(const std::string &s) { u32((uint32_t)s.size()); bytes((const uint8_t *)s.data(), s.size()); }
    bool be_;
};

class Reader {
  public:
    Reader(const uint8_t *p, size_t n, bool be) : p_(p), n_(n), be_(be) {}
    bool u8(uint8_t &v);
    bool u16(uint16_t &v);
    bool u32(uint32_t &v);
    bool i64(int64_t &v);
    bool bytes(size_t n, const uint8_t *&out);
    bool skip(size_t n);
    bool str(std::string &out); // u32 len + bytes, capped at 16 MiB
    size_t pos() const { return pos_; }
    size_t left() const { return n_ - pos_; }
    size_t offset() const { return pos_; }
  private:
    const uint8_t *p_; size_t n_, pos_ = 0; bool be_;
};

// String-table interning for the builder/serializer.
uint32_t intern(std::vector<std::string> &table, const std::string &s);
bool fetch_str(const std::vector<std::string> &t, uint32_t idx, std::string &out);

// Section body encode/decode (record_count u32 prefix inside `size` bytes).
bool read_section_prefix(Reader &r, size_t size, uint32_t &count, std::string &diag);
void write_section_prefix(Writer &w, uint32_t count);

bool read_u32vec(Reader &r, uint32_t max, std::vector<uint32_t> &out, std::string &diag);
void write_u32vec(Writer &w, const std::vector<uint32_t> &v);

} // namespace domsymas::detail
