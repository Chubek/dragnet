#include "Test.hpp"
#include <array>
#include <deque>
#include <memory>

using namespace firestone::gfx;
template<class Display> void exercise_display() {
    CHECK(!Display().available());
    throws<std::runtime_error>([] { Display().create({2, 2}); });
    auto destroyed = std::make_shared<int>(0); int frames = 0;
    std::deque<DisplayEvent> events;
    events.push_back({DisplayEvent::Kind::Resize, {1, 1}, 0, false, 0, 0});
    events.push_back({DisplayEvent::Kind::Close, {}, 0, false, 0, 0});
    Surface survivor;
    {
        DisplayFunctions functions;
        functions.create = [](Extent extent, const std::string& title) { CHECK(extent.width == 2 && title == "test"); return SurfaceHandle(42); };
        functions.destroy = [destroyed](SurfaceHandle) { ++*destroyed; };
        functions.present = [&](SurfaceHandle handle, const Frame& frame) { CHECK(handle == 42 && frame.data[0] == std::byte{255}); ++frames; };
        functions.poll = [&](SurfaceHandle) -> std::optional<DisplayEvent> {
            if (events.empty()) return std::nullopt;
            auto event = events.front(); events.pop_front(); return event;
        };
        Display display(functions); auto surface = display.create({2, 2}, "test");
        std::array<std::byte, 16> pixels{}; pixels[0] = std::byte{255};
        Frame frame{pixels.data(), pixels.size(), {2, 2}, 8, PixelFormat::RGBA8};
        surface.present(frame); CHECK(frames == 1);
        frame.bytes = 15; throws<std::invalid_argument>([&] { surface.present(frame); });
        frame.bytes = 16; CHECK(surface.poll()->kind == DisplayEvent::Kind::Resize);
        CHECK(surface.extent().width == 1);
        throws<std::invalid_argument>([&] { surface.present(frame); });
        frame.extent = {1, 1}; frame.stride = 4; surface.present(frame); CHECK(frames == 2);
        CHECK(surface.poll()->kind == DisplayEvent::Kind::Close); CHECK(!surface.poll());
        survivor = std::move(surface); CHECK(!surface.valid());
        throws<std::logic_error>([&] { surface.poll(); });
        throws<std::invalid_argument>([&] { display.create({0, 1}); });
        functions.create = [](Extent, const std::string&) { return SurfaceHandle(0); };
        throws<std::runtime_error>([&] { Display(functions).create({2, 2}); });
    }
    CHECK(*destroyed == 0); survivor = {}; CHECK(*destroyed == 1);
}
int main() { return test_main([] {
    GLSL glsl(ShaderStage::Vertex, "void main() { gl_Position = vec4(0.0); }");
    CHECK(glsl.source().find("#version 450 core\n") == 0);
    throws<std::invalid_argument>([] { GLSL source(ShaderStage::Compute, "void main() {}", 330); });
    throws<std::invalid_argument>([] { GLSL source(ShaderStage::Vertex, "#version 450\nvoid main() {}"); });
    HLSL hlsl(ShaderStage::Fragment, "float4 main() : SV_Target { return 1; }"); CHECK(hlsl.profile() == "ps_6_0");
    WGSL wgsl(ShaderStage::Compute, "@compute @workgroup_size(1) fn run() {}", "run"); CHECK(wgsl.entry() == "run");
    MSL msl(ShaderStage::Compute, "kernel void run() {}", "run"); CHECK(msl.language() == ShaderLanguage::MSL);
    throws<std::invalid_argument>([] { WGSL source(ShaderStage::Geometry, "invalid"); });
    throws<std::invalid_argument>([] { MSL source(ShaderStage::Compute, "kernel void run() {}", "bad entry"); });
    ShaderCompiler compiler([](const ShaderSource& source) { CHECK(source.language() == ShaderLanguage::GLSL); return ShaderBinary{ShaderBinaryFormat::Native, {std::byte{1}}, {}}; });
    CHECK(compiler.compile(glsl).bytes.size() == 1);
    ShaderCompiler empty([](const ShaderSource&) { return ShaderBinary{}; });
    throws<std::runtime_error>([&] { empty.compile(glsl); });
    SPIRV module(6, 123); CHECK(module.allocate_id() == 1 && module.bound() == 2);
    module.append(17, {1}); // OpCapability Shader
    module.append(14, {0, 1}); // OpMemoryModel Logical GLSL450
    auto bytes = module.to_bytes(); auto loaded = SPIRV::from_bytes(bytes);
    CHECK(loaded.words() == module.words() && loaded.minor_version() == 6);
    CHECK(loaded.instructions().size() == 2 && loaded.instructions()[1].operands == std::vector<std::uint32_t>({0, 1}));
    for (std::size_t i = 0; i < bytes.size(); i += 4) { std::swap(bytes[i], bytes[i + 3]); std::swap(bytes[i + 1], bytes[i + 2]); }
    CHECK(SPIRV::from_bytes(bytes).words() == module.words());
    bytes.pop_back(); throws<std::invalid_argument>([&] { SPIRV::from_bytes(bytes); });
    auto words = module.words(); words[5] = 17; throws<std::invalid_argument>([&] { SPIRV::from_words(words); });
    words = module.words(); words[5] = 0xffff0011; throws<std::invalid_argument>([&] { SPIRV::from_words(words); });
    words = module.words(); words[4] = 1; throws<std::invalid_argument>([&] { SPIRV::from_words(words); });
    words = module.words(); words[1] = 0x00010700; throws<std::invalid_argument>([&] { SPIRV::from_words(words); });
    throws<std::invalid_argument>([] { SPIRV::from_words({}); });
    exercise_display<Wayland>(); exercise_display<X11>(); exercise_display<DRM>();
}); }
