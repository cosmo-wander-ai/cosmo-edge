#ifdef COSMO_NN_USE_AXERA_BACKEND
#include "nn/device/axera/axera_person_native_runner.h"

#include <limits>
#include <mutex>
#include <thread>
#include <utility>

#include "ax_sys_api.h"
#include "nn/device/axera/axera_face_model_sdk_loader.h"
#include "nn/device/axera/axera_jfif_person_contract.h"
#include "nn/device/axera/axera_mouse_contract.h"
#include "nn/device/axera/axera_native_execution_state.h"
#include "nn/device/axera/axera_rubbish_contract.h"

namespace cosmo::nn::axera {
namespace {
    struct DeviceOutput {
        std::shared_ptr<face_binding::SdkLoadedModel> model;
        AX_U64 physical{0};
        void* host{nullptr};
        std::thread::id owner_thread;
    };
    std::shared_ptr<DeviceOutput> MakeOutput(std::shared_ptr<face_binding::SdkLoadedModel> model,
                                             AX_U32 size) {
        auto destroy = [](DeviceOutput* output) {
            auto& state = detail::NativeState();
            std::lock_guard<std::mutex> lock(state.mutex);
            if (!output->physical && !output->host) {
                // Keep the model alive until after the global lock is released.
                // The caller also retains the model for the duration of this call.
                delete output;
                return;
            }
            if (state.poisoned || output->owner_thread != std::this_thread::get_id() || !output->physical ||
                !output->host || AX_SYS_MemFree(output->physical, output->host) != 0) {
                state.poisoned = true;
                return;
            }
            delete output;
        };
        auto* raw         = new DeviceOutput;
        raw->owner_thread = std::this_thread::get_id();
        raw->model        = std::move(model);
        std::shared_ptr<DeviceOutput> result(raw, destroy);
        auto& state = detail::NativeState();
        {
            std::lock_guard<std::mutex> lock(state.mutex);
            if (state.poisoned)
                return nullptr;
            const auto rc = AX_SYS_MemAlloc(&raw->physical, &raw->host, size, 128,
                                            reinterpret_cast<const AX_S8*>("cosmo-person-result"));
            if (rc != 0 || !raw->physical || !raw->host) {
                if (raw->physical || raw->host)
                    state.poisoned = true;
                return nullptr;
            }
        }
        return result;
    }
}  // namespace

RawYuvPersonResult PersonNativeRunner::Run(
    const std::shared_ptr<const person_binding::Capability>& capability, media::NativeVideoBufferPtr source,
    float threshold) {
    return RunForColor(capability, std::move(source), threshold, raw_yuv::ColorProfile::Video709Limited);
}
RawYuvPersonResult PersonNativeRunner::RunJfif(
    const std::shared_ptr<const person_binding::Capability>& capability, media::NativeVideoBufferPtr source,
    float threshold) {
    return RunForColor(capability, std::move(source), threshold, raw_yuv::ColorProfile::Jfif601Full);
}
RawYuvPersonResult PersonNativeRunner::RunForColor(
    const std::shared_ptr<const person_binding::Capability>& capability, media::NativeVideoBufferPtr source,
    float scene_threshold, raw_yuv::ColorProfile color) {
    auto fail = [](RawYuvError error) { return RawYuvPersonResult{error, {}}; };
    if (!capability || !source)
        return fail(RawYuvError::InvalidResources);
    const bool jfif = color == raw_yuv::ColorProfile::Jfif601Full;
    if (jfif != (capability->GetContract().semantic_contract == jfif_person_contract::kSemantic))
        return fail(RawYuvError::InvalidContract);
    if (jfif && (!source->frame_witness || !source->frame_witness->Matches(*source)))
        return fail(RawYuvError::InvalidResources);
    try {
        // This holds the binding's execution lock through synchronous device
        // completion and parsing, preventing concurrent task/model revocation.
        auto execution = capability->AcquireForExecution();
        if (!execution)
            return fail(RawYuvError::InvalidContract);
        auto model = std::dynamic_pointer_cast<face_binding::SdkLoadedModel>(execution.Model());
        if (!model)
            return fail(RawYuvError::InvalidResources);
        const auto handle = model->HandleForOwnerThread();
        if (!handle)
            return fail(RawYuvError::InvalidResources);
        const auto& contract = capability->GetContract();
        const auto bytes     = (contract.result_capacity + 1) * 6 * sizeof(float);
        if (bytes > std::numeric_limits<AX_U32>::max())
            return fail(RawYuvError::InvalidContract);
        auto output = MakeOutput(model, static_cast<AX_U32>(bytes));
        if (!output)
            return fail(RawYuvError::InvalidResources);
        auto resources             = std::make_unique<RawYuvResources>();
        resources->device_owner    = output;
        resources->source          = std::move(source);
        resources->engine          = handle;
        resources->result_physical = output->physical;
        resources->result_host     = output->host;
        resources->result_bytes    = static_cast<AX_U32>(bytes);
        resources->owner_thread    = std::this_thread::get_id();
        const detections_v1::Contract result_contract{contract.result_capacity, contract.graph_confidence,
                                                      contract.graph_iou, 1};
        RawYuvSystemSdk sdk;
#ifdef COSMO_AXERA_HS_ROUTE_CANDIDATE
        if (contract.semantic_contract == hs_contract::kSemantic)
            return RawYuvExecutor::ExecuteHs(std::move(resources), sdk, contract.y_name.c_str(),
                                             contract.uv_name.c_str(), contract.output_name.c_str(),
                                             result_contract, contract.required_capacity, scene_threshold);
#endif
#ifdef COSMO_AXERA_MOUSE_ROUTE_CANDIDATE
        if (contract.semantic_contract == mouse_contract::kSemantic &&
            contract.model_sha256 == mouse_contract::ModelSha256())
            return RawYuvExecutor::ExecuteMouse(std::move(resources), sdk, contract.y_name.c_str(),
                                                contract.uv_name.c_str(), contract.output_name.c_str(),
                                                result_contract, contract.required_capacity, scene_threshold);
#endif
#ifdef COSMO_AXERA_RUBBISH_ROUTE_CANDIDATE
        if (contract.semantic_contract == rubbish_contract::kSemantic &&
            contract.model_sha256 == rubbish_contract::ModelSha256())
            return RawYuvExecutor::ExecuteRubbish(
                std::move(resources), sdk, contract.y_name.c_str(), contract.uv_name.c_str(),
                contract.output_name.c_str(), result_contract, contract.required_capacity, scene_threshold);
#endif
        return RawYuvExecutor::ExecutePerson(std::move(resources), sdk, contract.y_name.c_str(),
                                             contract.uv_name.c_str(), contract.output_name.c_str(),
                                             result_contract, contract.required_capacity, scene_threshold,
                                             color);
    } catch (...) {
        return fail(RawYuvError::InvalidResources);
    }
}
}  // namespace cosmo::nn::axera
#endif
