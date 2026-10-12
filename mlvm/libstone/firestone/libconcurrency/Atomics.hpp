#pragma once
#include <atomic>

namespace firestone::concurrency {
template<class T> using Atomic = std::atomic<T>;
using MemoryOrder = std::memory_order;
inline void thread_fence(MemoryOrder order = std::memory_order_seq_cst) noexcept { std::atomic_thread_fence(order); }
inline void signal_fence(MemoryOrder order = std::memory_order_seq_cst) noexcept { std::atomic_signal_fence(order); }
} // namespace firestone::concurrency
