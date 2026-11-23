#pragma once
// Private candidate identity. Exact bytes are hashed after making an owned
// copy; matching SHA does not by itself authorize the scene or task graph.
#include <cryptopp/sha.h>

#include <array>
#include <cstdint>
#include <memory>
#include <vector>
namespace cosmo::helmet_candidate {
// Exact frozen Mask candidate identity; model identity alone does not establish independent accuracy.
inline constexpr std::array<std::uint8_t, 32> kMaskCandidateModelHash{
    {0xc4, 0xd0, 0x76, 0x01, 0xbe, 0xd8, 0x07, 0xee, 0x76, 0xe3, 0xa4, 0xce, 0x91, 0x40, 0x3e, 0x2e,
     0x80, 0x6c, 0x6d, 0x46, 0x6e, 0x72, 0xb9, 0xca, 0x8f, 0x1f, 0x61, 0xd9, 0x7b, 0x8a, 0xba, 0xb8}};
inline std::shared_ptr<const std::vector<std::uint8_t>> VerifyMaskModelBytes(const void* data,
                                                                             std::size_t size) {
    if (!data || !size || size > 256u * 1024u * 1024u)
        return {};
    try {
        const auto* first = static_cast<const std::uint8_t*>(data);
        auto owned        = std::make_shared<const std::vector<std::uint8_t>>(first, first + size);
        std::array<std::uint8_t, 32> digest{};
        CryptoPP::SHA256().CalculateDigest(digest.data(), owned->data(), owned->size());
        return digest == kMaskCandidateModelHash ? owned : nullptr;
    } catch (...) {
        return {};
    }
}
}  // namespace cosmo::helmet_candidate
