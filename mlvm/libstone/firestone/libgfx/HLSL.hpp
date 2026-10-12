#pragma once
#include "Shader.hpp"
namespace firestone::gfx {
class HLSL : public ShaderSource {
public:
    HLSL(ShaderStage stage, std::string source, std::string entry = "main")
        : ShaderSource(ShaderLanguage::HLSL, stage, std::move(source), std::move(entry)) {}
    std::string profile(unsigned major = 6, unsigned minor = 0) const {
        if ((major != 5 && major != 6) || (major == 5 && minor > 1) || (major == 6 && minor > 9))
            throw std::invalid_argument("unsupported HLSL shader model");
        const char* prefix = "";
        switch (stage()) {
        case ShaderStage::Vertex: prefix = "vs"; break;
        case ShaderStage::Fragment: prefix = "ps"; break;
        case ShaderStage::Compute: prefix = "cs"; break;
        case ShaderStage::Geometry: prefix = "gs"; break;
        case ShaderStage::TessControl: prefix = "hs"; break;
        case ShaderStage::TessEvaluation: prefix = "ds"; break;
        default: throw std::invalid_argument("unsupported HLSL stage");
        }
        return std::string(prefix) + "_" + std::to_string(major) + "_" + std::to_string(minor);
    }
};
} // namespace firestone::gfx
