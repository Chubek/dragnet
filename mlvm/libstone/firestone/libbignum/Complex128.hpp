#pragma once
#include <complex>
#include <limits>

namespace firestone::bignum {

// Two binary64 components (128 bits total), following std::complex semantics.
static_assert(sizeof(double) == 8 && std::numeric_limits<double>::is_iec559,
    "Complex128 requires IEEE-754 binary64 double");
using Complex128 = std::complex<double>;

} // namespace firestone::bignum
