#pragma once
// Private candidate identity. Exact bytes are hashed after making an owned
// copy; matching SHA does not by itself authorize the scene or task graph.
#include <cryptopp/sha.h>

#include <array>
#include <cstdint>
#include <memory>
#include <vector>
namespace cosmo::helmet_candidate {
// Exact frozen Garment candidate identity; model identity alone does not establish independent accuracy.
inline constexpr std::array<std::uint8_t, 32> kGarmentCandidateModelHash{
    {0xa9, 0xb6, 0x5a, 0xde, 0xd7, 0xf2, 0x11, 0xb0, 0xf0, 0x4d, 0x28, 0x8f, 0x60, 0xad, 0x25, 0x7f,
     0x4c, 0x35, 0xc2, 0x2a, 0x56, 0xa3, 0x96, 0xe6, 0xc8, 0x7c, 0x0d, 0xf6, 0xda, 0xae, 0xdd, 0x09}};
inline std::shared_ptr<const std::vector<std::uint8_t>> VerifyGarmentModelBytes(const void* data,
                                                                                std::size_t size) {
    if (!data || !size || size > 256u * 1024u * 1024u)
        return {};
    try {
        const auto* first = static_cast<const std::uint8_t*>(data);
        auto owned        = std::make_shared<const std::vector<std::uint8_t>>(first, first + size);
        std::array<std::uint8_t, 32> digest{};
        CryptoPP::SHA256().CalculateDigest(digest.data(), owned->data(), owned->size());
        return digest == kGarmentCandidateModelHash ? owned : nullptr;
    } catch (...) {
        return {};
    }
}
}  // namespace cosmo::helmet_candidate
