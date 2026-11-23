#pragma once
// Task-scoped five-model loader.  Profile selects both preprocessing models;
// models are hash-bound and retained through the SDK's uncertain-completion path.
#include <cryptopp/sha.h>

#include <array>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

#include "FaceChainSdkExecutor.h"
#include "nn/device/axera/axera_face_model_sdk_loader.h"
namespace cosmo::helmet_candidate {
struct FaceModelBytes {
    const void* data{};
    std::size_t size{};
};
class FaceVerifiedModel final : public ModelOwner {
public:
    FaceVerifiedModel(std::shared_ptr<const std::vector<std::uint8_t>> b,
                      std::shared_ptr<nn::axera::face_binding::SdkLoadedModel> m)
        : bytes_(std::move(b)), model_(std::move(m)) {}
    AX_ENGINE_HANDLE HandleForOwnerThread() noexcept override {
        return model_ ? model_->HandleForOwnerThread() : nullptr;
    }

private:
    std::shared_ptr<const std::vector<std::uint8_t>> bytes_;
    std::shared_ptr<nn::axera::face_binding::SdkLoadedModel> model_;
};
struct FaceModelBundle {
    FaceInputProfile profile;
    std::array<std::shared_ptr<ModelOwner>, 5> models;
};
inline std::shared_ptr<FaceModelBundle> LoadFaceModelBundle(FaceInputProfile profile,
                                                            const std::array<FaceModelBytes, 5>& inputs) {
    static constexpr std::array<std::array<std::uint8_t, 32>, 7> hashes{
        {{{0x39, 0x3d, 0x96, 0xdb, 0x9a, 0xbb, 0xd3, 0x62, 0x41, 0x33, 0x41, 0x3a, 0xad, 0x2a, 0x4c, 0x15,
           0xfe, 0x3b, 0xc8, 0xdf, 0x63, 0x0b, 0xb9, 0x11, 0xc5, 0xe4, 0xe9, 0xe1, 0xfd, 0xdd, 0x89, 0x23}},
         {{0xef, 0xa5, 0x0d, 0x5a, 0xff, 0x29, 0xc8, 0x84, 0x7d, 0xa1, 0x99, 0xd8, 0x77, 0x96, 0xa0, 0x3c,
           0x2d, 0xb1, 0xf9, 0x6e, 0x2b, 0x4c, 0xfd, 0x55, 0xba, 0x9d, 0x36, 0xf9, 0xaa, 0xb7, 0x87, 0x4e}},
         {{0x4c, 0xa2, 0x56, 0xa8, 0x9a, 0xd0, 0xf2, 0x81, 0x1d, 0x0e, 0x16, 0x34, 0x63, 0x97, 0x35, 0x4d,
           0x4e, 0x7f, 0xda, 0x12, 0x0b, 0x88, 0xe8, 0xc4, 0x24, 0x32, 0xdc, 0x3a, 0xe0, 0x03, 0x44, 0x7a}},
         {{0x67, 0x9b, 0xa2, 0x37, 0x30, 0x09, 0xd7, 0x52, 0x57, 0x32, 0xdd, 0x14, 0xc4, 0x2a, 0xbb, 0x80,
           0x2d, 0x76, 0x53, 0xb9, 0x50, 0xf4, 0x7c, 0xdb, 0x90, 0x72, 0x5c, 0xa1, 0x33, 0xcd, 0x1d, 0x21}},
         {{0x1f, 0xb5, 0x77, 0x0b, 0x37, 0x43, 0x82, 0x93, 0x1d, 0x3e, 0x8f, 0x1c, 0x68, 0xdc, 0x23, 0x13,
           0x5e, 0xe5, 0xcc, 0x06, 0x68, 0xe6, 0x1e, 0x7a, 0xa5, 0xe8, 0x09, 0x3b, 0x4d, 0xc7, 0x0c, 0x56}},
         {{0x6b, 0x57, 0x25, 0x30, 0x83, 0xac, 0xb7, 0xe8, 0xed, 0x1d, 0xd6, 0x6c, 0x24, 0x91, 0xb5, 0x0d,
           0x1a, 0x1c, 0x5c, 0x43, 0xf3, 0x91, 0x9c, 0x56, 0x6a, 0x41, 0x4a, 0x80, 0x28, 0x74, 0x71, 0xc7}},
         {{0x9f, 0x66, 0x9a, 0x6f, 0x8d, 0x15, 0xd5, 0x28, 0x9e, 0x58, 0x70, 0x61, 0xe1, 0x03, 0x69, 0xf8,
           0xb6, 0xac, 0x8f, 0x5e, 0xd2, 0x1b, 0x9e, 0x72, 0x1c, 0x5a, 0xa4, 0x85, 0x22, 0x91, 0xed, 0x58}}}};
    if (profile != FaceInputProfile::Video709Limited && profile != FaceInputProfile::Jfif601Full)
        return {};
    try {
        auto bundle     = std::make_shared<FaceModelBundle>();
        bundle->profile = profile;
        for (std::size_t i = 0; i < 5; ++i) {
            const auto& in = inputs[i];
            if (!in.data || !in.size || in.size > 256u * 1024u * 1024u)
                return {};
            const auto* first = static_cast<const std::uint8_t*>(in.data);
            auto bytes        = std::make_shared<const std::vector<std::uint8_t>>(first, first + in.size);
            std::array<std::uint8_t, 32> digest{};
            CryptoPP::SHA256().CalculateDigest(digest.data(), bytes->data(), bytes->size());
            const auto index = i < 2 ? i + (profile == FaceInputProfile::Jfif601Full ? 2 : 0) : i + 2;
            if (digest != hashes[index])
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
                AX_ENGINE_IO_INFO_T* meta = nullptr;
                try {
                    if (AX_ENGINE_GetIOInfo(h, &meta) != 0) {
                        global.poisoned = true;
                        return {};
                    }
                } catch (...) {
                    global.poisoned = true;
                    return {};
                }
                if (!(i == 0   ? FaceLandmarkMetadataMatches(meta)
                      : i == 1 ? FaceCropMetadataMatches(meta)
                               : FaceFeatureBranchMetadataMatches(meta)))
                    return {};
            }
            bundle->models[i] = std::make_shared<FaceVerifiedModel>(std::move(bytes), std::move(model));
        }
        return bundle;
    } catch (...) {
        return {};
    }
}
}  // namespace cosmo::helmet_candidate
