#pragma once

#ifdef COSMO_NN_USE_RKNN_BACKEND

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include "nn/core/shared_resource.h"

namespace cosmo::nn {

enum class RknnOutputAdapterKind : uint8_t {
    GenericTensorV1 = 0,
    YoloAnchor3HeadV1,
    YoloDfl6HeadV1,
    YoloDfl9HeadScoreSumV1,
    Yolo26OneToOne6HeadV1,
    YoloPoseV1,
    YoloSegV1,
    YoloObbV1,
};

struct RknnOutputAdapterRegistryEntry {
    RknnOutputAdapterKind kind{RknnOutputAdapterKind::GenericTensorV1};
    const char* name{nullptr};
    bool implemented{false};
    bool auto_detect{false};
};

struct RknnYolov8BranchContract {
    static constexpr size_t kNoTensor = std::numeric_limits<size_t>::max();

    size_t box_index{kNoTensor};
    size_t class_index{kNoTensor};
    size_t score_sum_index{kNoTensor};
    int height{0};
    int width{0};

    [[nodiscard]] bool HasScoreSum() const {
        return score_sum_index != kNoTensor;
    }
};

struct RknnOutputAdapterContract {
    RknnOutputAdapterKind kind{RknnOutputAdapterKind::GenericTensorV1};
    int class_count{0};
    int point_count{0};
    std::vector<int> logical_shape;
    std::array<RknnYolov8BranchContract, 3> branches{};
    bool class_scores_are_probabilities{false};
};

const std::array<RknnOutputAdapterRegistryEntry, 8>& RknnOutputAdapterRegistry();
const char* RknnOutputAdapterName(RknnOutputAdapterKind kind);
bool ResolveRknnOutputAdapter(const std::vector<std::vector<int>>& shapes,
                              RknnOutputAdapterContract& contract, std::string& error);
bool IsRknnYolov8DflAdapter(RknnOutputAdapterKind kind);

bool IsRknnYolo26OneToOneAdapter(RknnOutputAdapterKind kind);

struct RknnYolov8Head {
    const float* data{nullptr};
    size_t element_count{0};
    std::vector<int> shape;
};

struct RknnYolov8QuantizedHead {
    const int8_t* data{nullptr};
    size_t element_count{0};
    std::vector<int> shape;
    int32_t zero_point{0};
    float scale{0.0f};
};

struct RknnYolov8TransformTiming {
    uint64_t dfl_nanoseconds{0};
    uint64_t class_nanoseconds{0};
};

struct RknnYolov8CandidateTiming {
    uint64_t dfl_nanoseconds{0};
    uint64_t class_nanoseconds{0};
    uint64_t points_scanned{0};
    uint64_t points_decoded{0};
    uint64_t score_sum_points_rejected{0};
};

struct RknnYolov8CandidateScratch {
    std::vector<int8_t> class_max;
    std::vector<int> class_ids;
    std::vector<int> active_points;
};

using RknnYolov8Layout = RknnOutputAdapterContract;

bool DetectRknnYolov8Layout(const std::vector<std::vector<int>>& shapes, RknnYolov8Layout& layout,
                            std::string& error);

bool ReconstructRknnYolov8(const std::vector<RknnYolov8Head>& heads, int input_height, int input_width,
                           float* output, size_t output_count, std::string& error);

bool ReconstructRknnYolov8Quantized(const std::vector<RknnYolov8QuantizedHead>& heads, int input_height,
                                    int input_width, float* output, size_t output_count, std::string& error,
                                    RknnYolov8TransformTiming* timing = nullptr);

bool DecodeRknnYolov8QuantizedCandidates(const std::vector<RknnYolov8QuantizedHead>& heads, int input_height,
                                         int input_width, float confidence_threshold,
                                         RknnYolov8CandidateScratch& scratch,
                                         std::vector<Yolov8Candidate>& candidates, std::string& error,
                                         RknnYolov8CandidateTiming* timing = nullptr);

}  // namespace cosmo::nn

#endif  // COSMO_NN_USE_RKNN_BACKEND
