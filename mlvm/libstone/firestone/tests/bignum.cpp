#include "Test.hpp"
#include <limits>
#include <random>

using namespace firestone::bignum;
int main() { return test_main([] {
    CHECK(BigInt(std::numeric_limits<std::int64_t>::min()).to_int64() == std::numeric_limits<std::int64_t>::min());
    CHECK(BigInt("-0000").to_string() == "0");
    CHECK(BigInt("ff", 16) == BigInt(255));
    CHECK(BigInt("101010", 2).to_string(36) == "16");
    throws<std::invalid_argument>([] { BigInt value("-"); });
    throws<std::invalid_argument>([] { BigInt value("123x"); });
    throws<std::invalid_argument>([] { BigInt value("2", 2); });
    throws<std::overflow_error>([] { (void)BigInt("9223372036854775808").to_int64(); });
    throws<std::domain_error>([] { (void)(BigInt(1) / BigInt()); });
    BigInt a("340282366920938463463374607431768211455");
    CHECK(a + BigInt(1) == (BigInt(1) << 128));
    CHECK((a * a).to_string() == "115792089237316195423570985008687907852589419931798687112530834793049593217025");
    CHECK((a * a) / a == a);
    CHECK((a * a + BigInt(7)) % a == BigInt(7));
    CHECK((BigInt(-7) >> 1) == BigInt(-3));
    CHECK((a << 65) >> 65 == a);
    CHECK((a >> 1000).is_zero());
    CHECK(gcd(BigInt(-48), BigInt(18)) == BigInt(6));
    CHECK(pow(BigInt(2), 256).bit_length() == 257);
    BigInt original(-7), moved(std::move(original));
    CHECK(moved == BigInt(-7) && original == BigInt());
    std::mt19937_64 random(71);
    for (int i = 0; i < 2000; ++i) {
        auto x = static_cast<std::int64_t>(random() % 2000000000) - 1000000000;
        auto y = static_cast<std::int64_t>(random() % 2000000000) - 1000000000;
        BigInt bx(x), by(y);
        CHECK((bx + by).to_int64() == x + y);
        CHECK((bx - by).to_int64() == x - y);
        CHECK((bx * by).to_int64() == x * y);
        if (y) { CHECK((bx / by).to_int64() == x / y); CHECK((bx % by).to_int64() == x % y); }
        auto wide = (bx << 128) + BigInt(static_cast<std::int64_t>(random() >> 1));
        if (y) { auto qr = divmod(wide, by); CHECK(qr.first * by + qr.second == wide); CHECK(qr.second.abs() < by.abs()); }
        CHECK(BigInt(wide.to_string(16), 16) == wide);
    }
    CHECK(BigFloat("0.1") + BigFloat("0.2") == BigFloat("0.3"));
    CHECK(BigFloat("123456789012345678901234567890.123456789") - BigFloat("123456789012345678901234567890") == BigFloat("0.123456789"));
    CHECK(BigFloat("1.245", 3) == BigFloat("1.24"));
    CHECK(BigFloat("1.255", 3) == BigFloat("1.26"));
    CHECK(BigFloat("-1.255", 3) == BigFloat("-1.26"));
    CHECK(BigFloat("9.995", 3).to_string() == "1e1");
    CHECK((BigFloat(1, 5) / BigFloat(3, 5)).to_string() == "3.3333e-1");
    CHECK(BigFloat(1, 2) / BigFloat(8, 2) == BigFloat("0.12"));
    CHECK(BigFloat(3, 2) / BigFloat(8, 2) == BigFloat("0.38"));
    CHECK(BigFloat("1e-20") * BigFloat("1e20") == BigFloat(1));
    CHECK(BigFloat("-12.5e+3").to_string() == "-1.25e4");
    CHECK(BigFloat("2.5").with_precision(1) == BigFloat(2));
    CHECK(BigFloat("3.5").with_precision(1) == BigFloat(4));
    CHECK(BigFloat("-0.0e99").to_string() == "0");
    throws<std::domain_error>([] { (void)(BigFloat(1) / BigFloat()); });
    for (const auto& text : {"", ".", "1e", "1.2.3", "NaN", "1e+", "1 2"})
        throws<std::invalid_argument>([&] { BigFloat value(text); });
    throws<std::out_of_range>([] { BigFloat value("1e100001"); });
    throws<std::invalid_argument>([] { BigFloat value(1, 0); });
    CHECK(Complex128(1, 2) * Complex128(3, 4) == Complex128(-5, 10));
}); }
