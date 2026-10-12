#pragma once
#include <mutex>
#include <shared_mutex>

namespace firestone::concurrency {
using Mutex = std::mutex;
using RecursiveMutex = std::recursive_mutex;
using TimedMutex = std::timed_mutex;
using SharedMutex = std::shared_mutex;
template<class M> using LockGuard = std::lock_guard<M>;
template<class M> using UniqueLock = std::unique_lock<M>;
template<class M> using SharedLock = std::shared_lock<M>;
} // namespace firestone::concurrency
