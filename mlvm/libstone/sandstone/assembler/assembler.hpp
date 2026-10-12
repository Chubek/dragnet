// Sandstone assembler framework (C++ API): RAII wrapper over assembler.h.
#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include "assembler.h"

namespace sandstone {

struct Reloc {
    std::string section;
    uint32_t offset = 0;
    std::string symbol;
    int64_t addend = 0;
    std::string type;
    uint32_t width_bits = 0;
    bool pcrel = false;
};

struct Symbol {
    std::string name;
    std::string section;
    uint64_t address = 0;
    bool global = false;
    bool defined = false;
};

struct Section {
    std::string name;
    std::vector<uint8_t> bytes;
    uint64_t base = 0;
};

class Assembler {
  public:
    explicit Assembler(const std::string &bank_path) {
        char err[1024] = {0};
        if (sandstone_asm_new(&ctx_, bank_path.c_str(), err, sizeof err) != 0)
            throw std::runtime_error(err[0] ? err : "cannot open bank");
    }
    Assembler(const uint8_t *data, size_t len) {
        char err[1024] = {0};
        if (sandstone_asm_new_from_memory(&ctx_, data, len, err, sizeof err) !=
            0)
            throw std::runtime_error(err[0] ? err : "cannot open bank");
    }
    ~Assembler() {
        if (ctx_)
            sandstone_asm_free(ctx_);
    }
    Assembler(const Assembler &) = delete;
    Assembler &operator=(const Assembler &) = delete;

    void set_syntax(const std::string &name) {
        if (sandstone_asm_set_syntax(ctx_, name.c_str()) != 0)
            throw std::runtime_error(last_error());
    }
    void enable_feature(const std::string &name) {
        if (sandstone_asm_enable_feature(ctx_, name.c_str()) != 0)
            throw std::runtime_error(last_error());
    }
    void assemble_string(const std::string &src,
                         const std::string &src_name = "<string>") {
        if (sandstone_asm_assemble_string(ctx_, src.c_str(),
                                          src_name.c_str()) != 0)
            throw std::runtime_error(last_error());
    }
    void assemble_file(const std::string &path) {
        if (sandstone_asm_assemble_file(ctx_, path.c_str()) != 0)
            throw std::runtime_error(last_error());
    }
    void write(const std::string &path, const std::string &format = "bin") {
        if (sandstone_asm_write(ctx_, path.c_str(), format.c_str()) != 0)
            throw std::runtime_error(last_error());
    }

    std::vector<Section> sections() const {
        std::vector<Section> out;
        uint32_t n = sandstone_asm_section_count(ctx_);
        for (uint32_t i = 0; i < n; i++) {
            Section s;
            s.name = sandstone_asm_section_name(ctx_, i);
            size_t len = 0;
            const uint8_t *bytes =
                sandstone_asm_section_bytes(ctx_, i, &len);
            s.bytes.assign(bytes, bytes + len);
            s.base = sandstone_asm_section_base(ctx_, i);
            out.push_back(std::move(s));
        }
        return out;
    }
    std::vector<uint8_t> image() const {
        size_t len = 0;
        const uint8_t *bytes = sandstone_asm_image(ctx_, &len);
        return std::vector<uint8_t>(bytes, bytes + len);
    }
    std::vector<Reloc> relocs() const {
        std::vector<Reloc> out;
        uint32_t n = 0;
        const sandstone_reloc_t *rs = sandstone_asm_relocs(ctx_, &n);
        for (uint32_t i = 0; i < n; i++) {
            Reloc r;
            r.section = rs[i].section;
            r.offset = rs[i].offset;
            r.symbol = rs[i].symbol;
            r.addend = rs[i].addend;
            r.type = rs[i].type;
            r.width_bits = rs[i].width_bits;
            r.pcrel = rs[i].pcrel != 0;
            out.push_back(std::move(r));
        }
        return out;
    }
    std::vector<Symbol> symbols() const {
        std::vector<Symbol> out;
        uint32_t n = 0;
        const sandstone_sym_t *ss = sandstone_asm_symbols(ctx_, &n);
        for (uint32_t i = 0; i < n; i++) {
            Symbol s;
            s.name = ss[i].name;
            s.section = ss[i].section;
            s.address = ss[i].address;
            s.global = ss[i].global != 0;
            s.defined = ss[i].defined != 0;
            out.push_back(std::move(s));
        }
        return out;
    }
    std::string last_error() const { return sandstone_asm_error(ctx_); }

  private:
    sandstone_asm_t *ctx_ = nullptr;
};

} // namespace sandstone
