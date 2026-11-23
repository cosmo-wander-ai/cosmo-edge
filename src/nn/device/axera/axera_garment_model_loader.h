#pragma once
#if defined(COSMO_NN_USE_AXERA_BACKEND) && defined(COSMO_AXERA_GARMENT_ROUTE_CANDIDATE)
#include <cstdint>
#include <memory>
#include <vector>

#include "nn/device/axera/helmet/HelmetPendingResources.h"
namespace cosmo::nn::axera::face_binding {
class SdkLoadedModel;
}
namespace cosmo::helmet_candidate {
class GarmentVerifiedModel final : public ModelOwner {
public:
    AX_ENGINE_HANDLE HandleForOwnerThread() noexcept override;

private:
    friend std::shared_ptr<GarmentVerifiedModel> LoadGarmentModel(const void*, std::size_t);
    GarmentVerifiedModel(std::shared_ptr<const std::vector<std::uint8_t>>,
                         std::shared_ptr<nn::axera::face_binding::SdkLoadedModel>);
    const std::shared_ptr<const std::vector<std::uint8_t>> bytes_;
    const std::shared_ptr<nn::axera::face_binding::SdkLoadedModel> model_;
};
// Caller supplies an immutable input snapshot and calls/uses/releases the
// result on the classifier worker thread. Scene/task authority is separate.
std::shared_ptr<GarmentVerifiedModel> LoadGarmentModel(const void*, std::size_t);
}  // namespace cosmo::helmet_candidate
#endif
