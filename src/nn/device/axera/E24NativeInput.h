#pragma once

#ifdef COSMO_NN_USE_AXERA_BACKEND

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <utility>
#include <vector>

#include "ax_engine_api.h"
#include "ax_ivps_api.h"
#include "ax_sys_api.h"
#include "media/NativeVideoBuffer.h"
#include "nn/device/axera/axera_native_execution_state.h"
#include "nn/device/axera/axera_sys.h"

namespace cosmo::nn::axera {

inline constexpr AX_U32 kE24ImageWidth     = 1280;
inline constexpr AX_U32 kE24ImageHeight    = 736;
inline constexpr AX_U32 kE24ActiveHeight   = 720;
inline constexpr AX_U32 kE24InputBytes     = kE24ImageWidth * kE24ImageHeight * 3 / 2;
inline constexpr AX_U32 kE24OutputElements = 300 * 10;

inline bool E24NativeModelMetadata(const AX_ENGINE_IO_INFO_T& info) {
    if (info.nInputSize != 1 || !info.pInputs || info.nOutputSize != 1 || !info.pOutputs)
        return false;
    const auto dense_shape = [](const AX_ENGINE_IOMETA_T& meta, const auto& expected) {
        if (!meta.pShape || meta.nShapeSize != expected.size() ||
            !std::equal(expected.begin(), expected.end(), meta.pShape))
            return false;
        if (!meta.pStride)
            return true;
        int stride = 1;
        for (std::size_t axis = expected.size(); axis > 0; --axis) {
            if (meta.pStride[axis - 1] != stride)
                return false;
            stride *= expected[axis - 1];
        }
        return true;
    };
    const auto& input  = info.pInputs[0];
    const auto& output = info.pOutputs[0];
    const std::array<int, 4> packed_shape{1, kE24ImageHeight * 3 / 2, kE24ImageWidth, 1};
    const std::array<int, 3> result_shape{1, 300, 10};
    return input.eDataType == AX_ENGINE_DT_UINT8 && input.eLayout == AX_ENGINE_TENSOR_LAYOUT_NHWC &&
           input.pExtraMeta && input.pExtraMeta->eColorSpace == AX_ENGINE_CS_NV12 &&
           input.nSize == kE24InputBytes && dense_shape(input, packed_shape) &&
           output.eDataType == AX_ENGINE_DT_FLOAT32 && output.nSize == kE24OutputElements * sizeof(float) &&
           dense_shape(output, result_shape);
}

class E24NativeInputPool;

class E24NativeInput final {
public:
    E24NativeInput(const E24NativeInput&)            = delete;
    E24NativeInput& operator=(const E24NativeInput&) = delete;

    ~E24NativeInput() {
        if (buffer_.phyAddr && buffer_.pVirAddr && AX_SYS_MemFree(buffer_.phyAddr, buffer_.pVirAddr) != 0) {
            // Preserve the SYS reference when the SDK could not release its buffer.
            (void)sys_.release();
        }
    }

    [[nodiscard]] AX_ENGINE_IO_BUFFER_T Binding() const {
        return buffer_;
    }

    // Prevent reuse after a device call whose completion is unknown. The caller also
    // preserves its in-flight model/other resources in accordance with its SDK lifetime.
    static void HoldOnUncertainCompletion(const std::shared_ptr<E24NativeInput>& input) {
        if (input)
            input->quarantine_ = input;
    }

private:
    friend class E24NativeInputPool;
    E24NativeInput() = default;

    std::unique_ptr<AxeraSysLease> sys_;
    AX_ENGINE_IO_BUFFER_T buffer_{};
    std::shared_ptr<E24NativeInput> quarantine_;
    media::NativeVideoBufferPtr quarantined_source_;
};

enum class E24NativePrepareResult { Ready, Bypass, DeviceError };

struct E24IvpsTiming {
    std::uint64_t wait_ns{};
    std::uint64_t resize_ns{};
};

class E24NativeInputPool final {
public:
    explicit E24NativeInputPool(std::size_t max_slots) : max_slots_(max_slots) {}

    // Invoke with the selected VDEC frame still leased. Only Ready and Bypass allow
    // ordinary source release. On DeviceError, the SDK may still access the source.
    E24NativePrepareResult Prepare(const AX_VIDEO_FRAME_T& source, std::shared_ptr<E24NativeInput>& result) {
        return PrepareSource(source, result, {}, nullptr);
    }

    E24NativePrepareResult Prepare(const media::NativeVideoBufferPtr& source,
                                   std::shared_ptr<E24NativeInput>& result, E24IvpsTiming* timing = nullptr) {
        if (!source || source->memory_kind != media::NativeVideoMemoryKind::AxeraPhysical ||
            source->format != media::NativeVideoBufferFormat::NV12 || !source->Valid() ||
            !source->frame_witness || !source->frame_witness->Matches(*source) ||
            source->color_range != media::NativeVideoColorRange::Limited) {
            result.reset();
            return E24NativePrepareResult::Bypass;
        }
        const auto& frame = *static_cast<const AX_VIDEO_FRAME_T*>(source->backend_frame);
        if (frame.u64PhyAddr[0] != source->planes[0].physical_address ||
            frame.u64PhyAddr[1] != source->planes[1].physical_address ||
            frame.u32PicStride[0] != source->planes[0].stride ||
            frame.u32PicStride[1] != source->planes[1].stride || frame.u32FrameSize != source->bytes ||
            source->planes[0].bytes < static_cast<AX_U64>(frame.u32PicStride[0]) * kSourceHeight ||
            source->planes[1].bytes < static_cast<AX_U64>(frame.u32PicStride[1]) * (kSourceHeight / 2)) {
            result.reset();
            return E24NativePrepareResult::Bypass;
        }
        return PrepareSource(frame, result, source, timing);
    }

private:
    E24NativePrepareResult PrepareSource(const AX_VIDEO_FRAME_T& source,
                                         std::shared_ptr<E24NativeInput>& result,
                                         const media::NativeVideoBufferPtr& source_owner,
                                         E24IvpsTiming* timing) {
        result.reset();
        AX_VIDEO_FRAME_T active_source{};
        if (!VdecNv12(source, active_source))
            return E24NativePrepareResult::Bypass;
        auto slot = Acquire();
        if (!slot)
            return E24NativePrepareResult::Bypass;
        auto active_destination = ActiveDestination(slot->buffer_, active_source);
        AX_IVPS_ASPECT_RATIO_T aspect{};
        aspect.eMode = AX_IVPS_ASPECT_RATIO_STRETCH;
        auto& state  = detail::NativeState();
        // Separate IVPS submissions from synchronous NPU execution. Both
        // engines keep their own buffers alive, with a shared poison signal.
        using Clock          = std::chrono::steady_clock;
        const auto queued_at = Clock::now();
        static std::mutex ivps_mutex;
        std::lock_guard<std::mutex> ivps_lock(ivps_mutex);
        if (state.poisoned.load(std::memory_order_acquire))
            return E24NativePrepareResult::Bypass;
        static bool ivps_initialized = false;
        if (!ivps_initialized) {
            std::lock_guard<std::mutex> lock(state.mutex);
            if (state.poisoned.load(std::memory_order_acquire) || AX_IVPS_Init() != 0)
                return E24NativePrepareResult::Bypass;
            ivps_initialized = true;
        }

        const auto started       = Clock::now();
        const auto resize_status = AX_IVPS_CropResizeVpp(&active_source, &active_destination, &aspect);
        const auto ended         = Clock::now();
        if (timing) {
            timing->wait_ns =
                std::chrono::duration_cast<std::chrono::nanoseconds>(started - queued_at).count();
            timing->resize_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(ended - started).count();
        }
        if (resize_status != 0) {
            state.poisoned.store(true, std::memory_order_release);
            slot->quarantined_source_ = source_owner;
            E24NativeInput::HoldOnUncertainCompletion(slot);
            return E24NativePrepareResult::DeviceError;
        }
        if (state.poisoned.load(std::memory_order_acquire))
            return E24NativePrepareResult::Bypass;
        result = std::move(slot);
        return E24NativePrepareResult::Ready;
    }

    static constexpr AX_U32 kSourceWidth  = 1920;
    static constexpr AX_U32 kSourceHeight = 1080;
    static constexpr AX_U32 kYBytes       = kE24ImageWidth * kE24ImageHeight;
    static constexpr AX_U32 kPad          = (kE24ImageHeight - kE24ActiveHeight) / 2;

    static bool VdecNv12(const AX_VIDEO_FRAME_T& source, AX_VIDEO_FRAME_T& active) {
        if (source.enImgFormat != AX_FORMAT_YUV420_SEMIPLANAR ||
            source.stCompressInfo.enCompressMode != AX_COMPRESS_MODE_NONE ||
            source.stDynamicRange != AX_DYNAMIC_RANGE_SDR8 ||
            (source.stColorGamut != AX_COLOR_GAMUT_BT601 && source.stColorGamut != AX_COLOR_GAMUT_BT709) ||
            source.s16CropX != 0 || source.s16CropY != 0 || source.s16CropWidth < 0 ||
            source.s16CropHeight < 0 || !source.u64PhyAddr[0] || !source.u64PhyAddr[1] ||
            source.u32PicStride[0] < kSourceWidth || source.u32PicStride[1] < kSourceWidth ||
            source.u32Width < kSourceWidth || source.u32Height < kSourceHeight)
            return false;
        const auto width =
            source.s16CropWidth > 0 ? static_cast<AX_U32>(source.s16CropWidth) : source.u32Width;
        const auto height =
            source.s16CropHeight > 0 ? static_cast<AX_U32>(source.s16CropHeight) : source.u32Height;
        const auto y_bytes  = static_cast<AX_U64>(source.u32PicStride[0]) * kSourceHeight;
        const auto uv_bytes = static_cast<AX_U64>(source.u32PicStride[1]) * (kSourceHeight / 2);
        if (width != kSourceWidth || height != kSourceHeight || y_bytes + uv_bytes > source.u32FrameSize ||
            source.u64PhyAddr[0] > std::numeric_limits<AX_U64>::max() - y_bytes ||
            source.u64PhyAddr[1] > std::numeric_limits<AX_U64>::max() - uv_bytes ||
            (source.u64PhyAddr[0] < source.u64PhyAddr[1] + uv_bytes &&
             source.u64PhyAddr[1] < source.u64PhyAddr[0] + y_bytes))
            return false;
        active               = source;
        active.u32Width      = width;
        active.u32Height     = height;
        active.u32FrameSize  = static_cast<AX_U32>(y_bytes + uv_bytes);
        active.s16CropWidth  = 0;
        active.s16CropHeight = 0;
        return true;
    }

    static AX_VIDEO_FRAME_T ActiveDestination(const AX_ENGINE_IO_BUFFER_T& dest,
                                              const AX_VIDEO_FRAME_T& source) {
        AX_VIDEO_FRAME_T frame{};
        frame.u32Width                      = kE24ImageWidth;
        frame.u32Height                     = kE24ActiveHeight;
        frame.u32FrameSize                  = kE24ImageWidth * kE24ActiveHeight * 3 / 2;
        frame.u32PicStride[0]               = kE24ImageWidth;
        frame.u32PicStride[1]               = kE24ImageWidth;
        frame.enImgFormat                   = AX_FORMAT_YUV420_SEMIPLANAR;
        frame.stCompressInfo.enCompressMode = AX_COMPRESS_MODE_NONE;
        frame.stDynamicRange                = source.stDynamicRange;
        frame.stColorGamut                  = source.stColorGamut;
        frame.u64PhyAddr[0]                 = dest.phyAddr + kPad * kE24ImageWidth;
        frame.u64PhyAddr[1]                 = dest.phyAddr + kYBytes + kPad * kE24ImageWidth / 2;
        const auto address                  = reinterpret_cast<std::uintptr_t>(dest.pVirAddr);
        frame.u64VirAddr[0]                 = address + kPad * kE24ImageWidth;
        frame.u64VirAddr[1]                 = address + kYBytes + kPad * kE24ImageWidth / 2;
        return frame;
    }

    std::shared_ptr<E24NativeInput> Acquire() {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& existing : slots_) {
            if (existing.use_count() == 1)
                return existing;
        }
        if (alloc_faulted_ || slots_.size() == max_slots_)
            return {};
        try {
            auto slot  = std::shared_ptr<E24NativeInput>(new E24NativeInput);
            slot->sys_ = std::make_unique<AxeraSysLease>();
            if (slot->sys_->Acquire() != 0)
                return {};
            auto& buffer     = slot->buffer_;
            buffer.nSize     = kE24InputBytes;
            const int status = AX_SYS_MemAlloc(&buffer.phyAddr, &buffer.pVirAddr, buffer.nSize, 4096,
                                               reinterpret_cast<const AX_S8*>("cosmo-e24-nv12"));
            if (status != 0 || !buffer.phyAddr || !buffer.pVirAddr) {
                if (buffer.phyAddr || buffer.pVirAddr) {
                    alloc_faulted_ = true;
                    E24NativeInput::HoldOnUncertainCompletion(slot);
                }
                return {};
            }
            auto* y  = static_cast<std::uint8_t*>(buffer.pVirAddr);
            auto* uv = y + kYBytes;
            std::memset(y, 114, kE24ImageWidth * kPad);
            std::memset(y + kE24ImageWidth * (kE24ImageHeight - kPad), 114, kE24ImageWidth * kPad);
            std::memset(uv, 128, kE24ImageWidth * kPad / 2);
            std::memset(uv + kE24ImageWidth * (kE24ImageHeight - kPad) / 2, 128, kE24ImageWidth * kPad / 2);
            slots_.push_back(slot);
            return slot;
        } catch (const std::bad_alloc&) {
            return {};
        }
    }

    const std::size_t max_slots_;
    std::mutex mutex_;
    std::vector<std::shared_ptr<E24NativeInput>> slots_;
    bool alloc_faulted_{false};
};

}  // namespace cosmo::nn::axera

#endif
