#pragma once

// Experimental two-plane SDK boundary. No graph calls it and no production
// model/color binding is established. A caller must retain exclusive device
// ownership, bind model identity and same-frame color provenance, and verify
// the actual SDK metadata and cache/visibility contract before enabling it.
#ifdef COSMO_NN_USE_AXERA_BACKEND
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <thread>

#include "ax_engine_api.h"
#include "ax_global_type.h"
#include "media/NativeVideoBuffer.h"
#include "nn/device/axera/axera_face_result.h"
#include "nn/device/axera/axera_fire_result.h"
#include "nn/device/axera/axera_mouse_result.h"
#include "nn/device/axera/axera_native_execution_state.h"
#include "nn/device/axera/axera_person_result.h"
#include "nn/device/axera/axera_raw_yuv_contract.h"
#include "nn/device/axera/axera_rubbish_result.h"
#include "nn/device/axera/axera_vehicle_result.h"

namespace cosmo::nn::axera {
struct RawYuvResources {
    std::shared_ptr<void> device_owner;  // engine, model and result allocation
    media::NativeVideoBufferPtr source;  // owns both VDEC planes
    AX_ENGINE_HANDLE engine{nullptr};
    AX_U64 result_physical{0};
    void* result_host{nullptr};
    AX_U32 result_bytes{0};
    std::thread::id owner_thread{};
};

enum class RawYuvError {
    None,
    InvalidResources,
    InvalidMetadata,
    InvalidContract,
    DeviceCompletionUnknown,
    InvalidResult,
    Quarantined
};
struct RawYuvResult {
    RawYuvError error{RawYuvError::None};
    FaceResult face;
};
struct RawYuvPersonResult {
    RawYuvError error{RawYuvError::None};
    PersonResult person;
};
struct RawYuvFireResult {
    RawYuvError error{RawYuvError::None};
    FireResult fire;
};
struct RawYuvVehicleResult {
    RawYuvError error{RawYuvError::None};
    VehicleResult vehicle;
};

class RawYuvSdk {
public:
    virtual ~RawYuvSdk()                                                          = default;
    virtual AX_S32 GetIOInfo(AX_ENGINE_HANDLE handle, AX_ENGINE_IO_INFO_T** info) = 0;
    virtual AX_S32 Run(AX_ENGINE_HANDLE handle, AX_ENGINE_IO_T* io)               = 0;
};
class RawYuvSystemSdk final : public RawYuvSdk {
public:
    AX_S32 GetIOInfo(AX_ENGINE_HANDLE handle, AX_ENGINE_IO_INFO_T** info) override {
        return AX_ENGINE_GetIOInfo(handle, info);
    }
    AX_S32 Run(AX_ENGINE_HANDLE handle, AX_ENGINE_IO_T* io) override {
        return AX_ENGINE_RunSync(handle, io);
    }
};

class RawYuvExecutor {
    struct Pending {
        explicit Pending(std::unique_ptr<RawYuvResources> r) : resources(std::move(r)) {}
        std::unique_ptr<RawYuvResources> resources;
        std::shared_ptr<Pending> keep_alive;
        std::array<std::array<AX_S32, 4>, 2> input_strides{};
        std::array<AX_S32, 3> output_stride{};
        std::array<AX_ENGINE_IO_BUFFER_T, 2> inputs{};
        AX_ENGINE_IO_BUFFER_T output{};
        AX_ENGINE_IO_T io{};
    };
    static bool PositiveStride(const AX_ENGINE_IOMETA_T& m, raw_yuv::Input& input) {
        if (!m.pStride)
            return true;
        input.model_strides_present = true;
        for (std::size_t i = 0; i < input.model_strides.size(); ++i) {
            if (m.pStride[i] <= 0)
                return false;
            input.model_strides[i] = static_cast<std::size_t>(m.pStride[i]);
        }
        return true;
    }
    static bool OutputStride(const AX_ENGINE_IOMETA_T& m, const std::array<AX_S32, 3>& expected) {
        return !m.pStride || std::equal(expected.begin(), expected.end(), m.pStride);
    }
    static bool Intersects(std::uint64_t a, std::size_t as, std::uint64_t b, std::size_t bs) {
        return a < b + bs && b < a + as;
    }

public:
    static RawYuvResult Execute(std::unique_ptr<RawYuvResources> resources, RawYuvSdk& sdk,
                                const char* y_name, const char* uv_name, const char* output_name,
                                const detections_v1::Contract& contract, std::size_t required_capacity,
                                float scene_threshold,
                                raw_yuv::ColorProfile color = raw_yuv::ColorProfile::Video709Limited) {
        return ExecuteImpl<RawYuvResult>(std::move(resources), sdk, y_name, uv_name, output_name, contract,
                                         required_capacity, scene_threshold, 1, ConvertFaceResult,
                                         FaceResultError::None, AX_ENGINE_TENSOR_LAYOUT_NCHW, color);
    }
    // This entry is not an admission path. The caller must hold a person model
    // capability and a same-frame witness for the lifetime of the call.
    static RawYuvPersonResult ExecutePerson(
        std::unique_ptr<RawYuvResources> resources, RawYuvSdk& sdk, const char* y_name, const char* uv_name,
        const char* output_name, const detections_v1::Contract& contract, std::size_t required_capacity,
        float scene_threshold, raw_yuv::ColorProfile color = raw_yuv::ColorProfile::Video709Limited) {
        if (contract.capacity != 64 || !required_capacity || required_capacity > 64 ||
            contract.class_count != 1 || !detections_v1::SameBits(contract.confidence, .1f) ||
            !detections_v1::SameBits(contract.iou, .35f))
            return {RawYuvError::InvalidContract, {}};
        return ExecuteImpl<RawYuvPersonResult>(std::move(resources), sdk, y_name, uv_name, output_name,
                                               contract, required_capacity, scene_threshold, 1,
                                               ConvertPersonResult, PersonResultError::None,
                                               AX_ENGINE_TENSOR_LAYOUT_NCHW, color);
    }

#if defined(COSMO_AXERA_HS_ROUTE_CANDIDATE)
    static RawYuvPersonResult ExecuteHs(std::unique_ptr<RawYuvResources> resources, RawYuvSdk& sdk,
                                        const char* y_name, const char* uv_name, const char* output_name,
                                        const detections_v1::Contract& contract,
                                        std::size_t required_capacity, float scene_threshold) {
        if (contract.capacity != 64 || required_capacity != 64 || contract.class_count != 1 ||
            !detections_v1::SameBits(contract.confidence, .1f) ||
            !detections_v1::SameBits(contract.iou, .35f) || !hs_contract::IsThreshold(scene_threshold))
            return {RawYuvError::InvalidContract, {}};
        return ExecuteImpl<RawYuvPersonResult>(
            std::move(resources), sdk, y_name, uv_name, output_name, contract, required_capacity,
            scene_threshold, 1, ConvertHsResult, PersonResultError::None, AX_ENGINE_TENSOR_LAYOUT_UNKNOWN);
    }
#endif

#ifdef COSMO_AXERA_MOUSE_ROUTE_CANDIDATE
    static RawYuvPersonResult ExecuteMouse(std::unique_ptr<RawYuvResources> resources, RawYuvSdk& sdk,
                                           const char* y_name, const char* uv_name, const char* output_name,
                                           const detections_v1::Contract& contract,
                                           std::size_t required_capacity, float scene_threshold) {
        if (contract.capacity != 64 || required_capacity != 64 || contract.class_count != 1 ||
            !detections_v1::SameBits(contract.confidence, .1f) ||
            !detections_v1::SameBits(contract.iou, .35f) || !mouse_contract::IsThreshold(scene_threshold))
            return {RawYuvError::InvalidContract, {}};
        return ExecuteImpl<RawYuvPersonResult>(
            std::move(resources), sdk, y_name, uv_name, output_name, contract, required_capacity,
            scene_threshold, 1, ConvertMouseResult, PersonResultError::None, AX_ENGINE_TENSOR_LAYOUT_UNKNOWN);
    }
#endif
#ifdef COSMO_AXERA_RUBBISH_ROUTE_CANDIDATE
    static RawYuvPersonResult ExecuteRubbish(std::unique_ptr<RawYuvResources> resources, RawYuvSdk& sdk,
                                             const char* y_name, const char* uv_name, const char* output_name,
                                             const detections_v1::Contract& contract,
                                             std::size_t required_capacity, float scene_threshold) {
        if (contract.capacity != 64 || required_capacity != 64 || contract.class_count != 1 ||
            !detections_v1::SameBits(contract.confidence, .1f) ||
            !detections_v1::SameBits(contract.iou, .35f) || !rubbish_contract::IsThreshold(scene_threshold))
            return {RawYuvError::InvalidContract, {}};
        return ExecuteImpl<RawYuvPersonResult>(std::move(resources), sdk, y_name, uv_name, output_name,
                                               contract, required_capacity, scene_threshold, 1,
                                               ConvertRubbishResult, PersonResultError::None,
                                               AX_ENGINE_TENSOR_LAYOUT_UNKNOWN);
    }
#endif
    // Experimental only: a separately authenticated capability and same-frame
    // witness are still required. Class policies are immutable for this call.
    static RawYuvFireResult ExecuteFire(
        std::unique_ptr<RawYuvResources> resources, RawYuvSdk& sdk, const char* y_name, const char* uv_name,
        const char* output_name, const detections_v1::Contract& contract, std::size_t required_capacity,
        const std::array<fire_binding::ClassPolicy, 2> policies,
        raw_yuv::ColorProfile color = raw_yuv::ColorProfile::Video709Limited) {
        if (contract.capacity != 64 || !required_capacity || required_capacity > 64 ||
            contract.class_count != 2 || !detections_v1::SameBits(contract.confidence, .1f) ||
            !detections_v1::SameBits(contract.iou, .35f) || policies[0].label != "fire" ||
            policies[1].label != "smog" || !(policies[0].enabled || policies[1].enabled) ||
            !std::all_of(policies.begin(), policies.end(), [&](const auto& policy) {
                return std::isfinite(policy.threshold) && policy.threshold >= contract.confidence &&
                       policy.threshold <= 1.f;
            }))
            return {RawYuvError::InvalidContract, {}};
        const auto convert = [&policies](const auto& output_contract, const auto& view, int width, int height,
                                         float) {
            return ConvertFireResult(output_contract, view, width, height, policies);
        };
        return ExecuteImpl<RawYuvFireResult>(std::move(resources), sdk, y_name, uv_name, output_name,
                                             contract, required_capacity, contract.confidence, 2, convert,
                                             FireResultError::None, AX_ENGINE_TENSOR_LAYOUT_NCHW, color);
    }

    static RawYuvVehicleResult ExecuteVehicle(std::unique_ptr<RawYuvResources> resources, RawYuvSdk& sdk,
                                              const char* y_name, const char* uv_name,
                                              const char* output_name,
                                              const detections_v1::Contract& contract,
                                              std::size_t required_capacity,
                                              const std::array<vehicle_binding::ClassPolicy, 3> policies) {
        if (contract.capacity != 64 || !required_capacity || required_capacity > 64 ||
            contract.class_count != 3 || !detections_v1::SameBits(contract.confidence, .1f) ||
            !detections_v1::SameBits(contract.iou, .35f) || policies[0].label != "Person" ||
            policies[1].label != "Nonvehicle" || policies[2].label != "Vehicle" ||
            !(policies[0].enabled || policies[1].enabled || policies[2].enabled) ||
            !std::all_of(policies.begin(), policies.end(), [&](const auto& policy) {
                return std::isfinite(policy.threshold) && policy.threshold >= contract.confidence &&
                       policy.threshold <= 1.f;
            }))
            return {RawYuvError::InvalidContract, {}};
        const auto convert = [&policies](const auto& output_contract, const auto& view, int width, int height,
                                         float) {
            return ConvertVehicleResult(output_contract, view, width, height, policies);
        };
        return ExecuteImpl<RawYuvVehicleResult>(std::move(resources), sdk, y_name, uv_name, output_name,
                                                contract, required_capacity, contract.confidence, 3, convert,
                                                VehicleResultError::None);
    }

private:
    template <typename Result, typename Convert, typename Success>
    static Result ExecuteImpl(std::unique_ptr<RawYuvResources> resources, RawYuvSdk& sdk, const char* y_name,
                              const char* uv_name, const char* output_name,
                              const detections_v1::Contract& contract, std::size_t required_capacity,
                              float scene_threshold, std::size_t expected_class_count, Convert convert,
                              Success success,
                              AX_ENGINE_TENSOR_LAYOUT_T expected_input_layout = AX_ENGINE_TENSOR_LAYOUT_NCHW,
                              raw_yuv::ColorProfile color = raw_yuv::ColorProfile::Video709Limited) {
        auto fail    = [](RawYuvError error) { return Result{error, {}}; };
        auto& global = detail::NativeState();
        std::lock_guard<std::mutex> lock(global.mutex);
        if (global.poisoned)
            return fail(RawYuvError::Quarantined);
        if (!resources || !resources->device_owner || !resources->source || !resources->source->Valid() ||
            resources->owner_thread != std::this_thread::get_id() || !resources->engine ||
            !resources->result_physical || !resources->result_host || !y_name || !*y_name || !uv_name ||
            !*uv_name || !output_name || !*output_name || std::strcmp(y_name, uv_name) == 0)
            return fail(RawYuvError::InvalidResources);
        if (!contract.capacity || contract.capacity > 1000 || !required_capacity ||
            contract.capacity < required_capacity || contract.class_count != expected_class_count ||
            !std::isfinite(contract.confidence) || contract.confidence <= 0.f || contract.confidence > 1.f ||
            !std::isfinite(contract.iou) || contract.iou < 0.f || contract.iou >= 1.f ||
            !std::isfinite(scene_threshold) || scene_threshold < contract.confidence || scene_threshold > 1.f)
            return fail(RawYuvError::InvalidContract);
        const auto& source = *resources->source;
        const auto* frame  = static_cast<const AX_VIDEO_FRAME_T*>(source.backend_frame);
        if (!frame || frame->enImgFormat != AX_FORMAT_YUV420_SEMIPLANAR ||
            frame->stCompressInfo.enCompressMode != AX_COMPRESS_MODE_NONE || frame->u32Width != 1920 ||
            frame->u32Height != 1080 || frame->u32FrameSize != source.bytes ||
            frame->u64PhyAddr[0] != source.planes[0].physical_address ||
            frame->u64PhyAddr[1] != source.planes[1].physical_address ||
            frame->u32PicStride[0] != source.planes[0].stride ||
            frame->u32PicStride[1] != source.planes[1].stride ||
            frame->u64VirAddr[0] > std::numeric_limits<std::uintptr_t>::max() ||
            frame->u64VirAddr[1] > std::numeric_limits<std::uintptr_t>::max() ||
            (frame->s16CropWidth > 0 && (frame->s16CropX != 0 || frame->s16CropWidth != 1920)) ||
            (frame->s16CropHeight > 0 && (frame->s16CropY != 0 || frame->s16CropHeight != 1080)))
            return fail(RawYuvError::InvalidResources);
        const auto expected_output = (contract.capacity + 1) * 6 * sizeof(float);
        if (resources->result_bytes != expected_output ||
            resources->result_physical > std::numeric_limits<std::uint64_t>::max() - expected_output)
            return fail(RawYuvError::InvalidResources);
        AX_ENGINE_IO_INFO_T* info = nullptr;
        if (sdk.GetIOInfo(resources->engine, &info) != 0 || !info || info->nInputSize != 2 ||
            info->nOutputSize != 1 || !info->pInputs || !info->pOutputs)
            return fail(RawYuvError::InvalidMetadata);
        std::array<raw_yuv::Input, 2> inputs{};
        const std::array<const char*, 2> names{y_name, uv_name};
        for (std::size_t i = 0; i < inputs.size(); ++i) {
            const auto& meta  = info->pInputs[i];
            auto& input       = inputs[i];
            const auto& plane = source.planes[i];
            const auto rows   = i == 0 ? 1080U : 540U;
            if (!meta.pName || std::strcmp(meta.pName, names[i]) != 0 || !meta.pShape ||
                meta.nShapeSize != 4 || meta.eDataType != AX_ENGINE_DT_UINT8 ||
                meta.eLayout != expected_input_layout ||
                plane.stride > static_cast<std::size_t>(std::numeric_limits<AX_S32>::max()) / rows ||
                plane.bytes > std::numeric_limits<AX_U32>::max())
                return fail(RawYuvError::InvalidMetadata);
            for (std::size_t d = 0; d < 4; ++d) {
                if (meta.pShape[d] <= 0)
                    return fail(RawYuvError::InvalidMetadata);
                input.shape[d] = static_cast<std::size_t>(meta.pShape[d]);
            }
            input.model_bytes                = meta.nSize;
            input.uint8_with_expected_layout = true;
            input.io_physical                = plane.physical_address;
            input.io_bytes                   = plane.bytes;
            input.io_strides_present         = true;
            input.io_strides                 = {plane.stride * rows, plane.stride * rows, plane.stride, 1};
            if (!PositiveStride(meta, input))
                return fail(RawYuvError::InvalidMetadata);
        }
        const auto metadata = raw_yuv::Validate(source, inputs[0], inputs[1], contract.capacity,
                                                required_capacity, raw_yuv::Layout::PitchedNchw, color);
        if (metadata != raw_yuv::Error::None)
            return fail(RawYuvError::InvalidMetadata);
        for (std::size_t i = 0; i < 2; ++i) {
            const auto& plane = source.planes[i];
            if (!plane.bytes)
                continue;
            if (plane.bytes > std::numeric_limits<std::uint64_t>::max() - plane.physical_address ||
                Intersects(plane.physical_address, plane.bytes, resources->result_physical, expected_output))
                return fail(RawYuvError::InvalidResources);
        }
        const auto& output = info->pOutputs[0];
        const std::array<AX_S32, 3> out_shape{1, static_cast<AX_S32>(contract.capacity + 1), 6};
        const std::array<AX_S32, 3> out_elements{static_cast<AX_S32>((contract.capacity + 1) * 6), 6, 1};
        if (!output.pName || std::strcmp(output.pName, output_name) != 0 || !output.pShape ||
            output.nShapeSize != 3 || output.eDataType != AX_ENGINE_DT_FLOAT32 ||
            output.nSize != expected_output ||
            !std::equal(out_shape.begin(), out_shape.end(), output.pShape) ||
            !OutputStride(output, out_elements))
            return fail(RawYuvError::InvalidMetadata);

        auto pending        = std::make_shared<Pending>(std::move(resources));
        pending->keep_alive = pending;
        struct CompletionGuard {
            detail::NativeExecutionState& global;
            std::shared_ptr<Pending>& pending;
            bool complete{false};
            ~CompletionGuard() {
                if (complete)
                    pending->keep_alive.reset();
                else
                    global.poisoned = true;
            }
        } guard{global, pending};
        auto& r = *pending->resources;
        for (std::size_t i = 0; i < pending->inputs.size(); ++i) {
            auto& buffer = pending->inputs[i];
            for (std::size_t d = 0; d < 4; ++d)
                pending->input_strides[i][d] = static_cast<AX_S32>(inputs[i].io_strides[d]);
            buffer.phyAddr = inputs[i].io_physical;
            // A VDEC plane can have no CPU mapping. Never map image pixels to
            // manufacture an address; the SDK must accept the actual value.
            buffer.pVirAddr    = reinterpret_cast<void*>(static_cast<std::uintptr_t>(frame->u64VirAddr[i]));
            buffer.nSize       = static_cast<AX_U32>(inputs[i].io_bytes);
            buffer.pStride     = pending->input_strides[i].data();
            buffer.nStrideSize = 4;
        }
        pending->output_stride      = {static_cast<AX_S32>(expected_output), 24, 4};
        pending->output.phyAddr     = r.result_physical;
        pending->output.pVirAddr    = r.result_host;
        pending->output.nSize       = r.result_bytes;
        pending->output.pStride     = pending->output_stride.data();
        pending->output.nStrideSize = 3;
        pending->io.pInputs         = pending->inputs.data();
        pending->io.nInputSize      = 2;
        pending->io.pOutputs        = &pending->output;
        pending->io.nOutputSize     = 1;
        if (sdk.Run(r.engine, &pending->io) != 0)
            return fail(RawYuvError::DeviceCompletionUnknown);
        guard.complete = true;
        const detections_v1::TensorView view{
            r.result_host, r.result_bytes, {1, contract.capacity + 1, 6}, 3, true, true};
        auto parsed = convert(contract, view, source.width, source.height, scene_threshold);
        return {parsed.error == success ? RawYuvError::None : RawYuvError::InvalidResult, std::move(parsed)};
    }
};
}  // namespace cosmo::nn::axera
#endif
