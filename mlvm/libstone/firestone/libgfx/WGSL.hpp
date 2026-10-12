#pragma once
#include "Shader.hpp"
namespace firestone::gfx {
class WGSL : public ShaderSource {
public:
    WGSL(ShaderStage stage, std::string source, std::string entry = "main")
        : ShaderSource(ShaderLanguage::WGSL, stage, std::move(source), std::move(entry)) {}
};
} // namespace firestone::gfx
