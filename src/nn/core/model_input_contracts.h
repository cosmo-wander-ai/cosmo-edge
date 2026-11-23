#pragma once

namespace cosmo::nn {

// Explicitly asserts that a compiled model consumes RGB uint8 NHWC pixels
// and incorporates conversion from [0,255] to [0,1]. This must come from the
// model's verified conversion configuration, not from tensor dtype alone.
inline constexpr char kAxeraRgbUint8InputContract[] = "cosmo.axera.input.rgb_u8_nhwc_0_255_to_0_1.v1";

}  // namespace cosmo::nn
