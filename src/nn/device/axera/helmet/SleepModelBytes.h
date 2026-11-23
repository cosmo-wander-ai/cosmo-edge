#include "util/SleepRoutePolicy.h"
#ifndef COSMO_AXERA_SLEEP_ROUTE_CANDIDATE
#error "sleep component requires explicit candidate gate"
#endif
#pragma once
// Private candidate identity. Exact bytes are hashed after making an owned
// copy; matching SHA does not by itself authorize the scene or task graph.
#include <cryptopp/sha.h>

#include <array>
#include <cstdint>
#include <memory>
#include <vector>
namespace cosmo::helmet_candidate {
// Exact sleep candidate identity; model identity alone does not establish independent accuracy.
inline constexpr std::array<std::uint8_t, 32> kSleepCandidateModelHash{
    {0x4d, 0x4e, 0xf4, 0x81, 0x28, 0x84, 0x58, 0xd4, 0xe4, 0x8d, 0xea, 0x32, 0x68, 0x73, 0x5a, 0xbb,
     0xbd, 0xed, 0xd7, 0x2b, 0x38, 0x13, 0x83, 0xef, 0x8c, 0xc3, 0x00, 0x9a, 0x28, 0x42, 0x9d, 0x1c}};
inline std::shared_ptr<const std::vector<std::uint8_t>> VerifySleepModelBytes(const void* data,
                                                                              std::size_t size) {
    if (!data || !size || size > 256u * 1024u * 1024u)
        return {};
    try {
        const auto* first = static_cast<const std::uint8_t*>(data);
        auto owned        = std::make_shared<const std::vector<std::uint8_t>>(first, first + size);
        std::array<std::uint8_t, 32> digest{};
        CryptoPP::SHA256().CalculateDigest(digest.data(), owned->data(), owned->size());
        return digest == kSleepCandidateModelHash ? owned : nullptr;
    } catch (...) {
        return {};
    }
}
}  // namespace cosmo::helmet_candidate
