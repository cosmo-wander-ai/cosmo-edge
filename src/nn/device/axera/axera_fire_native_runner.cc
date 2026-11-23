#ifdef COSMO_NN_USE_AXERA_BACKEND
#include "nn/device/axera/axera_fire_native_runner.h"

#include <limits>
#include <mutex>
#include <thread>
#include <utility>

#include "ax_sys_api.h"
#include "nn/device/axera/axera_face_model_sdk_loader.h"
#include "nn/device/axera/axera_native_execution_state.h"

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
                                            reinterpret_cast<const AX_S8*>("cosmo-fire-result"));
            if (rc != 0 || !raw->physical || !raw->host) {
                if (raw->physical || raw->host)
                    state.poisoned = true;
                return nullptr;
            }
        }
        return result;
    }
}  // namespace

RawYuvFireResult FireNativeRunner::Run(const std::shared_ptr<const fire_binding::Capability>& capability,
                                       media::NativeVideoBufferPtr source) {
    return RunForColor(capability, std::move(source), raw_yuv::ColorProfile::Video709Limited);
}
RawYuvFireResult FireNativeRunner::RunJfif(const std::shared_ptr<const fire_binding::Capability>& capability,
                                           media::NativeVideoBufferPtr source) {
    return RunForColor(capability, std::move(source), raw_yuv::ColorProfile::Jfif601Full);
}
RawYuvFireResult FireNativeRunner::RunForColor(
    const std::shared_ptr<const fire_binding::Capability>& capability, media::NativeVideoBufferPtr source,
    raw_yuv::ColorProfile color) {
    auto fail = [](RawYuvError error) { return RawYuvFireResult{error, {}}; };
    if (!capability || !source)
        return fail(RawYuvError::InvalidResources);
    const auto& semantic = capability->GetContract().semantic_contract;
    const bool jfif      = color == raw_yuv::ColorProfile::Jfif601Full;
    if (jfif != (semantic == "ax650-jfif-nv12-1080p-pitch2048-bt601-full-fire-smog-bgr736-pad161-k64-nms-v1"))
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
                                                      contract.graph_iou, 2};
        RawYuvSystemSdk sdk;
        return RawYuvExecutor::ExecuteFire(std::move(resources), sdk, contract.y_name.c_str(),
                                           contract.uv_name.c_str(), contract.output_name.c_str(),
                                           result_contract, contract.required_capacity, contract.classes,
                                           color);
    } catch (...) {
        return fail(RawYuvError::InvalidResources);
    }
}
}  // namespace cosmo::nn::axera
#endif
