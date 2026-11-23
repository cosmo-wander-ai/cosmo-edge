#pragma once
// Private candidate identity. Exact bytes are hashed after making an owned
// copy; matching SHA does not by itself authorize the scene or task graph.
#include <cryptopp/sha.h>

#include <array>
#include <cstdint>
#include <memory>
#include <vector>
namespace cosmo::helmet_candidate {
// Exact frozen Phone candidate identity; model identity alone does not establish independent accuracy.
inline constexpr std::array<std::uint8_t, 32> kPhoneCandidateModelHash{
    {0xd4, 0x89, 0x40, 0x04, 0xa9, 0xe2, 0x51, 0x8e, 0xb6, 0xf1, 0x09, 0x7e, 0x43, 0x56, 0x5f, 0xf3,
     0xe9, 0x31, 0x11, 0x1b, 0xc6, 0x09, 0x04, 0xfa, 0xaf, 0x7e, 0xea, 0xec, 0x2f, 0x1c, 0x74, 0xf5}};
inline std::shared_ptr<const std::vector<std::uint8_t>> VerifyPhoneModelBytes(const void* data,
                                                                              std::size_t size) {
    if (!data || !size || size > 256u * 1024u * 1024u)
        return {};
    try {
        const auto* first = static_cast<const std::uint8_t*>(data);
        auto owned        = std::make_shared<const std::vector<std::uint8_t>>(first, first + size);
        std::array<std::uint8_t, 32> digest{};
        CryptoPP::SHA256().CalculateDigest(digest.data(), owned->data(), owned->size());
        return digest == kPhoneCandidateModelHash ? owned : nullptr;
    } catch (...) {
        return {};
    }
}
}  // namespace cosmo::helmet_candidate
