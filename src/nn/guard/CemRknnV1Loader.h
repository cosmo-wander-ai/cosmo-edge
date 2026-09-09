#pragma once

#if defined(COSMO_NN_USE_RKNN_BACKEND) && defined(COSMO_HAS_MODEL_GUARD)

#include <cosmo_model_guard_rknn_v1.h>
#include <rknn_api.h>

#include <cstdint>
#include <vector>

namespace cosmo::nn {

static_assert(CMG_RKNN_V1_ABI_MAJOR == UINT32_C(1), "CosmoEdge requires RKNN model-guard ABI major 1");
static_assert(sizeof(rknn_context) == sizeof(std::uint64_t),
              "RK3576 model-guard requires a 64-bit RKNN context");

struct CmgRknnV1Api {
    using OpenArtifact    = CmgRknnV1Status (*)(void* context, const char* path, const char* certificate_path,
                                             CmgRknnV1Artifact** out_artifact);
    using GetArtifactInfo = CmgRknnV1Status (*)(void* context, const CmgRknnV1Artifact* artifact,
                                                CmgRknnV1ArtifactInfo* out_info);
    using LoadSegment     = CmgRknnV1Status (*)(void* context, CmgRknnV1Artifact* artifact,
                                            std::uint32_t segment_index, std::uint64_t* out_context);
    using CloseArtifact   = void (*)(void* context, CmgRknnV1Artifact* artifact) noexcept;

    std::uint32_t abi_major = 0;
    void* context           = nullptr;
    OpenArtifact open_artifact{};
    GetArtifactInfo get_artifact_info{};
    LoadSegment load_segment{};
    CloseArtifact close_artifact{};
};

struct RknnContextApi {
    using Destroy = void (*)(void* context, std::uint64_t rknn_context) noexcept;
    void* context = nullptr;
    Destroy destroy{};
};

class GuardOwnedRknnContext final {
public:
    GuardOwnedRknnContext() = default;
    GuardOwnedRknnContext(std::uint64_t context, RknnContextApi api) noexcept;
    ~GuardOwnedRknnContext() noexcept;

    GuardOwnedRknnContext(const GuardOwnedRknnContext&)            = delete;
    GuardOwnedRknnContext& operator=(const GuardOwnedRknnContext&) = delete;
    GuardOwnedRknnContext(GuardOwnedRknnContext&& other) noexcept;
    GuardOwnedRknnContext& operator=(GuardOwnedRknnContext&& other) noexcept;

    [[nodiscard]] std::uint64_t Get() const noexcept;
    [[nodiscard]] std::uint64_t Release() noexcept;

private:
    void Reset() noexcept;
    std::uint64_t context_ = 0;
    RknnContextApi api_{};
};

enum class RknnGuardLoadError {
    kNone,
    kInvalidArgument,
    kAbiUnavailable,
    kArtifactOpenFailed,
    kArtifactInfoFailed,
    kArtifactInfoInvalid,
    kSegmentLoadFailed,
    kAbiContractViolation,
    kNoMemory,
};

struct RknnGuardLoadResult {
    RknnGuardLoadError error        = RknnGuardLoadError::kNone;
    CmgRknnV1Status guard_status    = CMG_RKNN_V1_OK;
    std::uint64_t model_fingerprint = 0;
    std::vector<GuardOwnedRknnContext> contexts;

    [[nodiscard]] bool IsSuccess() const noexcept {
        return error == RknnGuardLoadError::kNone;
    }
    [[nodiscard]] bool IsOutOfMemory() const noexcept {
        return error == RknnGuardLoadError::kNoMemory || guard_status == CMG_RKNN_V1_RESOURCE_NO_MEMORY;
    }
};

[[nodiscard]] const CmgRknnV1Api& FrozenCmgRknnV1Api() noexcept;
[[nodiscard]] const RknnContextApi& NativeRknnContextApi() noexcept;
[[nodiscard]] RknnGuardLoadResult LoadCemRknnV1Artifact(const CmgRknnV1Api& guard_api,
                                                        const RknnContextApi& context_api,
                                                        const char* model_path, const char* certificate_path);

}  // namespace cosmo::nn

#endif
