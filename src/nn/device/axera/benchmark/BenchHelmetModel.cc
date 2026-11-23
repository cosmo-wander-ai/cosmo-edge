#if defined(COSMO_NN_USE_AXERA_BACKEND) && defined(COSMO_AXERA_SCENARIOBENCH_CANDIDATE)
#include <array>
#include <limits>
#include <mutex>
#include <utility>

#include "BenchHelmetExecutor.h"
#include "cryptopp/sha.h"
#include "nn/device/axera/axera_face_model_sdk_loader.h"

namespace cosmo::bench_ax {
VerifiedHelmetModel::VerifiedHelmetModel(std::shared_ptr<const std::vector<std::uint8_t>> bytes,
                                         std::shared_ptr<nn::axera::face_binding::SdkLoadedModel> model,
                                         bool legacy)
    : bytes_(std::move(bytes)),
      model_(std::move(model)),
      legacy_(legacy),
      central_(BenchEngineWorker::OnWorkerThread()),
      central_list_(central_ ? CentralPreparedList::Create() : nullptr) {}
AX_ENGINE_HANDLE VerifiedHelmetModel::HandleForOwnerThread() noexcept {
    return model_ ? model_->HandleForOwnerThread() : nullptr;
}
std::shared_ptr<VerifiedHelmetModel> LoadHelmetModel(const void* data, std::size_t size) {
    if (!data || !size || size > std::numeric_limits<std::uint32_t>::max())
        return {};
    try {
        const auto* first = static_cast<const std::uint8_t*>(data);
        auto bytes        = std::make_shared<const std::vector<std::uint8_t>>(first, first + size);
        std::array<std::uint8_t, CryptoPP::SHA256::DIGESTSIZE> hash{};
        CryptoPP::SHA256().CalculateDigest(hash.data(), bytes->data(), bytes->size());
        constexpr char hex_digits[] = "0123456789abcdef";
        std::array<char, 65> hex{};
        for (std::size_t i = 0; i < hash.size(); ++i) {
            hex[2 * i]     = hex_digits[hash[i] >> 4];
            hex[2 * i + 1] = hex_digits[hash[i] & 15];
        }
        const bool legacy = LegacyHelmetSelected();
        if (std::strcmp(hex.data(), legacy ? LegacyHelmetAxSha256().c_str() : HelmetAxSha256().c_str()) != 0)
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
            if (!HelmetMetadata(info))
                return {};
        }
        return std::shared_ptr<VerifiedHelmetModel>(
            new VerifiedHelmetModel(std::move(bytes), std::move(model), legacy));
    } catch (...) {
        return {};
    }
}
}  // namespace cosmo::bench_ax
#endif
