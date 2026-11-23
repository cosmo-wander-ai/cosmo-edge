#pragma once
// Private candidate identity. Exact bytes are hashed after making an owned
// copy; matching SHA does not by itself authorize the scene or task graph.
#include <cryptopp/sha.h>

#include <array>
#include <cstdint>
#include <memory>
#include <vector>
namespace cosmo::helmet_candidate {
// Exact U16 candidate identity; model identity alone does not establish independent accuracy.
inline constexpr std::array<std::uint8_t, 32> kFallCandidateModelHash{
    {0x1d, 0xb4, 0x80, 0x52, 0xec, 0x1b, 0xd5, 0x38, 0x4e, 0xe4, 0x91, 0x24, 0x33, 0xf0, 0x05, 0xb3,
     0x6e, 0x27, 0xd3, 0x0f, 0x4c, 0x17, 0xbf, 0x5f, 0xf5, 0xb2, 0x62, 0x71, 0xfa, 0xcb, 0x35, 0x43}};
inline std::shared_ptr<const std::vector<std::uint8_t>> VerifyFallModelBytes(const void* data,
                                                                             std::size_t size) {
    if (!data || !size || size > 256u * 1024u * 1024u)
        return {};
    try {
        const auto* first = static_cast<const std::uint8_t*>(data);
        auto owned        = std::make_shared<const std::vector<std::uint8_t>>(first, first + size);
        std::array<std::uint8_t, 32> digest{};
        CryptoPP::SHA256().CalculateDigest(digest.data(), owned->data(), owned->size());
        return digest == kFallCandidateModelHash ? owned : nullptr;
    } catch (...) {
        return {};
    }
}
}  // namespace cosmo::helmet_candidate
