#pragma once
// Private candidate identity. Exact bytes are hashed after making an owned
// copy; matching SHA does not by itself authorize the scene or task graph.
#include <cryptopp/sha.h>

#include <array>
#include <cstdint>
#include <memory>
#include <vector>
namespace cosmo::helmet_candidate {
// Exact frozen Electric candidate identity; model identity alone does not establish independent accuracy.
inline constexpr std::array<std::uint8_t, 32> kElectricCandidateModelHash{
    {0xb7, 0xed, 0xfa, 0x50, 0x41, 0xed, 0x7d, 0xfb, 0xd0, 0xf5, 0xd7, 0xa6, 0xe7, 0xcc, 0x03, 0x74,
     0x01, 0x9b, 0xe9, 0xe8, 0xb6, 0x84, 0xc0, 0xcc, 0x95, 0x0c, 0x9f, 0xe0, 0xd5, 0x2d, 0x55, 0x44}};
inline std::shared_ptr<const std::vector<std::uint8_t>> VerifyElectricModelBytes(const void* data,
                                                                                 std::size_t size) {
    if (!data || !size || size > 256u * 1024u * 1024u)
        return {};
    try {
        const auto* first = static_cast<const std::uint8_t*>(data);
        auto owned        = std::make_shared<const std::vector<std::uint8_t>>(first, first + size);
        std::array<std::uint8_t, 32> digest{};
        CryptoPP::SHA256().CalculateDigest(digest.data(), owned->data(), owned->size());
        return digest == kElectricCandidateModelHash ? owned : nullptr;
    } catch (...) {
        return {};
    }
}
}  // namespace cosmo::helmet_candidate
