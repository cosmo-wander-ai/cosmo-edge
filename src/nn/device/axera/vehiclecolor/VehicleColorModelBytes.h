#pragma once
// Private candidate identity. Exact bytes are hashed after making an owned
// copy; matching SHA does not by itself authorize the scene or task graph.
#include <cryptopp/sha.h>

#include <array>
#include <cstdint>
#include <memory>
#include <vector>
namespace cosmo::helmet_candidate {
// Exact frozen VehicleColor candidate identity; model identity alone does not establish independent accuracy.
inline constexpr std::array<std::uint8_t, 32> kVehicleColorCandidateModelHash{
    {0x87, 0x4f, 0x9f, 0x25, 0x4e, 0x4e, 0xb0, 0x4c, 0xb0, 0x18, 0x29, 0x22, 0xae, 0x8e, 0x8c, 0x55,
     0x2a, 0xe7, 0x8b, 0xfc, 0xf0, 0xcb, 0xc0, 0xa6, 0xc2, 0x62, 0x66, 0xc4, 0xaf, 0x34, 0xb7, 0xfd}};
inline std::shared_ptr<const std::vector<std::uint8_t>> VerifyVehicleColorModelBytes(const void* data,
                                                                                     std::size_t size) {
    if (!data || !size || size > 256u * 1024u * 1024u)
        return {};
    try {
        const auto* first = static_cast<const std::uint8_t*>(data);
        auto owned        = std::make_shared<const std::vector<std::uint8_t>>(first, first + size);
        std::array<std::uint8_t, 32> digest{};
        CryptoPP::SHA256().CalculateDigest(digest.data(), owned->data(), owned->size());
        return digest == kVehicleColorCandidateModelHash ? owned : nullptr;
    } catch (...) {
        return {};
    }
}
}  // namespace cosmo::helmet_candidate
