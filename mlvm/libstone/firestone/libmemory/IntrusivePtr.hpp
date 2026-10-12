#pragma once
#include <atomic>
#include <cstddef>
#include <stdexcept>
#include <utility>

namespace firestone::memory {

class RefCounted {
public:
    void retain() const noexcept { references_.fetch_add(1, std::memory_order_relaxed); }
    void release() const noexcept {
        if (references_.fetch_sub(1, std::memory_order_acq_rel) == 1) delete this;
    }
    std::size_t use_count() const noexcept { return references_.load(std::memory_order_relaxed); }
protected:
    RefCounted() = default;
    RefCounted(const RefCounted&) noexcept {}
    RefCounted& operator=(const RefCounted&) noexcept { return *this; }
    virtual ~RefCounted() = default;
private:
    mutable std::atomic<std::size_t> references_{0};
};

template<class T>
class IntrusivePtr {
    static_assert(std::is_base_of<RefCounted, T>::value, "intrusive pointers require RefCounted");
public:
    IntrusivePtr() = default;
    explicit IntrusivePtr(T* pointer) : pointer_(pointer) { if (pointer_) pointer_->retain(); }
    IntrusivePtr(const IntrusivePtr& other) : IntrusivePtr(other.pointer_) {}
    IntrusivePtr(IntrusivePtr&& other) noexcept : pointer_(std::exchange(other.pointer_, nullptr)) {}
    ~IntrusivePtr() { if (pointer_) pointer_->release(); }
    IntrusivePtr& operator=(IntrusivePtr other) noexcept { swap(other); return *this; }
    void swap(IntrusivePtr& other) noexcept { std::swap(pointer_, other.pointer_); }
    void reset(T* pointer = nullptr) { IntrusivePtr replacement(pointer); swap(replacement); }
    T* get() const noexcept { return pointer_; }
    T& operator*() const { if (!pointer_) throw std::logic_error("null intrusive pointer"); return *pointer_; }
    T* operator->() const { if (!pointer_) throw std::logic_error("null intrusive pointer"); return pointer_; }
    explicit operator bool() const noexcept { return pointer_ != nullptr; }
private:
    T* pointer_ = nullptr;
};

template<class T, class... Args>
IntrusivePtr<T> make_intrusive(Args&&... args) { return IntrusivePtr<T>(new T(std::forward<Args>(args)...)); }

} // namespace firestone::memory
