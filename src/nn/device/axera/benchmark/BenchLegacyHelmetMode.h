#pragma once
#include <cstdlib>
#include <cstring>
namespace cosmo::bench_ax {
inline bool LegacyHelmetSelected() noexcept {
    const char* value = std::getenv("COSMO_AXERA_BENCH_LEGACY_HELMET");
    return value && std::strcmp(value, "1") == 0;
}
}  // namespace cosmo::bench_ax
