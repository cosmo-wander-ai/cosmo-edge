#pragma once
// Private candidate identity. Exact bytes are hashed after making an owned
// copy; matching SHA does not by itself authorize the scene or task graph.
#include <cryptopp/sha.h>

#include <array>
#include <cstdint>
#include <memory>
#include <vector>
namespace cosmo::helmet_candidate {
// Exact frozen HsHelmet candidate identity; model identity alone does not establish independent accuracy.
inline constexpr std::array<std::uint8_t, 32> kHsHelmetCandidateModelHash{
    {0xca, 0xcb, 0xcc, 0xd6, 0xd8, 0xb7, 0x0c, 0xbd, 0xe7, 0x53, 0x5b, 0x33, 0xd2, 0xc5, 0xe7, 0xed,
     0xba, 0x88, 0xd2, 0xef, 0x66, 0x4d, 0x24, 0x11, 0x02, 0x70, 0x01, 0xe7, 0xc8, 0x9d, 0xca, 0x94}};
inline std::shared_ptr<const std::vector<std::uint8_t>> VerifyHsHelmetModelBytes(const void* data,
                                                                                 std::size_t size) {
    if (!data || !size || size > 256u * 1024u * 1024u)
        return {};
    try {
        const auto* first = static_cast<const std::uint8_t*>(data);
        auto owned        = std::make_shared<const std::vector<std::uint8_t>>(first, first + size);
        std::array<std::uint8_t, 32> digest{};
        CryptoPP::SHA256().CalculateDigest(digest.data(), owned->data(), owned->size());
        return digest == kHsHelmetCandidateModelHash ? owned : nullptr;
    } catch (...) {
        return {};
    }
}
}  // namespace cosmo::helmet_candidate
