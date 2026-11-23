#include <cassert>
#include <cstdlib>

#include "nn/core/axera_strict_mode.h"
int main() {
#if defined(COSMO_NN_USE_AXERA_BACKEND)
    constexpr bool expected = true;
#else
    constexpr bool expected = false;
#endif
    unsetenv("COSMO_AXERA_STRICT_ZERO_COPY");
    assert(cosmo::nn::AxeraStrictZeroCopyEnabled() == expected);
    setenv("COSMO_AXERA_STRICT_ZERO_COPY", "0", 1);
    assert(cosmo::nn::AxeraStrictZeroCopyEnabled() == expected);
    setenv("COSMO_AXERA_STRICT_ZERO_COPY", "1", 1);
    assert(cosmo::nn::AxeraStrictZeroCopyEnabled() == expected);
    assert(!cosmo::nn::AxeraStrictIngressVerified());
}
