#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

#include "nn/core/shared_resource.h"

namespace cosmo::nn {

inline bool UseCpuRgaBoundPadding(std::string_view chip) {
    // RK3588 color fill selects RGA2, whose physical addressing is limited to
    // 4 GiB. RGB resize/CSC can instead use RGA3 with the existing bound buffer.
    return chip == "rk3588";
}

// The caller exclusively owns the graph and holds the provider's context lock.
// sync(true) invalidates for CPU access; sync(false) flushes before RGA access.
// Fill every frame: the preceding inference changed padding to native INT8.
template <typename Sync>
bool FillRknnBoundRgbPadding(RknnBoundInputTarget& target, RknnBoundInputProvider* owner, uint64_t generation,
                             const std::array<uint8_t, 3>& color, Sync&& sync, std::string& reason) {
    const auto reject = [&](const char* message) {
        reason = message;
        return false;
    };
    if (!owner || target.owner != owner)
        return reject("RGB padding target belongs to another provider");
    target.frame_ready = false;
    if (generation == 0 || target.generation != generation)
        return reject("RGB padding target generation changed");
    if (!target.Matches(target.height, target.width))
        return reject("RGB padding target has an invalid shape, stride or allocation");
    const size_t bytes = static_cast<size_t>(target.height) * target.width_stride * 3;
    if (!sync(true, reason))
        return false;
    auto* pixels = static_cast<uint8_t*>(target.virtual_address);
    if (color[0] == color[1] && color[1] == color[2]) {
        std::memset(pixels, color[0], bytes);
    } else {
        for (size_t offset = 0; offset < bytes; offset += 3) {
            pixels[offset]     = color[0];
            pixels[offset + 1] = color[1];
            pixels[offset + 2] = color[2];
        }
    }
    if (!sync(false, reason))
        return false;
    reason.clear();
    // Only successful RGA resize/CSC may mark this frame ready for inference.
    return true;
}

}  // namespace cosmo::nn
