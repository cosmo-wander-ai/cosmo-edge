#pragma once
// TEST ONLY: hash-bound video-profile models; no production admission or task binding.
#include <cryptopp/sha.h>

#include <array>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

#include "ReidPairSdkExecutor.h"
#include "nn/device/axera/axera_face_model_sdk_loader.h"
namespace cosmo::helmet_candidate {
struct ReidModelBytes {
    const void* data{};
    std::size_t size{};
};
class ReidVerifiedBundle final : public ReidPairModelOwner {
public:
    ReidInputProfile profile{ReidInputProfile::Video709Limited};
    ReidInputProfile Profile() const noexcept override {
        return profile;
    }
    std::array<std::shared_ptr<const std::vector<std::uint8_t>>, 2> bytes;
    std::array<std::shared_ptr<nn::axera::face_binding::SdkLoadedModel>, 2> models;
    std::shared_ptr<nn::axera::face_binding::SdkLoadedModel> quality_model;
    std::shared_ptr<const std::vector<std::uint8_t>> quality_bytes;
    AX_ENGINE_HANDLE QualityForOwnerThread() noexcept override {
        return quality_model ? quality_model->HandleForOwnerThread() : nullptr;
    }
    AX_ENGINE_HANDLE BranchForOwnerThread(unsigned branch) noexcept override {
        return branch < models.size() && models[branch] ? models[branch]->HandleForOwnerThread() : nullptr;
    }
    AX_ENGINE_HANDLE HandleForOwnerThread() noexcept override {
        return BranchForOwnerThread(0);
    }
};
inline std::shared_ptr<ReidVerifiedBundle> LoadReidBundle(const std::array<ReidModelBytes, 2>& inputs,
                                                          ReidInputProfile profile,
                                                          ReidModelBytes quality = {}) {
    static constexpr std::array<std::array<std::uint8_t, 32>, 2> hashes{
        {{{0xe4, 0x7b, 0x05, 0xb8, 0x13, 0x32, 0xd8, 0x6b, 0x42, 0xb6, 0x7e, 0x36, 0xb1, 0x9d, 0x2e, 0x55,
           0x07, 0xed, 0x8b, 0x65, 0x1e, 0x61, 0x6c, 0xc4, 0x30, 0xf7, 0xe8, 0xce, 0xbb, 0x22, 0x16, 0xe1}},
         {{0x26, 0xc8, 0xa6, 0x78, 0xd2, 0x2b, 0x8a, 0xa4, 0x27, 0xf3, 0x4c, 0x30, 0x44, 0x43, 0xe8, 0x67,
           0xf4, 0x8a, 0x0e, 0x4c, 0x39, 0x99, 0xc8, 0xb8, 0xac, 0xc3, 0xb4, 0x72, 0x27, 0x87, 0x50, 0xb5}}}};
    static constexpr std::array<std::array<std::uint8_t, 32>, 2> jfif_hashes{
        {{{0xe4, 0x4a, 0xd3, 0xec, 0x44, 0xee, 0x28, 0x0c, 0x57, 0x56, 0x2b, 0x4a, 0x4f, 0xad, 0x05, 0xbf,
           0x7b, 0x13, 0xdb, 0x1a, 0x99, 0x25, 0x97, 0x6f, 0x16, 0x47, 0xb3, 0x0f, 0x7f, 0x0b, 0xf4, 0x2a}},
         {{0x08, 0xa2, 0xce, 0x8d, 0x4d, 0x1f, 0x48, 0x49, 0x5a, 0xa9, 0x90, 0x60, 0xea, 0x8e, 0xb9, 0xb2,
           0x5f, 0x01, 0xb4, 0x33, 0xe0, 0x50, 0x91, 0x04, 0x3e, 0xbb, 0xa2, 0x1c, 0xbc, 0x78, 0xd8, 0xbe}}}};
    if (profile != ReidInputProfile::Video709Limited && profile != ReidInputProfile::Jfif601Full)
        return {};
    try {
        auto bundle     = std::make_shared<ReidVerifiedBundle>();
        bundle->profile = profile;
        for (std::size_t i = 0; i < 2; ++i) {
            const auto& in = inputs[i];
            if (!in.data || !in.size || in.size > 256u * 1024u * 1024u)
                return {};
            const auto* first = static_cast<const std::uint8_t*>(in.data);
            auto bytes        = std::make_shared<const std::vector<std::uint8_t>>(first, first + in.size);
            std::array<std::uint8_t, 32> digest{};
            CryptoPP::SHA256().CalculateDigest(digest.data(), bytes->data(), bytes->size());
            if (digest != (profile == ReidInputProfile::Jfif601Full ? jfif_hashes[i] : hashes[i]))
                return {};
            nn::axera::face_binding::SdkLoader loader;
            auto model =
                std::dynamic_pointer_cast<nn::axera::face_binding::SdkLoadedModel>(loader.Load(bytes));
            if (!model)
                return {};
            auto h = model->HandleForOwnerThread();
            if (!h)
                return {};
            auto& global = nn::axera::detail::NativeState();
            {
                std::lock_guard<std::mutex> lock(global.mutex);
                if (global.poisoned)
                    return {};
                AX_ENGINE_IO_INFO_T* info = nullptr;
                try {
                    if (AX_ENGINE_GetIOInfo(h, &info) != 0) {
                        global.poisoned = true;
                        return {};
                    }
                } catch (...) {
                    global.poisoned = true;
                    return {};
                }
                if (!ReidMetadataMatches(info))
                    return {};
            }
            bundle->bytes[i]  = std::move(bytes);
            bundle->models[i] = std::move(model);
        }
        if (quality.data || quality.size) {
            if (profile != ReidInputProfile::Video709Limited || !quality.data || !quality.size ||
                quality.size > 256u * 1024u * 1024u)
                return {};
            const auto* first = static_cast<const std::uint8_t*>(quality.data);
            auto bytes = std::make_shared<const std::vector<std::uint8_t>>(first, first + quality.size);
            std::array<std::uint8_t, 32> digest{};
            CryptoPP::SHA256().CalculateDigest(digest.data(), bytes->data(), bytes->size());
            static constexpr std::array<std::uint8_t, 32> expected{
                {0x19, 0xaf, 0x62, 0x32, 0x82, 0x91, 0xba, 0x1d, 0x4e, 0x91, 0xb7,
                 0xbb, 0x8c, 0x46, 0xf1, 0x30, 0xdd, 0x03, 0x18, 0xba, 0xc9, 0x8b,
                 0x60, 0x10, 0x28, 0x4f, 0xd7, 0x3e, 0xeb, 0x37, 0xa1, 0xdb}};
            if (digest != expected)
                return {};
            nn::axera::face_binding::SdkLoader loader;
            auto model =
                std::dynamic_pointer_cast<nn::axera::face_binding::SdkLoadedModel>(loader.Load(bytes));
            if (!model)
                return {};
            auto h = model->HandleForOwnerThread();
            if (!h)
                return {};
            auto& global = nn::axera::detail::NativeState();
            {
                std::lock_guard<std::mutex> lock(global.mutex);
                if (global.poisoned)
                    return {};
                AX_ENGINE_IO_INFO_T* info = nullptr;
                try {
                    if (AX_ENGINE_GetIOInfo(h, &info) != 0) {
                        global.poisoned = true;
                        return {};
                    }
                } catch (...) {
                    global.poisoned = true;
                    return {};
                }
                if (!PedQualityMetadataMatches(info))
                    return {};
            }
            bundle->quality_bytes = std::move(bytes);
            bundle->quality_model = std::move(model);
        }
        return bundle;
    } catch (...) {
        return {};
    }
}
}  // namespace cosmo::helmet_candidate
