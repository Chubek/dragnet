#pragma once
#include "Shader.hpp"
namespace firestone::gfx {
class MSL : public ShaderSource {
public:
    MSL(ShaderStage stage, std::string source, std::string entry = "main")
        : ShaderSource(ShaderLanguage::MSL, stage, std::move(source), std::move(entry)) {}
};
} // namespace firestone::gfx
