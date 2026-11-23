#if defined(COSMO_NN_USE_AXERA_BACKEND) && defined(COSMO_AXERA_HSHELMET_ROUTE_CANDIDATE)
#include "nn/device/axera/axera_hshelmet_model_loader.h"

#include <mutex>
#include <utility>

#include "nn/device/axera/axera_face_model_sdk_loader.h"
#include "nn/device/axera/hshelmet/HsHelmetMetadata.h"
#include "nn/device/axera/hshelmet/HsHelmetModelBytes.h"
namespace cosmo::helmet_candidate {
HsHelmetVerifiedModel::HsHelmetVerifiedModel(std::shared_ptr<const std::vector<std::uint8_t>> bytes,
                                             std::shared_ptr<nn::axera::face_binding::SdkLoadedModel> model)
    : bytes_(std::move(bytes)), model_(std::move(model)) {}
AX_ENGINE_HANDLE HsHelmetVerifiedModel::HandleForOwnerThread() noexcept {
    return model_ ? model_->HandleForOwnerThread() : nullptr;
}
std::shared_ptr<HsHelmetVerifiedModel> LoadHsHelmetModel(const void* data, std::size_t size) {
    try {
        auto bytes = VerifyHsHelmetModelBytes(data, size);
        if (!bytes)
            return {};
        nn::axera::face_binding::SdkLoader loader;
        auto model = std::dynamic_pointer_cast<nn::axera::face_binding::SdkLoadedModel>(loader.Load(bytes));
        if (!model)
            return {};
        const auto handle = model->HandleForOwnerThread();
        if (!handle)
            return {};
        auto& global = nn::axera::detail::NativeState();
        {
            std::lock_guard<std::mutex> lock(global.mutex);
            if (global.poisoned)
                return {};
            AX_ENGINE_IO_INFO_T* info = nullptr;
            try {
                if (AX_ENGINE_GetIOInfo(handle, &info) != 0) {
                    global.poisoned = true;
                    return {};
                }
            } catch (...) {
                global.poisoned = true;
                return {};
            }
            if (!HsHelmetMetadataMatches(info))
                return {};
        }
        return std::shared_ptr<HsHelmetVerifiedModel>(
            new HsHelmetVerifiedModel(std::move(bytes), std::move(model)));
    } catch (...) {
        return {};
    }
}
}  // namespace cosmo::helmet_candidate
#endif
