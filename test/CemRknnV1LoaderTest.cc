#include "catch_amalgamated.hpp"

#if defined(COSMO_NN_USE_RKNN_BACKEND) && defined(COSMO_HAS_MODEL_GUARD)

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "nn/guard/CemRknnV1Loader.h"

namespace cosmo::nn {
namespace {

    struct State {
        CmgRknnV1Status open_status = CMG_RKNN_V1_OK;
        CmgRknnV1Status info_status = CMG_RKNN_V1_OK;
        std::vector<CmgRknnV1Status> load_statuses{CMG_RKNN_V1_OK};
        std::vector<std::uint64_t> destroyed;
        std::string model_path;
        std::string certificate_path;
        std::uint32_t close_calls = 0;
    };

    CmgRknnV1Artifact* Artifact() {
        return reinterpret_cast<CmgRknnV1Artifact*>(static_cast<std::uintptr_t>(0x7000));
    }
    CmgRknnV1Status Open(void* context, const char* model_path, const char* certificate_path,
                         CmgRknnV1Artifact** artifact) {
        auto& state            = *static_cast<State*>(context);
        state.model_path       = model_path;
        state.certificate_path = certificate_path;
        *artifact              = state.open_status == CMG_RKNN_V1_OK ? Artifact() : nullptr;
        return state.open_status;
    }
    CmgRknnV1Status Info(void* context, const CmgRknnV1Artifact*, CmgRknnV1ArtifactInfo* info) {
        auto& state = *static_cast<State*>(context);
        if (state.info_status != CMG_RKNN_V1_OK)
            return state.info_status;
        const std::uint32_t capacity = info->struct_size;
        *info                        = {};
        info->struct_size =
            capacity >= CMG_RKNN_V1_ARTIFACT_INFO_SIZE ? CMG_RKNN_V1_ARTIFACT_INFO_SIZE : capacity;
        info->source_format = CMG_RKNN_V1_SOURCE_RAW_RKNN;
        info->segment_count = static_cast<std::uint32_t>(state.load_statuses.size());
        std::fill(std::begin(info->model_identity_sha256), std::end(info->model_identity_sha256), 0x42);
        return CMG_RKNN_V1_OK;
    }
    CmgRknnV1Status Load(void* context, CmgRknnV1Artifact*, std::uint32_t index, std::uint64_t* output) {
        auto& state = *static_cast<State*>(context);
        if (index >= state.load_statuses.size())
            return CMG_RKNN_V1_RESOURCE_INVALID_ARGUMENT;
        const auto status = state.load_statuses[index];
        *output           = status == CMG_RKNN_V1_OK ? 100U + index : 0U;
        return status;
    }
    void Close(void* context, CmgRknnV1Artifact*) noexcept {
        ++static_cast<State*>(context)->close_calls;
    }
    void Destroy(void* context, std::uint64_t value) noexcept {
        static_cast<State*>(context)->destroyed.push_back(value);
    }
    CmgRknnV1Api GuardApi(State& state) {
        return {CMG_RKNN_V1_ABI_MAJOR, &state, Open, Info, Load, Close};
    }
    RknnContextApi ContextApi(State& state) {
        return {&state, Destroy};
    }

}  // namespace

TEST_CASE("RKNN Guard loader transfers every authenticated context") {
    State state;
    state.load_statuses = {CMG_RKNN_V1_OK, CMG_RKNN_V1_OK};
    {
        auto result = LoadCemRknnV1Artifact(GuardApi(state), ContextApi(state), "/models/model.rknn",
                                            "/data/model-guard/device-certificate.bin");
        REQUIRE(result.IsSuccess());
        REQUIRE(result.contexts.size() == 2);
        REQUIRE(result.model_fingerprint != 0);
        CHECK(result.contexts[0].Get() == 100);
        CHECK(result.contexts[1].Get() == 101);
        CHECK(result.contexts[0].Release() == 100);
        CHECK(state.model_path == "/models/model.rknn");
        CHECK(state.certificate_path == "/data/model-guard/device-certificate.bin");
    }
    CHECK(state.close_calls == 1);
    CHECK(state.destroyed == std::vector<std::uint64_t>{101});
}

TEST_CASE("RKNN Guard loader destroys partial results after a segment failure") {
    State state;
    state.load_statuses = {CMG_RKNN_V1_OK, CMG_RKNN_V1_BACKEND_FAILED};
    const auto result   = LoadCemRknnV1Artifact(GuardApi(state), ContextApi(state), "/models/model.rknn",
                                                "/data/model-guard/device-certificate.bin");
    CHECK_FALSE(result.IsSuccess());
    CHECK(result.error == RknnGuardLoadError::kSegmentLoadFailed);
    CHECK(result.contexts.empty());
    CHECK(state.destroyed == std::vector<std::uint64_t>{100});
    CHECK(state.close_calls == 1);
}

TEST_CASE("RKNN Guard loader fails closed when the ABI is unavailable") {
    State state;
    auto api          = GuardApi(state);
    api.abi_major     = 0;
    const auto result = LoadCemRknnV1Artifact(api, ContextApi(state), "/models/model.rknn",
                                              "/data/model-guard/device-certificate.bin");
    CHECK_FALSE(result.IsSuccess());
    CHECK(result.error == RknnGuardLoadError::kAbiUnavailable);
    CHECK(state.close_calls == 0);
    CHECK(state.destroyed.empty());
}

}  // namespace cosmo::nn

#endif
