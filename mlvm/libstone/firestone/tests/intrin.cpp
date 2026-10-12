#include "Test.hpp"
#include <cmath>
#include <limits>

using namespace firestone::intrin;
int main() { return test_main([] {
    AVX2::Pack<float> a(2.0f), b(3.0f);
    CHECK((a * b).reduce_add() == 48.0f);
    CHECK((a < b).all()); CHECK((a == b).count() == 0);
    CHECK((select(a < b, a, b) == a).all());
    CHECK((fma(a, b, a) == AVX2::Pack<float>(8)).all());
    Vector<std::int32_t, 2> maximum(std::numeric_limits<std::int32_t>::max());
    CHECK((maximum + Vector<std::int32_t, 2>(1))[0] == std::numeric_limits<std::int32_t>::min());
    CHECK((Vector<std::int64_t, 1>(std::numeric_limits<std::int64_t>::min()) - Vector<std::int64_t, 1>(1))[0] == std::numeric_limits<std::int64_t>::max());
    CHECK((Vector<std::uint16_t, 2>(65535) * Vector<std::uint16_t, 2>(65535))[0] == 1);
    CHECK((Vector<std::int8_t, 2>(127) * Vector<std::int8_t, 2>(2))[0] == -2);
    Vector<int, 4> lanes{1, 2, 3, 4};
    CHECK(lanes.shuffle({3, 2, 1, 0})[0] == 4);
    throws<std::out_of_range>([&] { (void)lanes.shuffle({4, 0, 0, 0}); });
    throws<std::invalid_argument>([] { Vector<int, 4> bad{1, 2}; });
    throws<std::invalid_argument>([] { (void)Vector<int, 4>::load(nullptr); });
    throws<std::domain_error>([&] { (void)(lanes / Vector<int, 4>(0)); });
    throws<std::overflow_error>([] { (void)(Vector<int, 1>(std::numeric_limits<int>::min()) / Vector<int, 1>(-1)); });
    int storage[4]{}; lanes.store(storage); CHECK((Vector<int, 4>::load(storage) == lanes).all());
    throws<std::invalid_argument>([&] { lanes.store(storage, 3); });
    auto nan = std::numeric_limits<double>::quiet_NaN();
    CHECK(!(Vector<double, 1>(nan) <= Vector<double, 1>(0)).any());
    CHECK(supported(InstructionSet::Scalar));
    (void)SSE4::available(); (void)AVX2::available(); (void)AVX512::available(); (void)NEON::available();
    auto before = std::fegetround();
    { ScopedRounding rounding(RoundingMode::Downward); CHECK(std::fegetround() == FE_DOWNWARD); }
    CHECK(std::fegetround() == before);
    CHECK(FPU::square_root(9.0) == 3.0); CHECK(FPU::fused_multiply_add(2.0, 3.0, 4.0) == 10.0);
}); }
