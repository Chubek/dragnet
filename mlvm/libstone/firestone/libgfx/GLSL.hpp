#pragma once
#include "Shader.hpp"
namespace firestone::gfx {
class GLSL : public ShaderSource {
public:
    // Source is the body; this constructor supplies the desktop core version directive.
    GLSL(ShaderStage stage, std::string body, unsigned version = 450)
        : ShaderSource(ShaderLanguage::GLSL, stage, prepare(stage, std::move(body), version)) {}
private:
    static std::string prepare(ShaderStage stage, std::string body, unsigned version) {
        if (version != 330 && version != 400 && version != 410 && version != 420 && version != 430 && version != 440 && version != 450 && version != 460)
            throw std::invalid_argument("unsupported desktop GLSL version");
        if (stage == ShaderStage::Compute && version < 430) throw std::invalid_argument("GLSL compute requires version 430");
        if ((stage == ShaderStage::TessControl || stage == ShaderStage::TessEvaluation) && version < 400)
            throw std::invalid_argument("GLSL tessellation requires version 400");
        if (body.empty() || body.find("#version") != std::string::npos) throw std::invalid_argument("GLSL body must omit version directive");
        return "#version " + std::to_string(version) + " core\n" + body;
    }
};
} // namespace firestone::gfx
