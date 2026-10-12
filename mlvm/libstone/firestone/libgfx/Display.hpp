#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

namespace firestone::gfx {
enum class DisplayKind { Wayland, X11, DRM };
enum class PixelFormat { RGBA8, BGRA8 };
struct Extent {
    std::size_t width = 0, height = 0;
    void validate() const {
        if (!width || !height || width > std::numeric_limits<std::size_t>::max() / 4 || height > std::numeric_limits<std::size_t>::max() / (width * 4))
            throw std::invalid_argument("invalid display extent");
    }
};
struct Frame {
    const std::byte* data = nullptr;
    std::size_t bytes = 0;
    Extent extent;
    std::size_t stride = 0;
    PixelFormat format = PixelFormat::RGBA8;
    void validate() const {
        extent.validate();
        if (format != PixelFormat::RGBA8 && format != PixelFormat::BGRA8) throw std::invalid_argument("unsupported pixel format");
        if (!data || stride < extent.width * 4 || extent.height > std::numeric_limits<std::size_t>::max() / stride || bytes < extent.height * stride)
            throw std::invalid_argument("invalid display frame storage");
    }
};
struct DisplayEvent {
    enum class Kind { Close, Resize, Key, Pointer };
    Kind kind = Kind::Close;
    Extent extent;
    std::uint32_t key = 0;
    bool pressed = false;
    double x = 0, y = 0;
};
using SurfaceHandle = std::uintptr_t;
// Adapters own native connections in captured state. Present consumes pixels synchronously.
struct DisplayFunctions {
    std::function<SurfaceHandle(Extent, const std::string&)> create;
    std::function<void(SurfaceHandle)> destroy; // Must not throw.
    std::function<void(SurfaceHandle, const Frame&)> present;
    std::function<std::optional<DisplayEvent>(SurfaceHandle)> poll;
};
namespace detail {
struct SurfaceResource {
    std::shared_ptr<DisplayFunctions> functions;
    SurfaceHandle handle = 0;
    ~SurfaceResource() noexcept { if (handle) { try { functions->destroy(handle); } catch (...) {} } }
};
} // namespace detail
template<DisplayKind Kind> class DisplayBackend;

class Surface {
public:
    Surface() = default;
    Surface(const Surface&) = delete;
    Surface& operator=(const Surface&) = delete;
    Surface(Surface&&) noexcept = default;
    Surface& operator=(Surface&&) noexcept = default;
    bool valid() const noexcept { return bool(resource_); }
    SurfaceHandle native_handle() const noexcept { return resource_ ? resource_->handle : 0; }
    Extent extent() const noexcept { return extent_; }
    void present(const Frame& frame) const {
        check(); frame.validate();
        if (frame.extent.width != extent_.width || frame.extent.height != extent_.height) throw std::invalid_argument("frame/surface size mismatch");
        resource_->functions->present(native_handle(), frame);
    }
    std::optional<DisplayEvent> poll() {
        check(); auto event = resource_->functions->poll(native_handle());
        if (event && event->kind == DisplayEvent::Kind::Resize) { event->extent.validate(); extent_ = event->extent; }
        return event;
    }
private:
    template<DisplayKind> friend class DisplayBackend;
    std::unique_ptr<detail::SurfaceResource> resource_;
    Extent extent_;
    void check() const { if (!resource_) throw std::logic_error("empty display surface"); }
};

template<DisplayKind Kind>
class DisplayBackend {
public:
    DisplayBackend() = default;
    explicit DisplayBackend(DisplayFunctions functions) {
        if (!functions.create || !functions.destroy || !functions.present || !functions.poll)
            throw std::invalid_argument("incomplete display callbacks");
        functions_ = std::make_shared<DisplayFunctions>(std::move(functions));
    }
    static constexpr DisplayKind kind = Kind;
    bool available() const noexcept { return bool(functions_); }
    Surface create(Extent extent, const std::string& title = {}) const {
        if (!available()) throw std::runtime_error("display backend requires a native adapter");
        extent.validate();
        if (title.find('\0') != std::string::npos) throw std::invalid_argument("NUL in surface title");
        Surface result; result.extent_ = extent; result.resource_ = std::make_unique<detail::SurfaceResource>();
        result.resource_->functions = functions_; result.resource_->handle = functions_->create(extent, title);
        if (!result.native_handle()) throw std::runtime_error("display adapter returned null surface");
        return result;
    }
private:
    std::shared_ptr<DisplayFunctions> functions_;
};
} // namespace firestone::gfx
