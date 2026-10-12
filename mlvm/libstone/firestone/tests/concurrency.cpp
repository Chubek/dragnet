#include "Test.hpp"
#include <chrono>
#include <future>
#include <memory>
#include <vector>

using namespace firestone::concurrency;
int main() { return test_main([] {
    Atomic<int> count{0}; Spinlock spin; int protected_count = 0;
    std::vector<Thread> workers;
    for (int i = 0; i < 4; ++i) workers.emplace_back([&] {
        for (int j = 0; j < 2000; ++j) { ++count; LockGuard<Spinlock> lock(spin); ++protected_count; }
    });
    for (auto& worker : workers) worker.join();
    CHECK(count == 8000 && protected_count == 8000);
    Thread failed([] { throw std::runtime_error("worker failure"); });
    throws<std::runtime_error>([&] { failed.join(); });
    CHECK(!failed.joinable());
    Semaphore semaphore(0, 2);
    CHECK(!semaphore.try_acquire()); CHECK(!semaphore.try_acquire_for(std::chrono::milliseconds(1)));
    semaphore.release(2); throws<std::overflow_error>([&] { semaphore.release(); });
    semaphore.acquire(); semaphore.acquire();
    { Thread release([&] { semaphore.release(); }); semaphore.acquire(); }
    Channel<std::unique_ptr<int>> channel(3); Atomic<int> sum{0};
    Thread producer([&] { for (int i = 1; i <= 1000; ++i) CHECK(channel.send(std::make_unique<int>(i))); channel.close(); });
    Thread consumer([&] { while (auto value = channel.receive()) sum += **value; });
    producer.join(); consumer.join(); CHECK(sum == 500500); CHECK(channel.closed());
    CHECK(!channel.send(std::make_unique<int>(1)) && !channel.receive());
    Channel<int> full(1); CHECK(full.try_send(7)); CHECK(!full.try_send(8));
    std::promise<void> starting;
    auto waiting = std::async(std::launch::async, [&] { starting.set_value(); return full.send(9); });
    starting.get_future().wait(); full.close(); CHECK(!waiting.get());
    CHECK(*full.receive() == 7); CHECK(!full.receive());
    Channel<int> empty;
    auto reader = std::async(std::launch::async, [&] { return empty.receive(); });
    empty.close(); CHECK(!reader.get());
    Channel<int> mpmc(4); Atomic<int> total{0}; workers.clear();
    for (int i = 0; i < 3; ++i) workers.emplace_back([&] { for (int j = 0; j < 500; ++j) CHECK(mpmc.send(1)); });
    Thread r1([&] { while (auto value = mpmc.receive()) total += *value; });
    Thread r2([&] { while (auto value = mpmc.receive()) total += *value; });
    for (auto& worker : workers) worker.join();
    mpmc.close(); r1.join(); r2.join(); CHECK(total == 1500);
    int steps = 0; Fiber fiber([&] { return ++steps == 3 ? FiberStep::Complete : FiberStep::Yield; });
    CHECK(fiber.resume() == FiberState::Suspended); fiber.resume(); CHECK(fiber.resume() == FiberState::Complete);
    fiber.resume(); CHECK(steps == 3);
    Fiber failure([]() -> FiberStep { throw std::runtime_error("fiber failure"); });
    throws<std::runtime_error>([&] { failure.resume(); }); CHECK(failure.state() == FiberState::Failed);
    GreenThread scheduler; std::vector<int> order; int iteration = 0;
    scheduler.spawn([&] { order.push_back(1); return ++iteration == 2 ? FiberStep::Complete : FiberStep::Yield; });
    scheduler.spawn([&] { order.push_back(2); scheduler.spawn([&] { order.push_back(3); return FiberStep::Complete; }); return FiberStep::Complete; });
    CHECK(scheduler.run(2) == 2 && scheduler.pending() == 2);
    scheduler.run(); CHECK(order == std::vector<int>({1, 2, 1, 3})); CHECK(!scheduler.pending());
    auto cancelled = scheduler.spawn([] { throw std::runtime_error("cancelled task ran"); return FiberStep::Complete; });
    CHECK(scheduler.cancel(cancelled)); scheduler.run(); CHECK(!scheduler.pending());
    scheduler.spawn([]() -> FiberStep { throw std::runtime_error("failed task"); });
    throws<std::runtime_error>([&] { scheduler.run(); }); CHECK(!scheduler.pending());
    if (CThreads::available()) {
        CThreads thread([] { return 42; }); CHECK(thread.join() == 42);
        CThreads error([]() -> int { throw std::runtime_error("C thread failure"); });
        throws<std::runtime_error>([&] { error.join(); });
    }
    if (PosixThread::available()) { PosixThread thread([&] { ++count; }); thread.join(); CHECK(count == 8001); }
    if (LinuxProcess::available()) {
        auto child = LinuxProcess::spawn({"/bin/sh", "-c", "exit 7"}); CHECK(child.wait().exit_code == 7); CHECK(child.wait().exit_code == 7);
        auto good = LinuxProcess::spawn({"/bin/true"}); CHECK(good.wait().success());
        throws<std::system_error>([] { (void)LinuxProcess::spawn({"/nonexistent/firestone-test-program"}); });
        CHECK(!LinuxSched::affinity().empty());
        throws<std::invalid_argument>([] { LinuxSched::set_affinity({}); });
    }
}); }
