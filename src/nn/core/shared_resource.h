#pragma once

#ifdef COSMO_NN_USE_SOPHON_BACKEND
#include "bmruntime_cpp.h"
#include "bmruntime_interface.h"
#endif

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "nn/core/abstract_context.h"

namespace cosmo::nn {

struct Yolov8DirectPostprocessConfig {
    bool configured{false};
    float confidence_threshold{0.0f};
    int input_width{0};
    int input_height{0};
};

struct Yolov8Candidate {
    float x{0.0f};
    float y{0.0f};
    float width{0.0f};
    float height{0.0f};
    float confidence{0.0f};
    int class_id{-1};
};

struct Yolov8CandidateBatch {
    bool ready{false};
    std::vector<Yolov8Candidate> candidates;

    void Reset() {
        ready = false;
        candidates.clear();
    }
};

class RknnBoundInputProvider {
public:
    virtual ~RknnBoundInputProvider()                                            = default;
    virtual bool EnsureRgaBoundInput(int height, int width, std::string& reason) = 0;
};

// Optional graph-local sidecar for RKNN/RGA input hand-off. The provider owns
// the memory; preprocessing only imports the fd and marks the current frame
// ready. Graph instances are exclusively leased while Forward runs.
struct RknnBoundInputTarget {
    RknnBoundInputProvider* owner{nullptr};
    void* virtual_address{nullptr};
    int fd{-1};
    size_t bytes{0};
    int height{0};
    int width{0};
    int channels{0};
    int width_stride{0};
    uint64_t generation{0};
    bool frame_ready{false};

    [[nodiscard]] bool Matches(int expected_height, int expected_width) const {
        if (!owner || !virtual_address || fd < 0 || height != expected_height || width != expected_width ||
            height <= 0 || width <= 0 || channels != 3 || width_stride < width) {
            return false;
        }
        const auto required = static_cast<uint64_t>(height) * static_cast<uint64_t>(width_stride) *
                              static_cast<uint64_t>(channels);
        return required <= bytes;
    }

    void Reset() {
        *this = {};
    }
};

class SharedResource {
public:
    explicit SharedResource(int i = 0) noexcept(false);

    ~SharedResource();

public:
#ifdef COSMO_NN_USE_SOPHON_BACKEND
    // Non-owning. SophonDevice owns this process-lifetime handle so destroying
    // one task graph cannot invalidate concurrently active media contexts.
    bm_handle_t m_handle = nullptr;
#endif

    int current_device_id = 0;

    // dino
    void* tokenizer_handle = nullptr;
    std::string tokenizer_path{};
    std::vector<int32_t> prompt_token_ids{};
    float text_threshold = 0.0f;
    float box_threshold  = 0.0f;

    // sophon
    int net_input_w         = 0;
    int net_input_h         = 0;
    float model_input_scale = 1.0f;  // for INT8 quantized models

    // Optional producer/consumer hint. Only compatible RKNN YOLOv8 native-output
    // graphs set this; every other backend keeps the established box-major scan.
    bool prefer_yolov8_class_major_scan = false;

    // Optional producer/consumer capability for a backend that can decode native
    // YOLOv8 heads directly into candidates. The graph instance is exclusively
    // leased by InstancePool while Forward runs, so this sidecar has the same
    // lifetime and concurrency boundary as the graph's ordinary BlobStore.
    Yolov8DirectPostprocessConfig yolov8_direct_postprocess{};
    Yolov8CandidateBatch yolov8_candidate_batch{};

    RknnBoundInputProvider* rknn_bound_input_provider{nullptr};
    RknnBoundInputTarget rknn_bound_input_target{};
    // Set by the graph's normalize node after shape inference. Producers may
    // write directly into the RKNN input DMA-BUF only when the complete
    // preprocessing contract (RGB UINT8, 0..255 -> 0..1) is compatible.
    bool rknn_bound_input_preprocess_compatible{false};
};

}  // namespace cosmo::nn
