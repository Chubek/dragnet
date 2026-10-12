#pragma once
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace firestone::gfx {
struct SPIRVInstruction { std::uint16_t opcode; std::vector<std::uint32_t> operands; };

// Binary framing validation only; opcode/type/control-flow validation belongs to a compiler/validator.
class SPIRV {
public:
    static constexpr std::uint32_t magic = 0x07230203;
    explicit SPIRV(unsigned minor_version = 0, std::uint32_t generator = 0)
        : words_{magic, 0x00010000u | (minor_version << 8), generator, 1, 0} {
        if (minor_version > 6) throw std::invalid_argument("SPIR-V version must be 1.0..1.6");
    }
    static SPIRV from_words(std::vector<std::uint32_t> words) {
        validate(words); SPIRV result; result.words_ = std::move(words); return result;
    }
    static SPIRV from_bytes(const std::vector<std::byte>& bytes) {
        if (bytes.size() < 20 || bytes.size() % 4) throw std::invalid_argument("truncated SPIR-V binary");
        std::vector<std::uint32_t> words(bytes.size() / 4);
        for (std::size_t i = 0; i < words.size(); ++i)
            for (unsigned j = 0; j < 4; ++j) words[i] |= std::uint32_t(std::to_integer<unsigned char>(bytes[i * 4 + j])) << (j * 8);
        if (words[0] == 0x03022307)
            for (auto& word : words) word = (word >> 24) | ((word >> 8) & 0x0000ff00) | ((word << 8) & 0x00ff0000) | (word << 24);
        return from_words(std::move(words));
    }
    const std::vector<std::uint32_t>& words() const noexcept { return words_; }
    std::uint32_t bound() const noexcept { return words_[3]; }
    unsigned minor_version() const noexcept { return (words_[1] >> 8) & 255; }
    std::uint32_t allocate_id() {
        if (words_[3] == std::numeric_limits<std::uint32_t>::max()) throw std::overflow_error("SPIR-V IDs exhausted");
        return words_[3]++;
    }
    void append(std::uint16_t opcode, const std::vector<std::uint32_t>& operands = {}) {
        if (operands.size() >= 65535) throw std::length_error("SPIR-V instruction too long");
        // Snapshot also permits append(opcode, words()).
        std::vector<std::uint32_t> instruction; instruction.reserve(operands.size() + 1);
        instruction.push_back((static_cast<std::uint32_t>(operands.size() + 1) << 16) | opcode);
        instruction.insert(instruction.end(), operands.begin(), operands.end());
        words_.insert(words_.end(), instruction.begin(), instruction.end());
    }
    std::vector<SPIRVInstruction> instructions() const {
        std::vector<SPIRVInstruction> result;
        for (std::size_t i = 5; i < words_.size();) {
            auto count = words_[i] >> 16;
            if (!count || count > words_.size() - i) throw std::logic_error("corrupt SPIR-V instruction length");
            result.push_back({static_cast<std::uint16_t>(words_[i]), {words_.begin() + static_cast<std::ptrdiff_t>(i + 1), words_.begin() + static_cast<std::ptrdiff_t>(i + count)}});
            i += count;
        }
        return result;
    }
    std::vector<std::byte> to_bytes() const {
        if (words_.size() > std::numeric_limits<std::size_t>::max() / 4) throw std::length_error("SPIR-V binary size overflow");
        std::vector<std::byte> bytes; bytes.reserve(words_.size() * 4);
        for (auto word : words_) for (unsigned i = 0; i < 4; ++i) bytes.push_back(static_cast<std::byte>((word >> (8 * i)) & 255));
        return bytes;
    }
private:
    std::vector<std::uint32_t> words_;
    static void validate(const std::vector<std::uint32_t>& words) {
        if (words.size() < 5 || words[0] != magic) throw std::invalid_argument("invalid SPIR-V header");
        if ((words[1] & 0xffff00ffu) != 0x00010000u || ((words[1] >> 8) & 255) > 6 || !words[3] || words[4])
            throw std::invalid_argument("invalid SPIR-V version, bound, or schema");
        for (std::size_t i = 5; i < words.size();) {
            auto count = words[i] >> 16;
            if (!count || count > words.size() - i) throw std::invalid_argument("invalid SPIR-V instruction length");
            i += count;
        }
    }
};
} // namespace firestone::gfx
