#pragma once
#include "Module.hpp"
#include <cstring>

namespace firestone::vm {

using BytecodeBytes = std::vector<std::uint8_t>;
struct BinaryLimits {
    Index bytes = 64 * 1024 * 1024, items = 1000000, string_bytes = 1024 * 1024;
};
namespace binary_detail {
struct Writer {
    BytecodeBytes bytes;
    void u8(std::uint8_t value) { bytes.push_back(value); }
    void u64(std::uint64_t value) { for (unsigned n = 0; n < 8; ++n) u8(static_cast<std::uint8_t>(value >> (n * 8))); }
    void index(Index value) { u64(value == no_index ? UINT64_MAX : static_cast<std::uint64_t>(value)); }
    void string(const std::string& value) { index(value.size()); bytes.insert(bytes.end(), value.begin(), value.end()); }
    void signature(const Signature& value) {
        u8(static_cast<std::uint8_t>(value.result)); index(value.parameters.size());
        for (auto parameter : value.parameters) u8(static_cast<std::uint8_t>(parameter));
    }
};
struct Reader {
    const BytecodeBytes& bytes;
    BinaryLimits limits;
    Index position = 0, items = 0;
    [[noreturn]] void fail(const char* message) const { throw std::invalid_argument(std::string("bytecode binary: ") + message); }
    std::uint8_t u8() { if (position == bytes.size()) fail("truncated input"); return bytes[position++]; }
    std::uint64_t u64() { std::uint64_t result = 0; for (unsigned n = 0; n < 8; ++n) result |= std::uint64_t{u8()} << (n * 8); return result; }
    Index index() {
        auto value = u64();
        if (value == UINT64_MAX) return no_index;
        if (value >= static_cast<std::uint64_t>(no_index)) fail("index exceeds host capacity");
        return static_cast<Index>(value);
    }
    Index count() {
        auto value = index();
        if (value > limits.items - items || value > bytes.size() - position) fail("item budget exceeded");
        items += value; return value;
    }
    std::string string() {
        auto size = index();
        if (size > limits.string_bytes || size > bytes.size() - position) fail("invalid string length");
        auto begin = bytes.begin() + static_cast<std::ptrdiff_t>(position);
        std::string result(begin, begin + static_cast<std::ptrdiff_t>(size)); position += size; return result;
    }
    ValueKind value_kind() {
        auto value = static_cast<ValueKind>(u8()); if (!valid_kind(value)) fail("invalid value kind"); return value;
    }
    Signature signature() {
        Signature result; result.result = value_kind(); auto size = count();
        for (Index n = 0; n < size; ++n) result.parameters.push_back(value_kind());
        return result;
    }
};
} // namespace binary_detail

// FSVM v1: fixed-width little-endian fields; IEEE-754 binary64 floats.
inline BytecodeBytes encode(const Module& module) {
    static_assert(sizeof(double) == 8 && std::numeric_limits<double>::is_iec559, "bytecode requires IEEE binary64");
    require_valid(module);
    binary_detail::Writer writer;
    for (auto byte : std::string("FSVM")) writer.u8(static_cast<std::uint8_t>(byte));
    writer.u64(1); writer.index(module.constants.size());
    for (const auto& value : module.constants.values()) {
        writer.u8(static_cast<std::uint8_t>(kind(value)));
        switch (kind(value)) {
        case ValueKind::Null: break;
        case ValueKind::Boolean: writer.u8(require<bool>(value) ? 1 : 0); break;
        case ValueKind::Integer: writer.u64(static_cast<std::uint64_t>(require<std::int64_t>(value))); break;
        case ValueKind::Float: {
            std::uint64_t bits; auto number = require<double>(value); std::memcpy(&bits, &number, 8); writer.u64(bits); break;
        }
        case ValueKind::String: writer.string(require<std::string>(value)); break;
        default: throw std::invalid_argument("nonportable constant kind");
        }
    }
    writer.index(module.natives.size());
    for (const auto& native : module.natives) { writer.string(native.first); writer.signature(native.second); }
    writer.index(module.size());
    for (Index f = 0; f < module.size(); ++f) {
        const auto& fn = module.function(f); writer.string(fn.name);
        writer.u8(static_cast<std::uint8_t>(fn.model)); writer.signature(fn.signature);
        writer.index(fn.register_count); writer.index(fn.local_count); writer.index(fn.code.size());
        for (const auto& i : fn.code.instructions()) {
            writer.u8(static_cast<std::uint8_t>(i.opcode)); writer.index(i.destination);
            writer.index(i.operands.size()); for (auto operand : i.operands) writer.index(operand);
            writer.index(i.index); writer.index(i.target); writer.index(i.alternate);
            writer.index(i.argument_count); writer.u8(static_cast<std::uint8_t>(i.bits)); writer.string(i.symbol);
        }
    }
    return std::move(writer.bytes);
}
inline Module decode(const BytecodeBytes& bytes, const BinaryLimits& limits = {}) {
    if (bytes.size() > limits.bytes) throw std::invalid_argument("bytecode binary: byte budget exceeded");
    binary_detail::Reader reader{bytes, limits};
    for (auto byte : std::string("FSVM")) if (reader.u8() != static_cast<std::uint8_t>(byte)) reader.fail("bad magic");
    if (reader.u64() != 1) reader.fail("unsupported version");
    Module module; auto constants = reader.count();
    for (Index n = 0; n < constants; ++n) {
        auto tag = reader.value_kind(); Value value;
        switch (tag) {
        case ValueKind::Null: break;
        case ValueKind::Boolean: { auto boolean = reader.u8(); if (boolean > 1) reader.fail("invalid boolean"); value = boolean != 0; break; }
        case ValueKind::Integer: value = signed_bits(reader.u64()); break;
        case ValueKind::Float: { auto bits = reader.u64(); double number; std::memcpy(&number, &bits, 8); value = number; break; }
        case ValueKind::String: value = reader.string(); break;
        default: reader.fail("nonportable constant kind");
        }
        if (module.constants.add(std::move(value)) != n) reader.fail("duplicate canonical constant");
    }
    auto natives = reader.count();
    for (Index n = 0; n < natives; ++n) {
        auto name = reader.string(); auto signature = reader.signature();
        if (!module.natives.emplace(std::move(name), std::move(signature)).second) reader.fail("duplicate native declaration");
    }
    auto functions = reader.count();
    for (Index f = 0; f < functions; ++f) {
        Function fn; fn.name = reader.string(); fn.model = static_cast<VMModel>(reader.u8()); fn.signature = reader.signature();
        fn.register_count = reader.index(); fn.local_count = reader.index(); auto instructions = reader.count();
        if (fn.register_count > limits.items || fn.local_count > limits.items) reader.fail("frame slot budget exceeded");
        for (Index pc = 0; pc < instructions; ++pc) {
            Instruction i; i.opcode = static_cast<Opcode>(reader.u8()); i.destination = reader.index();
            auto operands = reader.count(); for (Index n = 0; n < operands; ++n) i.operands.push_back(reader.index());
            i.index = reader.index(); i.target = reader.index(); i.alternate = reader.index();
            i.argument_count = reader.index(); i.bits = reader.u8(); i.symbol = reader.string(); fn.code.emit(std::move(i));
        }
        module.add_function(std::move(fn));
    }
    if (reader.position != bytes.size()) reader.fail("trailing input");
    auto errors = module.verify(limits.items);
    if (!errors.empty()) throw std::invalid_argument(errors.front().code + ": " + errors.front().message);
    return module;
}

} // namespace firestone::vm
