#include "nn/core/inference_pipeline_metrics.h"

namespace cosmo::nn {

void InferencePipelineMetrics::RecordStage(std::atomic<uint64_t>& count, std::atomic<uint64_t>& duration,
                                           uint64_t samples, uint64_t nanoseconds) {
    count.fetch_add(samples, std::memory_order_relaxed);
    duration.fetch_add(nanoseconds, std::memory_order_relaxed);
}

void InferencePipelineMetrics::RecordColorConvert(uint64_t nanoseconds, uint64_t frames) {
    RecordStage(color_convert_frames_, color_convert_nanoseconds_, frames, nanoseconds);
}

void InferencePipelineMetrics::RecordBlobConvert(uint64_t nanoseconds, uint64_t frames) {
    RecordStage(blob_convert_frames_, blob_convert_nanoseconds_, frames, nanoseconds);
}

void InferencePipelineMetrics::RecordGraphForward(uint64_t nanoseconds, uint64_t frames, bool success) {
    RecordStage(graph_forward_frames_, graph_forward_nanoseconds_, frames, nanoseconds);
    if (!success)
        graph_forward_failures_.fetch_add(1, std::memory_order_relaxed);
}

void InferencePipelineMetrics::RecordResultParse(uint64_t nanoseconds, uint64_t frames, bool success) {
    RecordStage(result_parse_frames_, result_parse_nanoseconds_, frames, nanoseconds);
    if (!success)
        result_parse_failures_.fetch_add(1, std::memory_order_relaxed);
}

void InferencePipelineMetrics::RecordRknnForward(uint64_t nanoseconds, bool success, RknnModelScope scope) {
    RecordStage(rknn_forwards_, rknn_forward_nanoseconds_, 1, nanoseconds);
    if (!success)
        rknn_forward_failures_.fetch_add(1, std::memory_order_relaxed);
    if (scope == RknnModelScope::Detector) {
        RecordStage(rknn_detector_forwards_, rknn_detector_forward_nanoseconds_, 1, nanoseconds);
        if (!success)
            rknn_detector_forward_failures_.fetch_add(1, std::memory_order_relaxed);
    }
}

void InferencePipelineMetrics::RecordRknnPrepare(uint64_t nanoseconds, RknnModelScope scope) {
    RecordStage(rknn_prepare_calls_, rknn_prepare_nanoseconds_, 1, nanoseconds);
    if (scope == RknnModelScope::Detector)
        RecordStage(rknn_detector_prepare_calls_, rknn_detector_prepare_nanoseconds_, 1, nanoseconds);
}

void InferencePipelineMetrics::RecordRknnInputsSet(uint64_t nanoseconds, RknnModelScope scope) {
    RecordStage(rknn_inputs_set_calls_, rknn_inputs_set_nanoseconds_, 1, nanoseconds);
    if (scope == RknnModelScope::Detector)
        RecordStage(rknn_detector_inputs_set_calls_, rknn_detector_inputs_set_nanoseconds_, 1, nanoseconds);
}

void InferencePipelineMetrics::RecordRknnRun(uint64_t nanoseconds, RknnModelScope scope) {
    RecordStage(rknn_run_calls_, rknn_run_nanoseconds_, 1, nanoseconds);
    if (scope == RknnModelScope::Detector)
        RecordStage(rknn_detector_run_calls_, rknn_detector_run_nanoseconds_, 1, nanoseconds);
}

void InferencePipelineMetrics::RecordRknnOutputsGet(uint64_t nanoseconds, RknnModelScope scope) {
    RecordStage(rknn_outputs_get_calls_, rknn_outputs_get_nanoseconds_, 1, nanoseconds);
    if (scope == RknnModelScope::Detector)
        RecordStage(rknn_detector_outputs_get_calls_, rknn_detector_outputs_get_nanoseconds_, 1, nanoseconds);
}

void InferencePipelineMetrics::RecordRknnOutputsRelease(uint64_t nanoseconds, RknnModelScope scope) {
    RecordStage(rknn_outputs_release_calls_, rknn_outputs_release_nanoseconds_, 1, nanoseconds);
    if (scope == RknnModelScope::Detector) {
        RecordStage(rknn_detector_outputs_release_calls_, rknn_detector_outputs_release_nanoseconds_, 1,
                    nanoseconds);
    }
}

void InferencePipelineMetrics::RecordRknnOutputTransform(uint64_t nanoseconds, RknnModelScope scope) {
    RecordStage(rknn_output_transform_calls_, rknn_output_transform_nanoseconds_, 1, nanoseconds);
    if (scope == RknnModelScope::Detector)
        RecordStage(rknn_detector_output_transform_calls_, rknn_detector_output_transform_nanoseconds_, 1,
                    nanoseconds);
}

void InferencePipelineMetrics::RecordRknnMutexWait(uint64_t nanoseconds, RknnModelScope scope) {
    RecordStage(rknn_mutex_wait_calls_, rknn_mutex_wait_nanoseconds_, 1, nanoseconds);
    if (scope == RknnModelScope::Detector)
        RecordStage(rknn_detector_mutex_wait_calls_, rknn_detector_mutex_wait_nanoseconds_, 1, nanoseconds);
}

void InferencePipelineMetrics::RecordRknnPreprocessFastHit() {
    rknn_preprocess_fast_hits_.fetch_add(1, std::memory_order_relaxed);
}

void InferencePipelineMetrics::RecordRknnRgaFill(uint64_t nanoseconds) {
    RecordStage(rknn_rga_fill_calls_, rknn_rga_fill_nanoseconds_, 1, nanoseconds);
}

void InferencePipelineMetrics::RecordRknnRgaResizeColor(uint64_t nanoseconds) {
    RecordStage(rknn_rga_resize_color_calls_, rknn_rga_resize_color_nanoseconds_, 1, nanoseconds);
}

void InferencePipelineMetrics::RecordRknnRgaSmallFrameResize(uint64_t nanoseconds) {
    RecordStage(rknn_rga_small_frame_resize_calls_, rknn_rga_small_frame_resize_nanoseconds_, 1, nanoseconds);
}

void InferencePipelineMetrics::RecordRknnRgaSmallFrameCsc(uint64_t nanoseconds) {
    RecordStage(rknn_rga_small_frame_csc_calls_, rknn_rga_small_frame_csc_nanoseconds_, 1, nanoseconds);
}

void InferencePipelineMetrics::RecordRknnRgaCropResize(uint64_t nanoseconds, bool success) {
    RecordStage(rknn_rga_crop_resize_calls_, rknn_rga_crop_resize_nanoseconds_, 1, nanoseconds);
    if (!success)
        rknn_rga_crop_resize_failures_.fetch_add(1, std::memory_order_relaxed);
}

void InferencePipelineMetrics::RecordRknnRgaCropSource(bool dmabuf) {
    auto& counter = dmabuf ? rknn_rga_crop_dmabuf_frames_ : rknn_rga_crop_host_fallbacks_;
    counter.fetch_add(1, std::memory_order_relaxed);
}

void InferencePipelineMetrics::RecordRknnRgaFailure() {
    rknn_rga_failures_.fetch_add(1, std::memory_order_relaxed);
}

void InferencePipelineMetrics::RecordRknnCpuResizeFallback(uint64_t nanoseconds) {
    RecordStage(rknn_cpu_resize_fallback_calls_, rknn_cpu_resize_fallback_nanoseconds_, 1, nanoseconds);
}

void InferencePipelineMetrics::RecordRknnCpuCropResizeFallback(uint64_t nanoseconds) {
    RecordStage(rknn_cpu_crop_resize_fallback_calls_, rknn_cpu_crop_resize_fallback_nanoseconds_, 1,
                nanoseconds);
}

void InferencePipelineMetrics::RecordRknnCpuNormalizeFallback(uint64_t nanoseconds) {
    RecordStage(rknn_cpu_normalize_fallback_calls_, rknn_cpu_normalize_fallback_nanoseconds_, 1, nanoseconds);
}

void InferencePipelineMetrics::RecordRknnNativeInputMap(uint64_t nanoseconds) {
    RecordStage(rknn_native_input_map_calls_, rknn_native_input_map_nanoseconds_, 1, nanoseconds);
}

void InferencePipelineMetrics::RecordRknnInputFormat(bool native_int8, bool compatibility_fallback,
                                                     bool uint8_contract) {
    if (uint8_contract)
        rknn_uint8_contract_inputs_.fetch_add(1, std::memory_order_relaxed);
    else
        (native_int8 ? rknn_native_int8_inputs_ : rknn_float_inputs_).fetch_add(1, std::memory_order_relaxed);
    if (compatibility_fallback)
        rknn_input_compatibility_fallbacks_.fetch_add(1, std::memory_order_relaxed);
}

void InferencePipelineMetrics::RecordRknnBoundInputBind(bool success) {
    rknn_bound_input_bind_attempts_.fetch_add(1, std::memory_order_relaxed);
    if (!success)
        rknn_bound_input_bind_failures_.fetch_add(1, std::memory_order_relaxed);
}

void InferencePipelineMetrics::RecordRknnBoundInputCopy(uint64_t nanoseconds, uint64_t bytes, bool success) {
    RecordStage(rknn_bound_input_copy_calls_, rknn_bound_input_copy_nanoseconds_, 1, nanoseconds);
    if (success)
        rknn_bound_input_copy_bytes_.fetch_add(bytes, std::memory_order_relaxed);
    else
        rknn_bound_input_copy_failures_.fetch_add(1, std::memory_order_relaxed);
}

void InferencePipelineMetrics::RecordRknnBoundInputSync(uint64_t nanoseconds, bool success) {
    RecordStage(rknn_bound_input_sync_calls_, rknn_bound_input_sync_nanoseconds_, 1, nanoseconds);
    if (!success)
        rknn_bound_input_sync_failures_.fetch_add(1, std::memory_order_relaxed);
}

void InferencePipelineMetrics::RecordRknnBoundInputFrame() {
    rknn_bound_input_frames_.fetch_add(1, std::memory_order_relaxed);
}

void InferencePipelineMetrics::RecordRknnRgaBoundInputBind(bool success) {
    RecordRknnBoundInputBind(success);
    rknn_rga_bound_input_bind_attempts_.fetch_add(1, std::memory_order_relaxed);
    if (!success)
        rknn_rga_bound_input_bind_failures_.fetch_add(1, std::memory_order_relaxed);
}

void InferencePipelineMetrics::RecordRknnRgaBoundInputImport(uint64_t nanoseconds, bool success) {
    RecordStage(rknn_rga_bound_input_import_calls_, rknn_rga_bound_input_import_nanoseconds_, 1, nanoseconds);
    if (!success)
        rknn_rga_bound_input_import_failures_.fetch_add(1, std::memory_order_relaxed);
}

void InferencePipelineMetrics::RecordRknnRgaBoundInputRequantize(uint64_t nanoseconds, bool success) {
    RecordStage(rknn_rga_bound_requantize_calls_, rknn_rga_bound_requantize_nanoseconds_, 1, nanoseconds);
    if (!success)
        rknn_rga_bound_requantize_failures_.fetch_add(1, std::memory_order_relaxed);
}

void InferencePipelineMetrics::RecordRknnRgaBoundInputFrame(bool fused_uint8) {
    rknn_rga_bound_input_frames_.fetch_add(1, std::memory_order_relaxed);
    if (fused_uint8)
        rknn_rga_bound_uint8_frames_.fetch_add(1, std::memory_order_relaxed);
    else
        rknn_rga_bound_native_int8_frames_.fetch_add(1, std::memory_order_relaxed);
    RecordRknnBoundInputFrame();
}

void InferencePipelineMetrics::RecordRknnRgaBoundInputNormalizeBypass() {
    rknn_rga_bound_input_normalize_bypasses_.fetch_add(1, std::memory_order_relaxed);
}

void InferencePipelineMetrics::RecordRknnMppDmaBufImport(uint64_t nanoseconds, bool success) {
    RecordStage(rknn_mpp_dmabuf_import_calls_, rknn_mpp_dmabuf_import_nanoseconds_, 1, nanoseconds);
    if (!success)
        rknn_mpp_dmabuf_import_failures_.fetch_add(1, std::memory_order_relaxed);
}

void InferencePipelineMetrics::RecordRknnMppDmaBufFrame(uint64_t source_bytes) {
    rknn_mpp_dmabuf_frames_.fetch_add(1, std::memory_order_relaxed);
    rknn_mpp_dmabuf_source_bytes_.fetch_add(source_bytes, std::memory_order_relaxed);
}

void InferencePipelineMetrics::RecordRknnMppDmaBufFallback() {
    rknn_mpp_dmabuf_fallbacks_.fetch_add(1, std::memory_order_relaxed);
}

void InferencePipelineMetrics::RecordRknnOutputFormat(bool native_int8, uint64_t bytes,
                                                      bool compatibility_fallback) {
    (native_int8 ? rknn_native_int8_outputs_ : rknn_float_outputs_).fetch_add(1, std::memory_order_relaxed);
    (native_int8 ? rknn_native_output_bytes_ : rknn_float_output_bytes_)
        .fetch_add(bytes, std::memory_order_relaxed);
    if (compatibility_fallback)
        rknn_output_compatibility_fallbacks_.fetch_add(1, std::memory_order_relaxed);
}

void InferencePipelineMetrics::RecordRknnYolov8Transform(uint64_t dfl_nanoseconds,
                                                         uint64_t class_nanoseconds) {
    RecordStage(rknn_yolov8_dfl_calls_, rknn_yolov8_dfl_nanoseconds_, 1, dfl_nanoseconds);
    RecordStage(rknn_yolov8_class_calls_, rknn_yolov8_class_nanoseconds_, 1, class_nanoseconds);
}

void InferencePipelineMetrics::RecordRknnYolov8DirectCandidates(bool success, uint64_t points_scanned,
                                                                uint64_t points_decoded,
                                                                uint64_t logical_float_bytes_avoided,
                                                                uint64_t score_sum_points_rejected) {
    rknn_yolov8_direct_candidate_calls_.fetch_add(1, std::memory_order_relaxed);
    if (!success) {
        rknn_yolov8_direct_candidate_failures_.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    rknn_yolov8_direct_points_scanned_.fetch_add(points_scanned, std::memory_order_relaxed);
    rknn_yolov8_direct_points_decoded_.fetch_add(points_decoded, std::memory_order_relaxed);
    rknn_yolov8_score_sum_points_rejected_.fetch_add(score_sum_points_rejected, std::memory_order_relaxed);
    rknn_yolov8_logical_float_bytes_avoided_.fetch_add(logical_float_bytes_avoided,
                                                       std::memory_order_relaxed);
}

void InferencePipelineMetrics::RecordYolov8Postprocess(uint64_t nanoseconds) {
    RecordStage(yolov8_postprocess_calls_, yolov8_postprocess_nanoseconds_, 1, nanoseconds);
}

void InferencePipelineMetrics::RecordYolov8Nms(uint64_t nanoseconds) {
    RecordStage(yolov8_nms_calls_, yolov8_nms_nanoseconds_, 1, nanoseconds);
}

InferencePipelineMetricsSnapshot InferencePipelineMetrics::Snapshot() const {
    InferencePipelineMetricsSnapshot snapshot;
#define SNAPSHOT_FIELD(field) snapshot.field = field##_.load(std::memory_order_relaxed)
    SNAPSHOT_FIELD(color_convert_frames);
    SNAPSHOT_FIELD(color_convert_nanoseconds);
    SNAPSHOT_FIELD(blob_convert_frames);
    SNAPSHOT_FIELD(blob_convert_nanoseconds);
    SNAPSHOT_FIELD(graph_forward_frames);
    SNAPSHOT_FIELD(graph_forward_nanoseconds);
    SNAPSHOT_FIELD(graph_forward_failures);
    SNAPSHOT_FIELD(result_parse_frames);
    SNAPSHOT_FIELD(result_parse_nanoseconds);
    SNAPSHOT_FIELD(result_parse_failures);
    SNAPSHOT_FIELD(rknn_forwards);
    SNAPSHOT_FIELD(rknn_forward_nanoseconds);
    SNAPSHOT_FIELD(rknn_forward_failures);
    SNAPSHOT_FIELD(rknn_prepare_calls);
    SNAPSHOT_FIELD(rknn_prepare_nanoseconds);
    SNAPSHOT_FIELD(rknn_inputs_set_calls);
    SNAPSHOT_FIELD(rknn_inputs_set_nanoseconds);
    SNAPSHOT_FIELD(rknn_run_calls);
    SNAPSHOT_FIELD(rknn_run_nanoseconds);
    SNAPSHOT_FIELD(rknn_outputs_get_calls);
    SNAPSHOT_FIELD(rknn_outputs_get_nanoseconds);
    SNAPSHOT_FIELD(rknn_outputs_release_calls);
    SNAPSHOT_FIELD(rknn_outputs_release_nanoseconds);
    SNAPSHOT_FIELD(rknn_output_transform_calls);
    SNAPSHOT_FIELD(rknn_output_transform_nanoseconds);
    SNAPSHOT_FIELD(rknn_mutex_wait_calls);
    SNAPSHOT_FIELD(rknn_mutex_wait_nanoseconds);
    SNAPSHOT_FIELD(rknn_detector_forwards);
    SNAPSHOT_FIELD(rknn_detector_forward_nanoseconds);
    SNAPSHOT_FIELD(rknn_detector_forward_failures);
    SNAPSHOT_FIELD(rknn_detector_prepare_calls);
    SNAPSHOT_FIELD(rknn_detector_prepare_nanoseconds);
    SNAPSHOT_FIELD(rknn_detector_inputs_set_calls);
    SNAPSHOT_FIELD(rknn_detector_inputs_set_nanoseconds);
    SNAPSHOT_FIELD(rknn_detector_run_calls);
    SNAPSHOT_FIELD(rknn_detector_run_nanoseconds);
    SNAPSHOT_FIELD(rknn_detector_outputs_get_calls);
    SNAPSHOT_FIELD(rknn_detector_outputs_get_nanoseconds);
    SNAPSHOT_FIELD(rknn_detector_outputs_release_calls);
    SNAPSHOT_FIELD(rknn_detector_outputs_release_nanoseconds);
    SNAPSHOT_FIELD(rknn_detector_output_transform_calls);
    SNAPSHOT_FIELD(rknn_detector_output_transform_nanoseconds);
    SNAPSHOT_FIELD(rknn_detector_mutex_wait_calls);
    SNAPSHOT_FIELD(rknn_detector_mutex_wait_nanoseconds);
    SNAPSHOT_FIELD(rknn_preprocess_fast_hits);
    SNAPSHOT_FIELD(rknn_rga_fill_calls);
    SNAPSHOT_FIELD(rknn_rga_fill_nanoseconds);
    SNAPSHOT_FIELD(rknn_rga_resize_color_calls);
    SNAPSHOT_FIELD(rknn_rga_resize_color_nanoseconds);
    SNAPSHOT_FIELD(rknn_rga_small_frame_resize_calls);
    SNAPSHOT_FIELD(rknn_rga_small_frame_resize_nanoseconds);
    SNAPSHOT_FIELD(rknn_rga_small_frame_csc_calls);
    SNAPSHOT_FIELD(rknn_rga_small_frame_csc_nanoseconds);
    SNAPSHOT_FIELD(rknn_rga_crop_resize_calls);
    SNAPSHOT_FIELD(rknn_rga_crop_resize_nanoseconds);
    SNAPSHOT_FIELD(rknn_rga_crop_resize_failures);
    SNAPSHOT_FIELD(rknn_rga_crop_dmabuf_frames);
    SNAPSHOT_FIELD(rknn_rga_crop_host_fallbacks);
    SNAPSHOT_FIELD(rknn_rga_failures);
    SNAPSHOT_FIELD(rknn_cpu_resize_fallback_calls);
    SNAPSHOT_FIELD(rknn_cpu_resize_fallback_nanoseconds);
    SNAPSHOT_FIELD(rknn_cpu_crop_resize_fallback_calls);
    SNAPSHOT_FIELD(rknn_cpu_crop_resize_fallback_nanoseconds);
    SNAPSHOT_FIELD(rknn_cpu_normalize_fallback_calls);
    SNAPSHOT_FIELD(rknn_cpu_normalize_fallback_nanoseconds);
    SNAPSHOT_FIELD(rknn_native_input_map_calls);
    SNAPSHOT_FIELD(rknn_native_input_map_nanoseconds);
    SNAPSHOT_FIELD(rknn_native_int8_inputs);
    SNAPSHOT_FIELD(rknn_float_inputs);
    SNAPSHOT_FIELD(rknn_uint8_contract_inputs);
    SNAPSHOT_FIELD(rknn_input_compatibility_fallbacks);
    SNAPSHOT_FIELD(rknn_bound_input_bind_attempts);
    SNAPSHOT_FIELD(rknn_bound_input_bind_failures);
    SNAPSHOT_FIELD(rknn_bound_input_copy_calls);
    SNAPSHOT_FIELD(rknn_bound_input_copy_nanoseconds);
    SNAPSHOT_FIELD(rknn_bound_input_copy_bytes);
    SNAPSHOT_FIELD(rknn_bound_input_copy_failures);
    SNAPSHOT_FIELD(rknn_bound_input_sync_calls);
    SNAPSHOT_FIELD(rknn_bound_input_sync_nanoseconds);
    SNAPSHOT_FIELD(rknn_bound_input_sync_failures);
    SNAPSHOT_FIELD(rknn_bound_input_frames);
    SNAPSHOT_FIELD(rknn_rga_bound_input_bind_attempts);
    SNAPSHOT_FIELD(rknn_rga_bound_input_bind_failures);
    SNAPSHOT_FIELD(rknn_rga_bound_input_import_calls);
    SNAPSHOT_FIELD(rknn_rga_bound_input_import_nanoseconds);
    SNAPSHOT_FIELD(rknn_rga_bound_input_import_failures);
    SNAPSHOT_FIELD(rknn_rga_bound_input_frames);
    SNAPSHOT_FIELD(rknn_rga_bound_uint8_frames);
    SNAPSHOT_FIELD(rknn_rga_bound_native_int8_frames);
    SNAPSHOT_FIELD(rknn_rga_bound_requantize_calls);
    SNAPSHOT_FIELD(rknn_rga_bound_requantize_nanoseconds);
    SNAPSHOT_FIELD(rknn_rga_bound_requantize_failures);
    SNAPSHOT_FIELD(rknn_rga_bound_input_normalize_bypasses);
    SNAPSHOT_FIELD(rknn_mpp_dmabuf_import_calls);
    SNAPSHOT_FIELD(rknn_mpp_dmabuf_import_nanoseconds);
    SNAPSHOT_FIELD(rknn_mpp_dmabuf_import_failures);
    SNAPSHOT_FIELD(rknn_mpp_dmabuf_frames);
    SNAPSHOT_FIELD(rknn_mpp_dmabuf_fallbacks);
    SNAPSHOT_FIELD(rknn_mpp_dmabuf_source_bytes);
    SNAPSHOT_FIELD(rknn_native_int8_outputs);
    SNAPSHOT_FIELD(rknn_float_outputs);
    SNAPSHOT_FIELD(rknn_output_compatibility_fallbacks);
    SNAPSHOT_FIELD(rknn_native_output_bytes);
    SNAPSHOT_FIELD(rknn_float_output_bytes);
    SNAPSHOT_FIELD(rknn_yolov8_dfl_calls);
    SNAPSHOT_FIELD(rknn_yolov8_dfl_nanoseconds);
    SNAPSHOT_FIELD(rknn_yolov8_class_calls);
    SNAPSHOT_FIELD(rknn_yolov8_class_nanoseconds);
    SNAPSHOT_FIELD(rknn_yolov8_direct_candidate_calls);
    SNAPSHOT_FIELD(rknn_yolov8_direct_candidate_failures);
    SNAPSHOT_FIELD(rknn_yolov8_direct_points_scanned);
    SNAPSHOT_FIELD(rknn_yolov8_direct_points_decoded);
    SNAPSHOT_FIELD(rknn_yolov8_score_sum_points_rejected);
    SNAPSHOT_FIELD(rknn_yolov8_logical_float_bytes_avoided);
    SNAPSHOT_FIELD(yolov8_postprocess_calls);
    SNAPSHOT_FIELD(yolov8_postprocess_nanoseconds);
    SNAPSHOT_FIELD(yolov8_nms_calls);
    SNAPSHOT_FIELD(yolov8_nms_nanoseconds);
#undef SNAPSHOT_FIELD
    return snapshot;
}

InferencePipelineMetrics& GetInferencePipelineMetrics() {
    static InferencePipelineMetrics metrics;
    return metrics;
}

}  // namespace cosmo::nn
