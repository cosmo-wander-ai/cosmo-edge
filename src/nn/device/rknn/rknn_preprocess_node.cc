#if defined(COSMO_NN_USE_RKNN_BACKEND) && defined(COSMO_MEDIA_USE_ROCKCHIP_BACKEND)

#include "nn/device/rknn/rknn_preprocess_node.h"

#include <rga/im2d.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>
#include <string>

#include "media/RockchipRgaBuffer.h"
#include "nn/core/inference_pipeline_metrics.h"
#include "nn/device/rknn/rknn_net_node.h"
#include "nn/node/node_type_utils.h"
#include "nn/utils/nv12_bt709_full_to_rgb.h"
#include "nn/utils/op.h"
#include "nn/utils/resize_geometry.h"
#include "nn/utils/rknn_bound_input_padding.h"
#include "util/Log.h"

namespace cosmo::nn {
namespace {

    using MetricsClock = std::chrono::steady_clock;

    constexpr float kNormalizeScale = 0.00392157f;
    // RGA2-class cores support at least 1/8..8 scaling. Keep the shared path
    // conservative so the same implementation is valid on both RGA2 and RGA3.
    constexpr int kConservativeRgaScaleLimit = 8;
#if defined(COSMO_RKNN_TARGET_CHIP)
    const bool kIsRk3588 = UseCpuRgaBoundPadding(COSMO_RKNN_TARGET_CHIP);
#else
    constexpr bool kIsRk3588 = false;
#endif

    uint64_t ElapsedNanoseconds(MetricsClock::time_point started_at) {
        return static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(MetricsClock::now() - started_at).count());
    }

    bool EnvironmentFlag(const char* name, bool default_value) {
        const char* raw = std::getenv(name);
        if (!raw || *raw == '\0')
            return default_value;
        std::string value(raw);
        std::transform(value.begin(), value.end(), value.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (value == "0" || value == "false" || value == "off" || value == "no")
            return false;
        if (value == "1" || value == "true" || value == "on" || value == "yes")
            return true;
        return default_value;
    }

    void LogRgaFallbackOnce(IM_STATUS status) {
        static std::atomic_flag logged = ATOMIC_FLAG_INIT;
        if (!logged.test_and_set(std::memory_order_relaxed)) {
            LOG_WARN(
                "RKNN detector RGA preprocessing failed with status {} ({}); disabling this node's RGA path "
                "and using CPU fallback",
                status, imStrError_t(status));
        }
    }

    void LogRgaBoundInputFallbackOnce(const std::string& reason) {
        static std::atomic_flag logged = ATOMIC_FLAG_INIT;
        if (!logged.test_and_set(std::memory_order_relaxed)) {
            LOG_WARN("RKNN RGA bound input unavailable; retaining the host preprocessing path: {}", reason);
        }
    }

    void LogMppDmaBufFallbackOnce(IM_STATUS status) {
        static std::atomic_flag logged = ATOMIC_FLAG_INIT;
        if (!logged.test_and_set(std::memory_order_relaxed)) {
            LOG_WARN(
                "RKNN MPP DMA-BUF preprocessing failed with status {} ({}); disabling this node's native "
                "path and using host RGA input",
                status, imStrError_t(status));
        }
    }

    void LogRgaSmallFrameFallbackOnce() {
        static std::atomic_flag logged = ATOMIC_FLAG_INIT;
        if (!logged.test_and_set(std::memory_order_relaxed)) {
            LOG_WARN(
                "RKNN BT.709-full NV12 small-frame preprocessing unavailable; using packed host fallback");
        }
    }

    void LogRgaCropResizeFallbackOnce(IM_STATUS status) {
        static std::atomic_flag logged = ATOMIC_FLAG_INIT;
        if (!logged.test_and_set(std::memory_order_relaxed)) {
            LOG_WARN(
                "RKNN RGA crop-resize failed with status {} ({}); disabling this node's RGA path and using "
                "CPU fallback",
                status, imStrError_t(status));
        }
    }

    void LogRgaBt2020DowngradeOnce() {
        static std::atomic_flag logged = ATOMIC_FLAG_INIT;
        if (!logged.test_and_set(std::memory_order_relaxed)) {
            LOG_WARN(
                "This librga header/runtime does not expose BT.2020 full-CSC modes; "
                "using the matching BT.709 range for this source");
        }
    }

    void BilinearResizePacked(const uint8_t* source, int source_width, int source_height, int channels,
                              uint8_t* destination, int destination_width, int destination_height,
                              size_t destination_stride, bool swap_red_blue) {
        const float x_ratio = static_cast<float>(source_width) / destination_width;
        const float y_ratio = static_cast<float>(source_height) / destination_height;
        for (int destination_y = 0; destination_y < destination_height; ++destination_y) {
            const float source_y = (destination_y + 0.5f) * y_ratio - 0.5f;
            int y0               = static_cast<int>(source_y);
            const float y_part   = source_y - y0;
            y0                   = std::max(0, std::min(y0, source_height - 1));
            const int y1         = std::min(y0 + 1, source_height - 1);
            for (int destination_x = 0; destination_x < destination_width; ++destination_x) {
                const float source_x = (destination_x + 0.5f) * x_ratio - 0.5f;
                int x0               = static_cast<int>(source_x);
                const float x_part   = source_x - x0;
                x0                   = std::max(0, std::min(x0, source_width - 1));
                const int x1         = std::min(x0 + 1, source_width - 1);
                for (int channel = 0; channel < channels; ++channel) {
                    const int source_channel = swap_red_blue && channel != 1 ? 2 - channel : channel;
                    const float v00          = source[(y0 * source_width + x0) * channels + source_channel];
                    const float v01          = source[(y0 * source_width + x1) * channels + source_channel];
                    const float v10          = source[(y1 * source_width + x0) * channels + source_channel];
                    const float v11          = source[(y1 * source_width + x1) * channels + source_channel];
                    const float value = v00 * (1 - x_part) * (1 - y_part) + v01 * x_part * (1 - y_part) +
                                        v10 * (1 - x_part) * y_part + v11 * x_part * y_part;
                    destination[static_cast<size_t>(destination_y) * destination_stride +
                                destination_x * channels + channel] =
                        static_cast<uint8_t>(std::min(255.0f, std::max(0.0f, value + 0.5f)));
                }
            }
        }
    }

    size_t PackedByteCount(int width, int height) {
        if (width <= 0 || height <= 0)
            return 0;
        const auto pixels = static_cast<size_t>(width) * static_cast<size_t>(height);
        if (pixels > std::numeric_limits<size_t>::max() / 3)
            return 0;
        return pixels * 3;
    }

    bool IsRgaDeviceUnavailable(IM_STATUS status) {
#if defined(RGA_CURRENT_API_VERSION) && RGA_CURRENT_API_VERSION >= 0x010a0600
        if (status == IM_STATUS_NO_SESSION)
            return true;
#endif
        return status == IM_STATUS_ERROR_VERSION;
    }

    bool MatchesNativeImage(const BlobHandle::NativeImage& native, int width, int height) {
        return RknnMppDmaBufEnabled() && native.Valid() && native.width == width && native.height == height &&
               (native.format == IMAGE_NV12 || native.format == IMAGE_I420) &&
               static_cast<uint64_t>(native.width_stride) * native.height_stride * 3 / 2 <= native.bytes;
    }

    IM_STATUS FillRgaPadding(rga_buffer_t target, uint8_t padding, RknnBoundInputProvider* bound_input,
                             uint64_t generation) {
        if (bound_input && kIsRk3588) {
            std::string reason;
            if (!bound_input->FillRgaBoundInput(generation, {padding, padding, padding}, reason)) {
                LogRgaBoundInputFallbackOnce(reason);
                return IM_STATUS_FAILED;
            }
            static std::atomic_flag logged = ATOMIC_FLAG_INIT;
            if (!logged.test_and_set(std::memory_order_relaxed))
                LOG_INFO("RKNN bound RGB padding uses CPU fill and cache sync before RGA resize/CSC");
            return IM_STATUS_SUCCESS;
        }
        const auto started = MetricsClock::now();
        const im_rect region{0, 0, target.width, target.height};
        const auto status = imfill_t(target, region, padding * 0x010101, 1);
        GetInferencePipelineMetrics().RecordRknnRgaFill(ElapsedNanoseconds(started));
        return status;
    }

    void FillHostPadding(uint8_t* output, int width, int height, const Rect& region, uint8_t padding) {
        // RK3588 fill selects RGA2, which cannot access host pages above 4 GiB.
        // The resize has completed synchronously, so only touch the padding.
        const auto started  = MetricsClock::now();
        const size_t stride = static_cast<size_t>(width) * 3;
        const size_t left   = static_cast<size_t>(region.x) * 3;
        const size_t right  = static_cast<size_t>(region.x + region.width) * 3;
        const int bottom    = region.y + region.height;
        std::memset(output, padding, static_cast<size_t>(region.y) * stride);
        if (left > 0 || right < stride) {
            for (int row = region.y; row < bottom; ++row) {
                auto* line = output + static_cast<size_t>(row) * stride;
                std::memset(line, padding, left);
                std::memset(line + right, padding, stride - right);
            }
        }
        std::memset(output + static_cast<size_t>(bottom) * stride, padding,
                    static_cast<size_t>(height - bottom) * stride);
        GetInferencePipelineMetrics().RecordRknnCpuHostPadding(ElapsedNanoseconds(started));
    }

    IM_COLOR_SPACE_MODE ResolveRgaYuvColorSpace(const BlobHandle::NativeImage& image) {
        const bool full_range = image.color_range == NativeImageColorRange::Full;
        switch (image.color_space) {
            case NativeImageColorSpace::Bt709:
                return full_range ? IM_YUV_BT709_FULL_RANGE : IM_YUV_BT709_LIMIT_RANGE;
            case NativeImageColorSpace::Bt2020:
                if (!media::RockchipRgaHasBt2020ColorSpace())
                    LogRgaBt2020DowngradeOnce();
                return media::RockchipRgaBt2020ColorSpace(full_range);
            case NativeImageColorSpace::Bt601:
            case NativeImageColorSpace::Unspecified:
            default:
                return full_range ? IM_YUV_BT601_FULL_RANGE : IM_YUV_BT601_LIMIT_RANGE;
        }
    }

    bool IsNv12Bt709Full(const BlobHandle::NativeImage& image) {
        return image.format == IMAGE_NV12 && image.color_space == NativeImageColorSpace::Bt709 &&
               image.color_range == NativeImageColorRange::Full;
    }

    bool ResizeNv12Bt709FullToHost(const BlobHandle::NativeImage& image, rga_buffer_handle_t source_handle,
                                   const Rect& crop, uint8_t* output, int output_width, int output_height,
                                   const Rect& region, uint8_t padding) {
        if (!IsNv12Bt709Full(image) || !image.Valid() || !source_handle || !output || output_width <= 0 ||
            output_height <= 0 || (image.width & 1) || (image.height & 1) || (image.width_stride & 3) ||
            (image.height_stride & 1)) {
            return false;
        }
        const auto native_bytes = static_cast<uint64_t>(image.width_stride) * image.height_stride * 3 / 2;
        if (native_bytes > image.bytes)
            return false;
        const int width  = region.width;
        const int height = region.height;
        // Preserve the exact existing letterbox geometry. An odd NV12 extent
        // takes the established host path rather than silently rounding its ROI.
        if (width <= 0 || height <= 0 || width > output_width || height > output_height || (width & 1) ||
            (height & 1) || width > std::numeric_limits<int>::max() - 3) {
            return false;
        }
        const int stride   = (width + 3) & ~3;
        const size_t bytes = static_cast<size_t>(stride) * height * 3 / 2;
        try {
            // A per-call owner keeps concurrent frames and changing geometry
            // independent. Release the imported handle before its vector storage.
            std::vector<uint8_t> small_nv12(bytes);
            media::ScopedRgaBufferHandle small_handle(small_nv12.data(), small_nv12.size());
            if (!small_handle)
                return false;
            auto source = wrapbuffer_handle_t(source_handle, image.width, image.height, image.width_stride,
                                              image.height_stride, RK_FORMAT_YCbCr_420_SP);
            auto target = wrapbuffer_handle_t(small_handle.Get(), width, height, stride, height,
                                              RK_FORMAT_YCbCr_420_SP);
            // Same-format resize preserves YUV sample values. CSC and its true
            // full-range colorimetry are handled explicitly on the small image.
            imsetColorSpace(&source, IM_COLOR_SPACE_DEFAULT);
            imsetColorSpace(&target, IM_COLOR_SPACE_DEFAULT);
            const im_rect source_rect{crop.x, crop.y, crop.width, crop.height};
            const im_rect target_rect{0, 0, width, height};
            if (!media::RockchipRgaSucceeded(
                    imcheck_t(source, target, {}, source_rect, target_rect, {}, IM_SYNC)))
                return false;
            const auto resize_started = MetricsClock::now();
            const IM_STATUS status    = improcess(source, target, {}, source_rect, target_rect, {}, IM_SYNC);
            const auto resize_ns      = ElapsedNanoseconds(resize_started);
            if (!media::RockchipRgaSucceeded(status))
                return false;
            // IM_SYNC completes writes to this imported host allocation before
            // CPU access. Never expose it as an RKNN-bound DMA input frame.
            const auto csc_started = MetricsClock::now();
            const bool converted =
                Nv12Bt709FullToRgb({small_nv12.data(), small_nv12.size(), width, height,
                                    static_cast<size_t>(stride), static_cast<size_t>(height)},
                                   {output, PackedByteCount(output_width, output_height), output_width,
                                    output_height, static_cast<size_t>(output_width) * 3},
                                   region.x, region.y, {padding, padding, padding});
            if (!converted)
                return false;
            GetInferencePipelineMetrics().RecordRknnRgaSmallFrameCsc(ElapsedNanoseconds(csc_started));
            GetInferencePipelineMetrics().RecordRknnRgaSmallFrameResize(resize_ns);
            return true;
        } catch (const std::bad_alloc&) {
            return false;
        }
    }

    int NextRgaScaleDimension(int current, int target) {
        if (current <= 0 || target <= 0)
            return 0;
        const auto current_wide = static_cast<int64_t>(current);
        const auto target_wide  = static_cast<int64_t>(target);
        if (target_wide > current_wide * kConservativeRgaScaleLimit) {
            return static_cast<int>(current_wide * kConservativeRgaScaleLimit);
        }
        if (current_wide > target_wide * kConservativeRgaScaleLimit) {
            return static_cast<int>((current_wide + kConservativeRgaScaleLimit - 1) /
                                    kConservativeRgaScaleLimit);
        }
        return target;
    }

    bool RunStagedRgaResize(rga_buffer_t source, const im_rect& source_rect,
                            IM_COLOR_SPACE_MODE source_color_space, rga_buffer_t target,
                            const im_rect& target_rect, IM_STATUS& last_status, bool is_crop) {
        if (source_rect.width <= 0 || source_rect.height <= 0 || target_rect.width <= 0 ||
            target_rect.height <= 0) {
            last_status = IM_STATUS_INVALID_PARAM;
            return false;
        }

        std::vector<std::pair<int, int>> destinations;
        int current_width  = source_rect.width;
        int current_height = source_rect.height;
        while (true) {
            const int next_width  = NextRgaScaleDimension(current_width, target_rect.width);
            const int next_height = NextRgaScaleDimension(current_height, target_rect.height);
            if (next_width <= 0 || next_height <= 0 || destinations.size() >= 16) {
                last_status = IM_STATUS_INVALID_PARAM;
                return false;
            }
            destinations.emplace_back(next_width, next_height);
            if (next_width == target_rect.width && next_height == target_rect.height)
                break;
            if (next_width == current_width && next_height == current_height) {
                last_status = IM_STATUS_INVALID_PARAM;
                return false;
            }
            current_width  = next_width;
            current_height = next_height;
        }

        std::vector<std::vector<uint8_t>> stage_storage;
        std::vector<media::ScopedRgaBufferHandle> stage_handles;
        std::vector<rga_buffer_t> stage_buffers;
        if (destinations.size() > 1) {
            const auto intermediate_count = destinations.size() - 1;
            stage_storage.reserve(intermediate_count);
            stage_handles.reserve(intermediate_count);
            stage_buffers.reserve(intermediate_count);
        }

        rga_buffer_t current_source = source;
        im_rect current_source_rect = source_rect;
        for (size_t index = 0; index < destinations.size(); ++index) {
            const bool final_pass = index + 1 == destinations.size();
            rga_buffer_t current_target{};
            im_rect current_target_rect{};
            if (final_pass) {
                current_target      = target;
                current_target_rect = target_rect;
            } else {
                const int width  = destinations[index].first;
                const int height = destinations[index].second;
                if (width > std::numeric_limits<int>::max() - 15) {
                    last_status = IM_STATUS_INVALID_PARAM;
                    return false;
                }
                const int stride   = (width + 15) & ~15;
                const size_t bytes = PackedByteCount(stride, height);
                stage_storage.emplace_back(bytes);
                stage_handles.emplace_back();
                if (bytes == 0 || !stage_handles.back().ImportVirtual(stage_storage.back().data(), bytes)) {
                    last_status = IM_STATUS_OUT_OF_MEMORY;
                    return false;
                }
                stage_buffers.push_back(wrapbuffer_handle_t(stage_handles.back().Get(), width, height, stride,
                                                            height, RK_FORMAT_RGB_888));
                current_target      = stage_buffers.back();
                current_target_rect = {0, 0, width, height};
            }

            // A later RGB stage must not inherit the previous YUV CSC mode.
            imsetColorSpace(&current_source, IM_COLOR_SPACE_DEFAULT);
            imsetColorSpace(&current_target, IM_COLOR_SPACE_DEFAULT);
            if (index == 0 && source_color_space != IM_COLOR_SPACE_DEFAULT) {
                media::SetRgaYuvToRgbColorSpace(current_source, current_target, source_color_space);
            }
            const im_rect empty_rect{};
            const rga_buffer_t empty_buffer{};
            last_status = imcheck_t(current_source, current_target, empty_buffer, current_source_rect,
                                    current_target_rect, empty_rect, IM_SYNC);
            if (!media::RockchipRgaSucceeded(last_status))
                return false;
            const auto started = MetricsClock::now();
            last_status        = improcess(current_source, current_target, empty_buffer, current_source_rect,
                                           current_target_rect, empty_rect, IM_SYNC);
            const bool success = media::RockchipRgaSucceeded(last_status);
            if (is_crop)
                GetInferencePipelineMetrics().RecordRknnRgaCropResize(ElapsedNanoseconds(started), success);
            else
                GetInferencePipelineMetrics().RecordRknnRgaResizeColor(ElapsedNanoseconds(started));
            if (!success)
                return false;

            current_source      = current_target;
            current_source_rect = {0, 0, current_target_rect.width, current_target_rect.height};
        }
        return true;
    }

}  // namespace

bool RknnFastPreprocessEnabled() {
    return EnvironmentFlag("COSMO_RKNN_FAST_PREPROCESS", true);
}

bool RknnForceRgaFailure() {
    return EnvironmentFlag("COSMO_RKNN_RGA_FORCE_FAIL", false);
}

bool RknnMppDmaBufEnabled() {
    return EnvironmentFlag("COSMO_RKNN_MPP_DMABUF", true);
}

bool RknnForceMppDmaBufFailure() {
    return EnvironmentFlag("COSMO_RKNN_MPP_DMABUF_FORCE_FAIL", false);
}

bool IsRknnNativeNormalizeContract(const std::vector<float>& mean, const std::vector<float>& std_dev,
                                   float scale, const DimsVector& input_dims) {
    if (input_dims.size() != 4 || input_dims[0] != 1 || input_dims[1] <= 0 || input_dims[2] <= 0 ||
        input_dims[3] != 3 || mean.size() < 3 || !std_dev.empty() ||
        std::fabs(scale - kNormalizeScale) > 1e-8f) {
        return false;
    }
    return std::all_of(mean.begin(), mean.begin() + 3, [](float value) { return std::fabs(value) <= 1e-8f; });
}

void MapPackedU8ToNativeInt8(const uint8_t* source, int8_t* destination, size_t pixels, bool swap_red_blue) {
    if (!source || !destination)
        return;
    for (size_t pixel = 0; pixel < pixels; ++pixel) {
        const auto source_offset      = pixel * 3;
        const auto destination_offset = pixel * 3;
        for (size_t channel = 0; channel < 3; ++channel) {
            const size_t source_channel = swap_red_blue && channel != 1 ? 2 - channel : channel;
            destination[destination_offset + channel] =
                static_cast<int8_t>(static_cast<int>(source[source_offset + source_channel]) - 128);
        }
    }
}

RknnResizeNode::RknnResizeNode() : Node() {
    node_type     = NodeType::NODE_RESIZE;
    name          = NodeTypeUtils::NodeTypeToStr(NODE_RESIZE).append("_0");
    one_blob_only = true;
}

RknnResizeNode::~RknnResizeNode() {
    ReleaseRgaBoundTarget();
}

void RknnResizeNode::LoadParam(Op* op) {
    const auto* resize = dynamic_cast<Resize*>(op);
    if (!resize)
        return;
    if (resize->dsize.size() >= 2) {
        out_height_ = resize->dsize[0];
        out_width_  = resize->dsize[1];
    }
    gravity_       = resize->gravity;
    padding_value_ = resize->color.empty() ? 114 : static_cast<uint8_t>(resize->color.front());
}

DeviceType RknnResizeNode::GetTopBlobDeviceType() {
    return DeviceType::DEVICE_NAIVE;
}

Status RknnResizeNode::InferTopShapes() {
    if (out_height_ <= 0 || out_width_ <= 0)
        return Status(COSMO_NN_ERR_INVALID_CFG, "RKNN resize output dimensions must be positive");
    shared_resource->net_input_w = out_width_;
    shared_resource->net_input_h = out_height_;
    top_blob_shapes              = {{1, out_height_, out_width_, 3}};
    top_blob_data_types          = {DataType::DATA_TYPE_UINT8};
    return Status{COSMO_NN_OK};
}

size_t RknnResizeNode::GetBottomCount() {
    return 1;
}

size_t RknnResizeNode::GetTopCount() {
    return 1;
}

void RknnResizeNode::ReleaseRgaBoundTarget() {
    if (rga_bound_target_handle_ != 0) {
        releasebuffer_handle(static_cast<rga_buffer_handle_t>(rga_bound_target_handle_));
        rga_bound_target_handle_     = 0;
        rga_bound_target_generation_ = 0;
    }
}

bool RknnResizeNode::AcquireRgaBoundTarget(uint32_t& handle) {
    handle                = 0;
    const bool enabled    = RknnRgaBoundInputEnabled();
    const bool compatible = shared_resource && shared_resource->rknn_bound_input_preprocess_compatible;
    const bool provider_available = shared_resource && shared_resource->rknn_bound_input_provider;
    if (rga_bound_target_unavailable_ || !enabled || !compatible || !provider_available) {
        if (!rga_bound_target_unavailable_ && !rga_bound_guard_logged_) {
            LOG_WARN(
                "RKNN detector cannot acquire the RGA-bound input target: enabled={} shared_resource={} "
                "preprocess_compatible={} provider={}",
                enabled, shared_resource != nullptr, compatible, provider_available);
            rga_bound_guard_logged_ = true;
        }
        return false;
    }
    std::string reason;
    auto* provider = shared_resource->rknn_bound_input_provider;
    if (!provider->EnsureRgaBoundInput(out_height_, out_width_, reason)) {
        rga_bound_target_unavailable_ = true;
        LogRgaBoundInputFallbackOnce(reason);
        return false;
    }
    const auto& target = shared_resource->rknn_bound_input_target;
    if (target.owner != provider || !target.Matches(out_height_, out_width_) ||
        target.bytes > static_cast<size_t>(std::numeric_limits<int>::max())) {
        rga_bound_target_unavailable_ = true;
        LogRgaBoundInputFallbackOnce("provider returned an incompatible target");
        return false;
    }
    if (rga_bound_target_handle_ != 0 && rga_bound_target_generation_ == target.generation) {
        handle = rga_bound_target_handle_;
        return true;
    }
    ReleaseRgaBoundTarget();
    const auto import_started = MetricsClock::now();
    const auto imported       = importbuffer_fd(target.fd, static_cast<int>(target.bytes));
    GetInferencePipelineMetrics().RecordRknnRgaBoundInputImport(ElapsedNanoseconds(import_started),
                                                                imported != 0);
    if (imported == 0) {
        rga_bound_target_unavailable_ = true;
        GetInferencePipelineMetrics().RecordRknnRgaFailure();
        LogRgaBoundInputFallbackOnce("RGA could not import the RKNN DMA-BUF fd");
        return false;
    }
    rga_bound_target_handle_     = static_cast<uint32_t>(imported);
    rga_bound_target_generation_ = target.generation;
    handle                       = rga_bound_target_handle_;
    return true;
}

bool RknnResizeNode::ResizeWithRga(Blob& bottom, Blob& top, const Rect& region, bool allow_bound_target) {
    if (rga_unavailable_ || region.empty())
        return false;
    if (RknnForceRgaFailure()) {
        rga_unavailable_ = true;
        GetInferencePipelineMetrics().RecordRknnRgaFailure();
        return false;
    }

    const auto& bottom_desc = bottom.GetBlobDesc();
    if (bottom_desc.image_format != IMAGE_BGR && bottom_desc.image_format != IMAGE_RGB) {
        GetInferencePipelineMetrics().RecordRknnRgaFailure();
        return false;
    }

    const int source_height = bottom_desc.dims[1];
    const int source_width  = bottom_desc.dims[2];
    const auto target_size  = PackedByteCount(out_width_, out_height_);
    auto* const output      = static_cast<uint8_t*>(top.GetHandle().base);
    const im_rect source_rect{0, 0, source_width, source_height};
    const im_rect target_rect{region.x, region.y, region.width, region.height};
    const bool needs_padding = region.width != out_width_ || region.height != out_height_;

    media::ScopedRgaBufferHandle host_target_handle;
    uint32_t bound_handle     = 0;
    bool checked_bound_target = false;
    IM_STATUS last_status     = IM_STATUS_FAILED;
    const auto run_resize     = [&](rga_buffer_t source, IM_COLOR_SPACE_MODE color_space, bool bound_target) {
        const int target_stride =
            bound_target ? shared_resource->rknn_bound_input_target.width_stride : out_width_;
        const auto target_handle =
            bound_target ? static_cast<rga_buffer_handle_t>(bound_handle) : host_target_handle.Get();
        auto target = wrapbuffer_handle_t(target_handle, out_width_, out_height_, target_stride, out_height_,
                                              RK_FORMAT_RGB_888);
        if (color_space != IM_COLOR_SPACE_DEFAULT)
            media::SetRgaYuvToRgbColorSpace(source, target, color_space);

        const bool needs_staging =
            NextRgaScaleDimension(source_rect.width, target_rect.width) != target_rect.width ||
            NextRgaScaleDimension(source_rect.height, target_rect.height) != target_rect.height;
        bool single_pass = false;
        last_status      = IM_STATUS_NOERROR;
        // A larger ratio on RK3588 can pass librga's check for RGA2, then
        // fail at submission when only RGA3 can access the buffers.
        if (!kIsRk3588 || !needs_staging) {
            last_status = imcheck_t(source, target, {}, source_rect, target_rect, {}, IM_SYNC);
            single_pass = media::RockchipRgaSucceeded(last_status);
        }
        if (!single_pass && (!needs_staging || IsRgaDeviceUnavailable(last_status)))
            return false;

        const bool cpu_host_padding = needs_padding && kIsRk3588 && !bound_target;
        if (needs_padding && !cpu_host_padding) {
            auto* provider = bound_target ? shared_resource->rknn_bound_input_provider : nullptr;
            last_status    = FillRgaPadding(target, padding_value_, provider, rga_bound_target_generation_);
            if (!media::RockchipRgaSucceeded(last_status))
                return false;
        }

        bool success = false;
        if (single_pass) {
            const auto resize_started = MetricsClock::now();
            last_status               = improcess(source, target, {}, source_rect, target_rect, {}, IM_SYNC);
            GetInferencePipelineMetrics().RecordRknnRgaResizeColor(ElapsedNanoseconds(resize_started));
            success = media::RockchipRgaSucceeded(last_status);
        }
        if (!success && needs_staging && !IsRgaDeviceUnavailable(last_status)) {
            success =
                RunStagedRgaResize(source, source_rect, color_space, target, target_rect, last_status, false);
        }
        if (success && cpu_host_padding) {
            host_target_handle.Reset();
            FillHostPadding(output, out_width_, out_height_, region, padding_value_);
        }
        return success;
    };

    const auto resize_to_target = [&](rga_buffer_t source, IM_COLOR_SPACE_MODE color_space) {
        if (!checked_bound_target) {
            checked_bound_target = true;
            if (allow_bound_target)
                (void)AcquireRgaBoundTarget(bound_handle);
        }
        if (bound_handle) {
            if (run_resize(source, color_space, true)) {
                auto& target = shared_resource->rknn_bound_input_target;
                if (target.owner == shared_resource->rknn_bound_input_provider &&
                    target.generation == rga_bound_target_generation_) {
                    target.frame_ready = true;
                    return true;
                }
                last_status = IM_STATUS_INVALID_PARAM;
            }
            if (IsRgaDeviceUnavailable(last_status))
                return false;
        }
        if (!host_target_handle && !host_target_handle.ImportVirtual(output, target_size)) {
            last_status = IM_STATUS_OUT_OF_MEMORY;
            return false;
        }
        return run_resize(source, color_space, false);
    };

    const auto fail_rga = [&]() {
        rga_unavailable_ = IsRgaDeviceUnavailable(last_status);
        GetInferencePipelineMetrics().RecordRknnRgaFailure();
        if (rga_unavailable_)
            LogRgaFallbackOnce(last_status);
        return false;
    };
    const auto bottom_handle   = bottom.GetHandle(false);
    const auto& native         = bottom_handle.native_image;
    const bool native_matches  = MatchesNativeImage(native, source_width, source_height);
    const bool full_range_nv12 = IsNv12Bt709Full(native);
    if (native_matches && full_range_nv12) {
        const std::array<int, 4> geometry{native.width, native.height, native.width_stride,
                                          native.height_stride};
        if (geometry != native_full_range_geometry_) {
            native_full_range_geometry_        = geometry;
            native_full_range_csc_unavailable_ = false;
            native_small_frame_unavailable_    = false;
        }
    }

    if (native_matches && !native_rga_unavailable_ &&
        (!full_range_nv12 || !native_full_range_csc_unavailable_ || !native_small_frame_unavailable_)) {
        const bool force_native_failure = RknnForceMppDmaBufFailure();
        media::ScopedRgaBufferHandle native_source_handle;
        if (!force_native_failure) {
            const auto import_started = MetricsClock::now();
            native_source_handle.ImportFd(native.fd, native.bytes);
            GetInferencePipelineMetrics().RecordRknnMppDmaBufImport(ElapsedNanoseconds(import_started),
                                                                    bool(native_source_handle));
        }
        if (native_source_handle) {
            if (!full_range_nv12 || !native_full_range_csc_unavailable_) {
                const int format =
                    native.format == IMAGE_NV12 ? RK_FORMAT_YCbCr_420_SP : RK_FORMAT_YCbCr_420_P;
                auto source = wrapbuffer_handle_t(native_source_handle.Get(), native.width, native.height,
                                                  native.width_stride, native.height_stride, format);
                if (resize_to_target(source, ResolveRgaYuvColorSpace(native))) {
                    GetInferencePipelineMetrics().RecordRknnMppDmaBufFrame(native.bytes);
                    return true;
                }
            }
            if (IsRgaDeviceUnavailable(last_status))
                return fail_rga();

            if (full_range_nv12) {
                if (!native_full_range_csc_unavailable_) {
                    LOG_WARN(
                        "RKNN native BT.709-full NV12 CSC failed with status {} ({}); trying NV12 resize "
                        "and CPU full-range CSC on the small host image",
                        last_status, imStrError_t(last_status));
                    native_full_range_csc_unavailable_ = true;
                }
                if (ResizeNv12Bt709FullToHost(native, native_source_handle.Get(),
                                              {0, 0, source_width, source_height}, output, out_width_,
                                              out_height_, region, padding_value_)) {
                    GetInferencePipelineMetrics().RecordRknnMppDmaBufFrame(native.bytes);
                    return true;
                }
                native_small_frame_unavailable_ = true;
                LogRgaSmallFrameFallbackOnce();
            }
        } else if (force_native_failure) {
            native_rga_unavailable_ = true;
            LogMppDmaBufFallbackOnce(last_status);
        }
        GetInferencePipelineMetrics().RecordRknnMppDmaBufFallback();
    }

    // Materialize packed host pixels only when the native source cannot be used.
    const auto host_handle = bottom.GetHandle();
    if (!host_handle.base)
        return false;
    media::ScopedRgaBufferHandle host_source_handle(host_handle.base,
                                                    PackedByteCount(source_width, source_height));
    if (!host_source_handle) {
        last_status = IM_STATUS_OUT_OF_MEMORY;
        return fail_rga();
    }
    const int format = bottom_desc.image_format == IMAGE_RGB ? RK_FORMAT_RGB_888 : RK_FORMAT_BGR_888;
    auto source = wrapbuffer_handle_t(host_source_handle.Get(), source_width, source_height, source_width,
                                      source_height, format);
    return resize_to_target(source, IM_COLOR_SPACE_DEFAULT) || fail_rga();
}

void RknnResizeNode::ResizeWithCpu(Blob& bottom, Blob& top, const Rect& region, bool output_rgb) const {
    const auto& desc   = bottom.GetBlobDesc();
    const auto* source = static_cast<const uint8_t*>(bottom.GetHandle().base);
    auto* destination  = static_cast<uint8_t*>(top.GetHandle().base);
    if (region.width != out_width_ || region.height != out_height_)
        std::memset(destination, padding_value_, PackedByteCount(out_width_, out_height_));
    if (region.empty())
        return;

    const size_t row_bytes = static_cast<size_t>(out_width_) * 3;
    destination += static_cast<size_t>(region.y) * row_bytes + static_cast<size_t>(region.x) * 3;
    BilinearResizePacked(source, desc.dims[2], desc.dims[1], 3, destination, region.width, region.height,
                         row_bytes, output_rgb && desc.image_format == IMAGE_BGR);
}

Status RknnResizeNode::ResizeSingle(const std::shared_ptr<Blob>& bottom, const std::shared_ptr<Blob>& top,
                                    bool allow_bound_target) {
    if (!bottom || !top || !top->GetHandle().base)
        return Status(COSMO_NN_ERR_NULL_PARAM, "RKNN resize input or output is null");
    const auto& desc = bottom->GetBlobDesc();
    if (desc.data_type != DATA_TYPE_UINT8 || desc.data_format != DATA_FORMAT_NHWC || desc.dims.size() != 4 ||
        desc.dims[0] != 1 || desc.dims[1] <= 0 || desc.dims[2] <= 0 || desc.dims[3] != 3) {
        return Status(COSMO_NN_ERR_INVALID_INPUT, "RKNN resize requires packed batch-1 uint8 input");
    }

    const auto region = CalculateResizeRegion(desc.dims[2], desc.dims[1], out_width_, out_height_, gravity_);
    const bool output_rgb  = desc.image_format == IMAGE_RGB || desc.image_format == IMAGE_BGR;
    const bool rga_success = output_rgb && gravity_ >= 0 && gravity_ <= 2 &&
                             ResizeWithRga(*bottom, *top, region, allow_bound_target);
    if (!rga_success) {
        if (!bottom->GetHandle().base)
            return Status(COSMO_NN_ERR_NULL_PARAM, "RKNN host resize fallback could not materialize input");
        const auto cpu_started = MetricsClock::now();
        ResizeWithCpu(*bottom, *top, region, output_rgb);
        GetInferencePipelineMetrics().RecordRknnCpuResizeFallback(ElapsedNanoseconds(cpu_started));
    }

    auto top_desc         = top->GetBlobDesc();
    top_desc.data_format  = DATA_FORMAT_NHWC;
    top_desc.image_format = output_rgb ? IMAGE_RGB : desc.image_format;
    top->SetBlobDesc(top_desc);
    return Status{COSMO_NN_OK};
}

Status RknnResizeNode::Forward(std::vector<std::shared_ptr<Blob>>& bottom_blobs,
                               std::vector<std::shared_ptr<Blob>>& top_blobs) {
    timer.Start();
    if (shared_resource)
        shared_resource->rknn_bound_input_target.frame_ready = false;
    if (top_blobs.size() != 1 || !top_blobs[0] || !top_blobs[0]->GetHandle().base)
        return Status(COSMO_NN_ERR_INVALID_INPUT, "RKNN resize requires exactly one output");
    const int batch = static_cast<int>(bottom_blobs.size());
    if (batch <= 0 || batch > max_batch)
        return Status(COSMO_NN_ERR_INVALID_INPUT, "RKNN resize batch size is invalid");

    auto top_desc    = top_blobs[0]->GetBlobDesc();
    top_desc.dims[0] = batch;
    top_blobs[0]->SetBlobDesc(top_desc);
    const size_t slice_size = PackedByteCount(out_width_, out_height_);
    for (int index = 0; index < batch; ++index) {
        BlobDesc slice_desc = top_desc;
        slice_desc.dims[0]  = 1;
        BlobHandle slice_handle;
        slice_handle.base =
            static_cast<uint8_t*>(top_blobs[0]->GetHandle().base) + static_cast<size_t>(index) * slice_size;
        auto slice = std::make_shared<Blob>(slice_desc, slice_handle);
        RETURN_ON_FAIL(ResizeSingle(bottom_blobs[index], slice, batch == 1));
        top_desc.image_format = slice->GetBlobDesc().image_format;
        top_desc.data_format  = slice->GetBlobDesc().data_format;
    }
    top_blobs[0]->SetBlobDesc(top_desc);
    timer.Stop();
    return Status{COSMO_NN_OK};
}

RknnCropResizeNode::RknnCropResizeNode() : CpuCropResizeNode() {
    node_type     = NodeType::NODE_CROP_RESIZE;
    name          = NodeTypeUtils::NodeTypeToStr(NODE_CROP_RESIZE).append("_0");
    one_blob_only = false;
}

RknnCropResizeNode::~RknnCropResizeNode() {
    ReleaseRgaBoundTarget();
}

void RknnCropResizeNode::LoadParam(Op* op) {
    CpuCropResizeNode::LoadParam(op);
    fast_contract_ = dst_height > 0 && dst_width > 0 && !h_top_crop.empty() && !h_bottom_crop.empty() &&
                     !w_left_crop.empty() && !w_right_crop.empty();
}

void RknnCropResizeNode::ReleaseRgaBoundTarget() {
    if (rga_bound_target_handle_ != 0) {
        releasebuffer_handle(static_cast<rga_buffer_handle_t>(rga_bound_target_handle_));
        rga_bound_target_handle_     = 0;
        rga_bound_target_generation_ = 0;
    }
}

void RknnCropResizeNode::InvalidateRgaBoundFrame() {
    if (!shared_resource)
        return;
    auto& target = shared_resource->rknn_bound_input_target;
    if (target.owner == shared_resource->rknn_bound_input_provider)
        target.frame_ready = false;
}

bool RknnCropResizeNode::AcquireRgaBoundTarget(uint32_t& handle) {
    handle                = 0;
    const bool enabled    = RknnRgaBoundInputEnabled();
    const bool compatible = shared_resource && shared_resource->rknn_bound_input_preprocess_compatible;
    const bool provider_available = shared_resource && shared_resource->rknn_bound_input_provider;
    if (!enabled || !compatible || !provider_available || rga_bound_target_unavailable_) {
        if (!rga_bound_target_unavailable_ && !rga_bound_guard_logged_) {
            LOG_WARN(
                "RKNN classifier cannot acquire the RGA-bound input target: enabled={} "
                "shared_resource={} preprocess_compatible={} provider={}",
                enabled, shared_resource != nullptr, compatible, provider_available);
            rga_bound_guard_logged_ = true;
        }
        return false;
    }
    std::string reason;
    auto* provider = shared_resource->rknn_bound_input_provider;
    if (!provider->EnsureRgaBoundInput(dst_height, dst_width, reason)) {
        rga_bound_target_unavailable_ = true;
        LogRgaBoundInputFallbackOnce(reason);
        return false;
    }
    const auto& target = shared_resource->rknn_bound_input_target;
    if (target.owner != provider || !target.Matches(dst_height, dst_width) ||
        target.bytes > static_cast<size_t>(std::numeric_limits<int>::max())) {
        rga_bound_target_unavailable_ = true;
        LogRgaBoundInputFallbackOnce("provider returned an incompatible crop-resize target");
        return false;
    }
    if (rga_bound_target_handle_ != 0 && rga_bound_target_generation_ == target.generation) {
        handle = rga_bound_target_handle_;
        return true;
    }
    ReleaseRgaBoundTarget();
    const auto import_started = MetricsClock::now();
    const auto imported       = importbuffer_fd(target.fd, static_cast<int>(target.bytes));
    GetInferencePipelineMetrics().RecordRknnRgaBoundInputImport(ElapsedNanoseconds(import_started),
                                                                imported != 0);
    if (imported == 0) {
        rga_bound_target_unavailable_ = true;
        GetInferencePipelineMetrics().RecordRknnRgaFailure();
        LogRgaBoundInputFallbackOnce("RGA could not import the classifier RKNN DMA-BUF fd");
        return false;
    }
    rga_bound_target_handle_     = static_cast<uint32_t>(imported);
    rga_bound_target_generation_ = target.generation;
    handle                       = rga_bound_target_handle_;
    return true;
}

Status RknnCropResizeNode::ResizeRegions(std::vector<std::shared_ptr<Blob>>& image_blobs,
                                         std::vector<std::shared_ptr<Blob>>& rect_blobs, Blob& output) {
    if (!fast_contract_)
        return Status(COSMO_NN_ERR_INVALID_CFG, "RKNN crop resize parameters are incomplete");
    if (image_blobs.empty() || image_blobs.size() != rect_blobs.size())
        return Status(COSMO_NN_ERR_INVALID_INPUT, "RKNN crop resize needs rectangles for each source image");

    size_t roi_count = 0;
    const auto first_format =
        image_blobs.front() ? image_blobs.front()->GetBlobDesc().image_format : IMAGE_UNKNOWN;
    const bool convert_to_rgb = first_format == IMAGE_RGB || first_format == IMAGE_BGR;
    for (size_t index = 0; index < image_blobs.size(); ++index) {
        if (!image_blobs[index] || !rect_blobs[index])
            return Status(COSMO_NN_ERR_NULL_PARAM, "RKNN crop image or rectangles are missing");
        const auto& image = image_blobs[index]->GetBlobDesc();
        const auto& rects = rect_blobs[index]->GetBlobDesc().dims;
        if (image.data_type != DATA_TYPE_UINT8 || image.data_format != DATA_FORMAT_NHWC ||
            image.dims.size() != 4 || image.dims[0] != 1 || image.dims[1] <= 0 || image.dims[2] <= 0 ||
            image.dims[3] != 3 || rects.size() != 2 || rects[0] < 0 || rects[1] != 4) {
            return Status(COSMO_NN_ERR_INVALID_INPUT,
                          "RKNN crop requires packed images and [n,4] rectangles");
        }
        if (image.image_format != first_format &&
            (!convert_to_rgb || (image.image_format != IMAGE_RGB && image.image_format != IMAGE_BGR))) {
            return Status(COSMO_NN_ERR_INVALID_INPUT, "RKNN crop image formats cannot share an output batch");
        }
        roi_count += static_cast<size_t>(rects[0]);
    }
    if (roi_count > static_cast<size_t>(max_batch))
        return Status(COSMO_NN_ERR_INVALID_INPUT, "RKNN crop resize batch is too large");

    auto& metrics = GetInferencePipelineMetrics();
    if (roi_count != 0 && !rga_unavailable_ && RknnForceRgaFailure()) {
        rga_unavailable_ = true;
        metrics.RecordRknnRgaCropResize(0, false);
        metrics.RecordRknnRgaFailure();
    }
    const bool supported_mode = gravity >= 0 && gravity <= 2;
    if (rga_unavailable_ || !supported_mode || !convert_to_rgb) {
        // Validate every CPU source before changing any slice of the output.
        for (size_t index = 0; index < image_blobs.size(); ++index) {
            if (rect_blobs[index]->GetBlobDesc().dims[0] > 0 && !image_blobs[index]->GetHandle().base)
                return Status(COSMO_NN_ERR_NULL_PARAM, "RKNN crop fallback could not materialize the image");
        }
    }
    uint32_t bound_handle = 0;
    if (roi_count == 1 && convert_to_rgb && supported_mode && !rga_unavailable_)
        (void)AcquireRgaBoundTarget(bound_handle);

    const size_t slice_size = PackedByteCount(dst_width, dst_height);
    auto* const pixels      = static_cast<uint8_t*>(output.GetHandle().base);
    const uint8_t padding   = color.empty() ? 114 : static_cast<uint8_t>(color.front());
    size_t processed        = 0;

    for (size_t index = 0; index < image_blobs.size(); ++index) {
        auto& image      = *image_blobs[index];
        const auto& desc = image.GetBlobDesc();
        const int width  = desc.dims[2];
        const int height = desc.dims[1];
        RETURN_ON_FAIL(PrepareRect(rect_blobs[index], width, height));

        const auto image_handle   = image.GetHandle(false);
        const auto& native        = image_handle.native_image;
        const bool native_matches = MatchesNativeImage(native, width, height);
        media::ScopedRgaBufferHandle native_handle;
        media::ScopedRgaBufferHandle packed_handle;
        bool tried_native_import     = false;
        bool tried_packed_import     = false;
        const uint8_t* packed_pixels = static_cast<const uint8_t*>(image_handle.base);
        const auto packed_source     = [&]() {
            if (!packed_pixels)
                packed_pixels = static_cast<const uint8_t*>(image.GetHandle().base);
            return packed_pixels;
        };

        for (size_t offset = 0; offset < calculated_rects.size(); offset += 4, ++processed) {
            const Rect crop{calculated_rects[offset], calculated_rects[offset + 1],
                            calculated_rects[offset + 2], calculated_rects[offset + 3]};
            if (crop.empty() || crop.x < 0 || crop.y < 0 || crop.x > width - crop.width ||
                crop.y > height - crop.height) {
                return Status(COSMO_NN_ERR_INVALID_INPUT, "crop region is outside the image");
            }
            const auto region =
                CalculateResizeRegion(crop.width, crop.height, dst_width, dst_height, gravity);
            auto* const destination = pixels + processed * slice_size;
            bool success            = false;
            if (convert_to_rgb && supported_mode && !rga_unavailable_ && !region.empty()) {
                IM_STATUS status = IM_STATUS_FAILED;
                media::ScopedRgaBufferHandle host_target;
                const im_rect source_rect{crop.x, crop.y, crop.width, crop.height};
                const im_rect target_rect{region.x, region.y, region.width, region.height};
                const auto run = [&](rga_buffer_t source, IM_COLOR_SPACE_MODE color_space, bool bound) {
                    const int stride =
                        bound ? shared_resource->rknn_bound_input_target.width_stride : dst_width;
                    const auto handle =
                        bound ? static_cast<rga_buffer_handle_t>(bound_handle) : host_target.Get();
                    auto target = wrapbuffer_handle_t(handle, dst_width, dst_height, stride, dst_height,
                                                      RK_FORMAT_RGB_888);
                    const bool needs_padding    = region.width != dst_width || region.height != dst_height;
                    const bool cpu_host_padding = needs_padding && kIsRk3588 && !bound;
                    if (needs_padding && !cpu_host_padding) {
                        auto* provider = bound ? shared_resource->rknn_bound_input_provider : nullptr;
                        status = FillRgaPadding(target, padding, provider, rga_bound_target_generation_);
                        if (!media::RockchipRgaSucceeded(status))
                            return false;
                    }
                    const bool resized = RunStagedRgaResize(source, source_rect, color_space, target,
                                                            target_rect, status, true);
                    if (resized && cpu_host_padding) {
                        host_target.Reset();
                        FillHostPadding(destination, dst_width, dst_height, region, padding);
                    }
                    return resized;
                };
                const auto resize_to_target = [&](rga_buffer_t source, IM_COLOR_SPACE_MODE color_space) {
                    if (bound_handle) {
                        if (run(source, color_space, true)) {
                            auto& target = shared_resource->rknn_bound_input_target;
                            if (target.owner == shared_resource->rknn_bound_input_provider &&
                                target.generation == rga_bound_target_generation_) {
                                target.frame_ready = true;
                                return true;
                            }
                            status = IM_STATUS_INVALID_PARAM;
                        }
                        if (IsRgaDeviceUnavailable(status))
                            return false;
                    }
                    if (!host_target && !host_target.ImportVirtual(destination, slice_size)) {
                        status = IM_STATUS_OUT_OF_MEMORY;
                        return false;
                    }
                    return run(source, color_space, false);
                };

                // Preserve the exact business ROI when the YUV chroma grid needs even coordinates.
                const bool exact_yuv_crop = ((crop.x | crop.y | crop.width | crop.height) & 1) == 0;
                if (native_matches && exact_yuv_crop && !native_rga_unavailable_) {
                    if (!tried_native_import) {
                        tried_native_import = true;
                        if (RknnForceMppDmaBufFailure()) {
                            native_rga_unavailable_ = true;
                            LogMppDmaBufFallbackOnce(status);
                        } else {
                            const auto started = MetricsClock::now();
                            native_handle.ImportFd(native.fd, native.bytes);
                            metrics.RecordRknnMppDmaBufImport(ElapsedNanoseconds(started),
                                                              bool(native_handle));
                        }
                        if (!native_handle)
                            metrics.RecordRknnMppDmaBufFallback();
                    }
                    if (native_handle) {
                        const int format =
                            native.format == IMAGE_NV12 ? RK_FORMAT_YCbCr_420_SP : RK_FORMAT_YCbCr_420_P;
                        const auto source =
                            wrapbuffer_handle_t(native_handle.Get(), width, height, native.width_stride,
                                                native.height_stride, format);
                        const std::array<int, 12> geometry{
                            width,
                            height,
                            native.width_stride,
                            native.height_stride,
                            static_cast<int>(native.format),
                            static_cast<int>(native.color_space),
                            static_cast<int>(native.color_range),
                            crop.x,
                            crop.y,
                            crop.width,
                            crop.height,
                            bound_handle ? shared_resource->rknn_bound_input_target.width_stride : 0};
                        bool attempted_native_resize = false;
                        if (geometry != rejected_native_crop_ && geometry != avoid_native_csc_crop_) {
                            attempted_native_resize = true;
                            success = resize_to_target(source, ResolveRgaYuvColorSpace(native));
                            if (!success && !IsRgaDeviceUnavailable(status)) {
                                if (IsNv12Bt709Full(native))
                                    avoid_native_csc_crop_ = geometry;
                                else if (status == IM_STATUS_NOT_SUPPORTED ||
                                         status == IM_STATUS_INVALID_PARAM ||
                                         status == IM_STATUS_ILLEGAL_PARAM)
                                    rejected_native_crop_ = geometry;
                            }
                        }
                        if (!success && geometry == avoid_native_csc_crop_ &&
                            geometry != rejected_native_crop_) {
                            attempted_native_resize = true;
                            success =
                                ResizeNv12Bt709FullToHost(native, native_handle.Get(), crop, destination,
                                                          dst_width, dst_height, region, padding);
                            if (!success)
                                rejected_native_crop_ = geometry;
                        }
                        if (success) {
                            metrics.RecordRknnMppDmaBufFrame(native.bytes);
                            metrics.RecordRknnRgaCropSource(true);
                        } else if (attempted_native_resize) {
                            metrics.RecordRknnMppDmaBufFallback();
                        }
                    }
                }
                if (!success && !IsRgaDeviceUnavailable(status)) {
                    if (!tried_packed_import) {
                        tried_packed_import = true;
                        packed_handle.ImportVirtual(const_cast<uint8_t*>(packed_source()),
                                                    PackedByteCount(width, height));
                    }
                    if (packed_handle) {
                        const int format =
                            desc.image_format == IMAGE_RGB ? RK_FORMAT_RGB_888 : RK_FORMAT_BGR_888;
                        const auto source =
                            wrapbuffer_handle_t(packed_handle.Get(), width, height, width, height, format);
                        success = resize_to_target(source, IM_COLOR_SPACE_DEFAULT);
                        if (success)
                            metrics.RecordRknnRgaCropSource(false);
                    }
                }
                if (!success) {
                    metrics.RecordRknnRgaFailure();
                    rga_unavailable_ = IsRgaDeviceUnavailable(status);
                    if (rga_unavailable_)
                        LogRgaCropResizeFallbackOnce(status);
                }
            }
            if (success) {
                metrics.RecordRknnPreprocessFastHit();
            } else {
                const auto* source = packed_source();
                if (!source)
                    return Status(COSMO_NN_ERR_NULL_PARAM,
                                  "RKNN crop fallback could not materialize the image");
                const auto started = MetricsClock::now();
                ResizeCropOnCpu(source, width, crop, region, destination,
                                convert_to_rgb && desc.image_format == IMAGE_BGR);
                metrics.RecordRknnCpuCropResizeFallback(ElapsedNanoseconds(started));
            }
        }
    }

    auto top_desc         = output.GetBlobDesc();
    top_desc.dims[0]      = static_cast<int>(roi_count);
    top_desc.data_format  = DATA_FORMAT_NHWC;
    top_desc.image_format = convert_to_rgb ? IMAGE_RGB : first_format;
    output.SetBlobDesc(top_desc);
    return Status{COSMO_NN_OK};
}

Status RknnCropResizeNode::Forward(std::vector<std::shared_ptr<Blob>>& image_blobs,
                                   std::vector<std::shared_ptr<Blob>>& rect_blobs,
                                   std::vector<std::shared_ptr<Blob>>& top_blobs) {
    InvalidateRgaBoundFrame();
    if (top_blobs.size() != 1 || !top_blobs.front() || !top_blobs.front()->GetHandle().base)
        return Status(COSMO_NN_ERR_INVALID_INPUT, "RKNN crop resize requires one output");

    timer.Start();
    auto status = ResizeRegions(image_blobs, rect_blobs, *top_blobs.front());
    timer.Stop();
    if (!status)
        InvalidateRgaBoundFrame();
    return status;
}

RknnNormalizeNode::RknnNormalizeNode() : Node() {
    node_type     = NodeType::NODE_NORMALIZE;
    name          = NodeTypeUtils::NodeTypeToStr(NODE_NORMALIZE).append("_0");
    one_blob_only = true;
}

void RknnNormalizeNode::LoadParam(Op* op) {
    const auto* normalize = dynamic_cast<Normalize*>(op);
    if (!normalize)
        return;
    mean_          = normalize->mean;
    std_dev_       = normalize->std;
    uniform_scale_ = normalize->scale;
    is_bgr_        = normalize->is_bgr;
    if (std_dev_.empty()) {
        scale_.assign(mean_.size(), uniform_scale_);
    } else {
        scale_.resize(std_dev_.size());
        std::transform(std_dev_.begin(), std_dev_.end(), scale_.begin(),
                       [](float value) { return 1.0f / value; });
    }
}

DeviceType RknnNormalizeNode::GetTopBlobDeviceType() {
    return DeviceType::DEVICE_NAIVE;
}

bool RknnNormalizeNode::NeedBottomShapesInfered() {
    return true;
}

Status RknnNormalizeNode::InferTopShapesWithBottoms(std::vector<DimsVector> dims,
                                                    std::vector<DataType> types) {
    if (dims.size() != 1 || types.size() != 1 || dims[0].size() != 4)
        return Status(COSMO_NN_ERR_INVALID_INPUT, "RKNN normalize input shape is invalid");
    native_contract_ = types[0] == DATA_TYPE_UINT8 &&
                       IsRknnNativeNormalizeContract(mean_, std_dev_, uniform_scale_, dims[0]);
    if (shared_resource)
        shared_resource->rknn_bound_input_preprocess_compatible = native_contract_ && !is_bgr_;
    if (native_contract_) {
        top_blob_shapes     = {dims[0]};
        top_blob_data_types = {DataType::DATA_TYPE_INT8};
    } else {
        top_blob_shapes     = {{dims[0][0], dims[0][3], dims[0][1], dims[0][2]}};
        top_blob_data_types = {DataType::DATA_TYPE_FLOAT};
    }
    return Status{COSMO_NN_OK};
}

size_t RknnNormalizeNode::GetBottomCount() {
    return 1;
}

size_t RknnNormalizeNode::GetTopCount() {
    return 1;
}

bool RknnNormalizeNode::NeedSwapRedBlue(ImageFormat format) const {
    if (format == IMAGE_BGR || format == IMAGE_BGRA)
        return !is_bgr_;
    if (format == IMAGE_RGB || format == IMAGE_RGBA)
        return is_bgr_;
    return false;
}

bool RknnNormalizeNode::CanBypassBoundInput(const Blob& bottom) const {
    if (!native_contract_ || !shared_resource || !shared_resource->rknn_bound_input_provider) {
        return false;
    }
    const auto& target = shared_resource->rknn_bound_input_target;
    const auto desc    = const_cast<Blob&>(bottom).GetBlobDesc();
    return target.frame_ready && target.owner == shared_resource->rknn_bound_input_provider &&
           desc.data_type == DATA_TYPE_UINT8 && desc.data_format == DATA_FORMAT_NHWC &&
           desc.image_format == IMAGE_RGB && desc.dims.size() == 4 && desc.dims[0] == 1 &&
           target.Matches(desc.dims[1], desc.dims[2]) && !NeedSwapRedBlue(desc.image_format);
}

Status RknnNormalizeNode::ForwardNative(const Blob& bottom, Blob& top) {
    auto& mutable_bottom   = const_cast<Blob&>(bottom);
    const auto bottom_desc = mutable_bottom.GetBlobDesc();
    auto top_desc          = top.GetBlobDesc();
    if (bottom_desc.data_type != DATA_TYPE_UINT8 || bottom_desc.data_format != DATA_FORMAT_NHWC ||
        top_desc.data_type != DATA_TYPE_INT8 || top_desc.data_format != DATA_FORMAT_NHWC) {
        return Status(COSMO_NN_ERR_INVALID_INPUT, "RKNN native preprocessing blob contract mismatch");
    }
    const size_t pixels =
        static_cast<size_t>(bottom_desc.dims[0]) * bottom_desc.dims[1] * bottom_desc.dims[2];
    const auto started = MetricsClock::now();
    MapPackedU8ToNativeInt8(static_cast<const uint8_t*>(mutable_bottom.GetHandle().base),
                            static_cast<int8_t*>(top.GetHandle().base), pixels,
                            NeedSwapRedBlue(bottom_desc.image_format));
    GetInferencePipelineMetrics().RecordRknnNativeInputMap(ElapsedNanoseconds(started));
    GetInferencePipelineMetrics().RecordRknnPreprocessFastHit();
    top_desc.image_format = is_bgr_ ? IMAGE_BGR : IMAGE_RGB;
    top.SetBlobDesc(top_desc);
    return Status{COSMO_NN_OK};
}

Status RknnNormalizeNode::ForwardFloat(const Blob& bottom, Blob& top) {
    auto& mutable_bottom   = const_cast<Blob&>(bottom);
    const auto bottom_desc = mutable_bottom.GetBlobDesc();
    const auto top_desc    = top.GetBlobDesc();
    if (bottom_desc.data_type != DATA_TYPE_UINT8 || top_desc.data_type != DATA_TYPE_FLOAT ||
        top_desc.data_format != DATA_FORMAT_NCHW || mean_.size() < 3 || scale_.size() < 3) {
        return Status(COSMO_NN_ERR_INVALID_INPUT, "RKNN float normalize blob contract mismatch");
    }
    const int batch          = bottom_desc.dims[0];
    const int height         = bottom_desc.dims[1];
    const int width          = bottom_desc.dims[2];
    const int channels       = bottom_desc.dims[3];
    const int plane          = height * width;
    const bool swap_red_blue = NeedSwapRedBlue(bottom_desc.image_format);
    const auto* source       = static_cast<const uint8_t*>(mutable_bottom.GetHandle().base);
    auto* destination        = static_cast<float*>(top.GetHandle().base);
    for (int batch_index = 0; batch_index < batch; ++batch_index) {
        const auto* batch_source = source + batch_index * plane * channels;
        auto* batch_destination  = destination + batch_index * plane * 3;
        for (int pixel = 0; pixel < plane; ++pixel) {
            const int source_offset = pixel * channels;
            const int first_channel = swap_red_blue ? 2 : 0;
            const int third_channel = swap_red_blue ? 0 : 2;
            batch_destination[pixel] =
                (static_cast<float>(batch_source[source_offset + first_channel]) - mean_[0]) * scale_[0];
            batch_destination[plane + pixel] =
                (static_cast<float>(batch_source[source_offset + 1]) - mean_[1]) * scale_[1];
            batch_destination[2 * plane + pixel] =
                (static_cast<float>(batch_source[source_offset + third_channel]) - mean_[2]) * scale_[2];
        }
    }
    return Status{COSMO_NN_OK};
}

Status RknnNormalizeNode::Forward(std::vector<std::shared_ptr<Blob>>& bottom_blobs,
                                  std::vector<std::shared_ptr<Blob>>& top_blobs) {
    timer.Start();
    if (bottom_blobs.size() != 1 || top_blobs.size() != 1 || !bottom_blobs[0] || !top_blobs[0] ||
        !bottom_blobs[0]->GetHandle().base || !top_blobs[0]->GetHandle().base) {
        return Status(COSMO_NN_ERR_INVALID_INPUT,
                      "RKNN normalize requires exactly one valid input and output");
    }
    const auto bottom_desc = bottom_blobs[0]->GetBlobDesc();
    if (bottom_desc.dims.size() != 4 || bottom_desc.dims[0] <= 0 || bottom_desc.dims[0] > max_batch) {
        return Status(COSMO_NN_ERR_INVALID_INPUT, "RKNN normalize batch size is invalid");
    }
    SetCurrentBatch(top_blobs[0], bottom_desc.dims[0]);
    auto runtime_top_desc        = top_blobs[0]->GetBlobDesc();
    runtime_top_desc.data_format = native_contract_ ? DATA_FORMAT_NHWC : DATA_FORMAT_NCHW;
    top_blobs[0]->SetBlobDesc(runtime_top_desc);
    Status status;
    if (native_contract_) {
        if (CanBypassBoundInput(*bottom_blobs[0])) {
            runtime_top_desc.image_format = is_bgr_ ? IMAGE_BGR : IMAGE_RGB;
            top_blobs[0]->SetBlobDesc(runtime_top_desc);
            GetInferencePipelineMetrics().RecordRknnPreprocessFastHit();
            GetInferencePipelineMetrics().RecordRknnRgaBoundInputNormalizeBypass();
            status = COSMO_NN_OK;
        } else {
            if (shared_resource && shared_resource->rknn_bound_input_target.owner ==
                                       shared_resource->rknn_bound_input_provider) {
                shared_resource->rknn_bound_input_target.frame_ready = false;
            }
            status = ForwardNative(*bottom_blobs[0], *top_blobs[0]);
        }
    } else {
        const auto fallback_started = MetricsClock::now();
        status                      = ForwardFloat(*bottom_blobs[0], *top_blobs[0]);
        GetInferencePipelineMetrics().RecordRknnCpuNormalizeFallback(ElapsedNanoseconds(fallback_started));
    }
    timer.Stop();
    return status;
}

}  // namespace cosmo::nn

#endif
