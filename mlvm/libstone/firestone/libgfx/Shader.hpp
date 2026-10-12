#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace firestone::gfx {
enum class ShaderLanguage { GLSL, HLSL, MSL, WGSL };
enum class ShaderStage { Vertex, Fragment, Compute, Geometry, TessControl, TessEvaluation };
inline bool shader_identifier(const std::string& text) noexcept {
    auto letter = [](char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; };
    if (text.empty() || !letter(text.front())) return false;
    for (char c : text) if (!letter(c) && !(c >= '0' && c <= '9')) return false;
    return true;
}

class ShaderSource {
public:
    ShaderSource(ShaderLanguage language, ShaderStage stage, std::string source, std::string entry = "main")
        : language_(language), stage_(stage), source_(std::move(source)), entry_(std::move(entry)) {
        if (source_.empty() || source_.find('\0') != std::string::npos) throw std::invalid_argument("empty shader source or embedded NUL");
        if (!shader_identifier(entry_)) throw std::invalid_argument("invalid shader entry point");
        switch (stage) {
        case ShaderStage::Vertex: case ShaderStage::Fragment: case ShaderStage::Compute:
        case ShaderStage::Geometry: case ShaderStage::TessControl: case ShaderStage::TessEvaluation: break;
        default: throw std::invalid_argument("invalid shader stage");
        }
        switch (language) {
        case ShaderLanguage::GLSL: case ShaderLanguage::HLSL: break;
        case ShaderLanguage::MSL: case ShaderLanguage::WGSL:
            if (stage != ShaderStage::Vertex && stage != ShaderStage::Fragment && stage != ShaderStage::Compute)
                throw std::invalid_argument("unsupported shader stage for language");
            break;
        default: throw std::invalid_argument("invalid shader language");
        }
    }
    ShaderLanguage language() const noexcept { return language_; }
    ShaderStage stage() const noexcept { return stage_; }
    const std::string& source() const noexcept { return source_; }
    const std::string& entry() const noexcept { return entry_; }
private:
    ShaderLanguage language_;
    ShaderStage stage_;
    std::string source_, entry_;
};

enum class ShaderBinaryFormat { SPIRV, DXIL, MetalLibrary, Native };
struct ShaderBinary {
    ShaderBinaryFormat format = ShaderBinaryFormat::Native;
    std::vector<std::byte> bytes;
    std::string diagnostics;
};
class ShaderCompiler {
public:
    using Compile = std::function<ShaderBinary(const ShaderSource&)>;
    explicit ShaderCompiler(Compile compile) : compile_(std::move(compile)) {
        if (!compile_) throw std::invalid_argument("missing shader compiler callback");
    }
    ShaderBinary compile(const ShaderSource& source) const {
        auto result = compile_(source);
        if (result.bytes.empty()) throw std::runtime_error("shader compiler returned an empty binary: " + result.diagnostics);
        return result;
    }
private:
    Compile compile_;
};
} // namespace firestone::gfx
