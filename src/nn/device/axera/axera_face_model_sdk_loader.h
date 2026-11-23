#pragma once

#ifdef COSMO_NN_USE_AXERA_BACKEND
#include <memory>
#include <thread>

#include "ax_engine_api.h"
#include "nn/device/axera/axera_face_model_binding.h"
#include "nn/device/axera/axera_net_node.h"

namespace cosmo::nn::axera::face_binding {

// Owns the handle, immutable bytes and process-wide engine lease. It is not an
// inference API. The caller must retain this owner, serialize device use, and
// keep it alive until the SDK has definitively completed all submissions.
class SdkLoadedModel final : public LoadedModel {
public:
    bool ReadMetadata(ModelMetadata& output) const override;
    // Only the creation thread may obtain/use this borrowed handle. Never
    // destroy it directly or retain it beyond this object's lifetime.
    AX_ENGINE_HANDLE HandleForOwnerThread() const noexcept;

private:
    friend class SdkLoader;
    SdkLoadedModel() = default;
    ~SdkLoadedModel() override;
    AxeraEngineLease engine_;
    std::shared_ptr<const std::vector<std::uint8_t>> bytes_;
    AX_ENGINE_HANDLE handle_{nullptr};
    bool engine_acquired_{false};
    std::thread::id owner_thread_;
};

// Concrete trusted loader for the exact bytes passed by Binding::Bind.
// Failed or poisoned SDK state cannot publish a model. This does not itself
// prove SDK color, cache visibility, or per-frame I/O stride semantics.
class SdkLoader final : public Loader {
public:
    std::shared_ptr<LoadedModel> Load(
        const std::shared_ptr<const std::vector<std::uint8_t>>& exact_bytes) override;
};

}  // namespace cosmo::nn::axera::face_binding
#endif
