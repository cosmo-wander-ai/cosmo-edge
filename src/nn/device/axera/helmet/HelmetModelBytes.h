#pragma once
// Private candidate identity. Exact bytes are hashed after making an owned
// copy; matching SHA does not by itself authorize the scene or task graph.
#include <cryptopp/sha.h>

#include <array>
#include <cstdint>
#include <memory>
#include <vector>
namespace cosmo::helmet_candidate {
inline constexpr std::array<std::uint8_t, 32> kCandidateModelHash{
    {0x4c, 0x63, 0xda, 0x86, 0xa1, 0x58, 0x8b, 0xa2, 0xf9, 0x6d, 0xaa, 0xe1, 0x13, 0x59, 0x26, 0x5c,
     0x4b, 0xcd, 0x29, 0xda, 0xed, 0x95, 0x9a, 0xb0, 0x3f, 0x11, 0x0a, 0x82, 0xfa, 0x52, 0xe3, 0x27}};
inline std::shared_ptr<const std::vector<std::uint8_t>> VerifyHelmetModelBytes(const void* data,
                                                                               std::size_t size) {
    if (!data || !size || size > 256u * 1024u * 1024u)
        return {};
    try {
        const auto* first = static_cast<const std::uint8_t*>(data);
        auto owned        = std::make_shared<const std::vector<std::uint8_t>>(first, first + size);
        std::array<std::uint8_t, 32> digest{};
        CryptoPP::SHA256().CalculateDigest(digest.data(), owned->data(), owned->size());
        return digest == kCandidateModelHash ? owned : nullptr;
    } catch (...) {
        return {};
    }
}
}  // namespace cosmo::helmet_candidate
