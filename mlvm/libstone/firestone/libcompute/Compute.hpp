#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace firestone::compute {

enum class BackendKind { CUDA, HIP, OpenCL, SYCL };
class Unavailable : public std::runtime_error { public: using std::runtime_error::runtime_error; };
struct DeviceInfo {
    std::string name;
    std::size_t max_allocation = std::numeric_limits<std::size_t>::max();
    std::size_t max_work_group_size = 1024;
    std::array<std::size_t, 3> max_local_size{{1024, 1024, 64}};
};
struct NDRange {
    std::array<std::size_t, 3> global{{1, 1, 1}};
    std::array<std::size_t, 3> local{{1, 1, 1}};
    void validate(const DeviceInfo& device) const {
        std::size_t group = 1, total = 1;
        for (std::size_t i = 0; i < 3; ++i) {
            if (!global[i] || !local[i] || global[i] % local[i]) throw std::invalid_argument("nonuniform or empty compute range");
            if (local[i] > device.max_local_size[i] || local[i] > device.max_work_group_size / group)
                throw std::out_of_range("compute work group exceeds device limits");
            if (global[i] > std::numeric_limits<std::size_t>::max() / total) throw std::overflow_error("compute range overflow");
            group *= local[i]; total *= global[i];
        }
    }
};
using NativeHandle = std::uintptr_t;
struct NativeArgument {
    enum class Kind { Scalar, Buffer, Local };
    Kind kind = Kind::Scalar;
    std::vector<std::byte> scalar;
    NativeHandle buffer = 0;
    std::size_t bytes = 0;
};

// SDK adapters implement these synchronous operations; release callbacks must not throw.
// Captured state owns the native context and must outlive every returned resource.
struct BackendFunctions {
    DeviceInfo device;
    std::function<NativeHandle(std::size_t)> allocate;
    std::function<void(NativeHandle)> release_buffer;
    std::function<void(NativeHandle, std::size_t, const void*, std::size_t)> write;
    std::function<void(NativeHandle, std::size_t, void*, std::size_t)> read;
    std::function<NativeHandle(const std::string&, const std::string&, const std::vector<std::string>&)> compile;
    std::function<void(NativeHandle)> release_kernel;
    std::function<void(NativeHandle, const NDRange&, const std::vector<NativeArgument>&)> launch;
    std::function<void()> synchronize;
};
namespace detail {
struct Context { BackendKind kind; BackendFunctions functions; };
struct Resource {
    std::shared_ptr<Context> context;
    NativeHandle handle = 0;
    bool kernel = false;
    Resource(std::shared_ptr<Context> owner, bool is_kernel) : context(std::move(owner)), kernel(is_kernel) {}
    ~Resource() noexcept {
        if (handle) {
            try {
                if (kernel) context->functions.release_kernel(handle);
                else context->functions.release_buffer(handle);
            } catch (...) {}
        }
    }
};
inline void check_range(std::size_t size, std::size_t offset, std::size_t count) {
    if (offset > size || count > size - offset) throw std::out_of_range("compute buffer range");
}
} // namespace detail

template<BackendKind Kind> class Backend;
class Argument;
class Buffer {
public:
    Buffer() = default;
    std::size_t size() const noexcept { return bytes_; }
    bool valid() const noexcept { return bool(resource_); }
    NativeHandle native_handle() const noexcept { return resource_ ? resource_->handle : 0; }
    void write(const void* source, std::size_t bytes, std::size_t offset = 0) const {
        check(); detail::check_range(bytes_, offset, bytes);
        if (!bytes) return;
        if (!source) throw std::invalid_argument("null compute upload source");
        resource_->context->functions.write(native_handle(), offset, source, bytes);
    }
    void read(void* destination, std::size_t bytes, std::size_t offset = 0) const {
        check(); detail::check_range(bytes_, offset, bytes);
        if (!bytes) return;
        if (!destination) throw std::invalid_argument("null compute download destination");
        resource_->context->functions.read(native_handle(), offset, destination, bytes);
    }
private:
    template<BackendKind> friend class Backend;
    friend class Argument;
    std::shared_ptr<detail::Resource> resource_;
    std::size_t bytes_ = 0;
    void check() const { if (!resource_) throw std::logic_error("empty compute buffer"); }
};
class Kernel {
public:
    Kernel() = default;
    bool valid() const noexcept { return bool(resource_); }
    NativeHandle native_handle() const noexcept { return resource_ ? resource_->handle : 0; }
    const std::string& entry() const noexcept { return entry_; }
private:
    template<BackendKind> friend class Backend;
    std::shared_ptr<detail::Resource> resource_;
    std::string entry_;
};
class Argument {
public:
    template<class T> static Argument scalar(const T& value) {
        static_assert(std::is_trivially_copyable<T>::value && !std::is_pointer<T>::value, "scalar arguments must be plain values");
        Argument result; result.native_.scalar.resize(sizeof(T)); result.native_.bytes = sizeof(T);
        std::memcpy(result.native_.scalar.data(), &value, sizeof(T)); return result;
    }
    static Argument buffer(const Buffer& value) {
        value.check(); Argument result; result.owner_ = value.resource_;
        result.native_.kind = NativeArgument::Kind::Buffer; result.native_.buffer = value.native_handle(); result.native_.bytes = value.size(); return result;
    }
    static Argument local(std::size_t bytes) {
        if (!bytes) throw std::invalid_argument("empty local compute argument");
        Argument result; result.native_.kind = NativeArgument::Kind::Local; result.native_.bytes = bytes; return result;
    }
private:
    template<BackendKind> friend class Backend;
    NativeArgument native_;
    std::shared_ptr<detail::Resource> owner_;
};

template<BackendKind Kind>
class Backend {
public:
    Backend() = default;
    explicit Backend(BackendFunctions functions) {
        if (!functions.allocate || !functions.release_buffer || !functions.write || !functions.read ||
            !functions.compile || !functions.release_kernel || !functions.launch || !functions.synchronize)
            throw std::invalid_argument("incomplete compute backend callbacks");
        if (!functions.device.max_work_group_size || !functions.device.max_allocation)
            throw std::invalid_argument("invalid compute device limits");
        for (auto limit : functions.device.max_local_size) if (!limit) throw std::invalid_argument("invalid local dimension limit");
        context_ = std::make_shared<detail::Context>(detail::Context{Kind, std::move(functions)});
    }
    static constexpr BackendKind kind = Kind;
    bool available() const noexcept { return bool(context_); }
    const DeviceInfo& device() const { check(); return context_->functions.device; }
    Buffer allocate(std::size_t bytes) const {
        check(); if (!bytes || bytes > device().max_allocation) throw std::out_of_range("compute allocation size");
        Buffer result; result.resource_ = std::make_shared<detail::Resource>(context_, false); result.bytes_ = bytes;
        result.resource_->handle = context_->functions.allocate(bytes);
        if (!result.native_handle()) throw std::runtime_error("compute allocation returned null handle");
        return result;
    }
    Kernel compile(const std::string& source, const std::string& entry, const std::vector<std::string>& options = {}) const {
        check(); if (source.empty() || entry.empty() || entry.find('\0') != std::string::npos) throw std::invalid_argument("empty source or invalid compute entry");
        Kernel result; result.entry_ = entry; result.resource_ = std::make_shared<detail::Resource>(context_, true);
        result.resource_->handle = context_->functions.compile(source, entry, options);
        if (!result.native_handle()) throw std::runtime_error("compute compiler returned null handle");
        return result;
    }
    void launch(const Kernel& kernel, const NDRange& range, const std::vector<Argument>& arguments = {}) const {
        check();
        if (!kernel.resource_ || kernel.resource_->context != context_) throw std::invalid_argument("foreign compute kernel");
        range.validate(device()); std::vector<NativeArgument> native; native.reserve(arguments.size());
        for (const auto& argument : arguments) {
            if (argument.owner_ && argument.owner_->context != context_) throw std::invalid_argument("foreign compute buffer argument");
            native.push_back(argument.native_);
        }
        context_->functions.launch(kernel.native_handle(), range, native);
    }
    void synchronize() const { check(); context_->functions.synchronize(); }
private:
    std::shared_ptr<detail::Context> context_;
    void check() const { if (!available()) throw Unavailable("compute backend requires an SDK adapter"); }
};

} // namespace firestone::compute
