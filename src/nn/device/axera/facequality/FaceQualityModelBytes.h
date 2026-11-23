#pragma once
// Private candidate identity. Exact bytes are hashed after making an owned
// copy; matching SHA does not by itself authorize the scene or task graph.
#include <cryptopp/sha.h>

#include <array>
#include <cstdint>
#include <memory>
#include <vector>
namespace cosmo::helmet_candidate {
// Exact frozen FaceQuality candidate identity; model identity alone does not establish independent accuracy.
inline constexpr std::array<std::uint8_t, 32> kFaceQualityCandidateModelHash{
    {0x5f, 0x68, 0x77, 0x0b, 0x23, 0x96, 0x0f, 0x70, 0x47, 0x51, 0x61, 0x9f, 0x4b, 0x5b, 0x2b, 0x37,
     0x83, 0x97, 0x06, 0xe5, 0x4d, 0x75, 0x99, 0xaa, 0x94, 0xe8, 0xe2, 0xc3, 0x52, 0x55, 0x84, 0x07}};
inline std::shared_ptr<const std::vector<std::uint8_t>> VerifyFaceQualityModelBytes(const void* data,
                                                                                    std::size_t size) {
    if (!data || !size || size > 256u * 1024u * 1024u)
        return {};
    try {
        const auto* first = static_cast<const std::uint8_t*>(data);
        auto owned        = std::make_shared<const std::vector<std::uint8_t>>(first, first + size);
        std::array<std::uint8_t, 32> digest{};
        CryptoPP::SHA256().CalculateDigest(digest.data(), owned->data(), owned->size());
        return digest == kFaceQualityCandidateModelHash ? owned : nullptr;
    } catch (...) {
        return {};
    }
}
}  // namespace cosmo::helmet_candidate
