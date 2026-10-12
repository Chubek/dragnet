#include <libbignum/BigInt.hpp>
#include <libcompute/OpenMP.hpp>
#include <libconcurrency/Thread.hpp>
#include <libconcurrency/Channel.hpp>
#include <libgfx/SPIR-V.hpp>
#include <libintrin/AVX2.hpp>

int main() {
    firestone::concurrency::Channel<int> channel;
    firestone::concurrency::Thread worker([&] { channel.send(42); channel.close(); });
    if (channel.receive() != 42) return 1;
    worker.join();
    auto value = firestone::bignum::pow(firestone::bignum::BigInt(2), 100);
    if (value.to_string() != "1267650600228229401496703205376") return 1;
    firestone::intrin::AVX2::Pack<float> lanes(2.0f);
    if (lanes.reduce_add() != 16.0f) return 1;
    firestone::gfx::SPIRV module;
    if (firestone::gfx::SPIRV::from_bytes(module.to_bytes()).bound() != 1) return 1;
    int sum = firestone::compute::OpenMP::parallel_reduce(4, 0, [](std::size_t i) { return int(i); }, [](int a, int b) { return a + b; });
    return sum == 6 ? 0 : 1;
}
