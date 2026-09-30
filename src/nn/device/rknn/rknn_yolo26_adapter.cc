#ifdef COSMO_NN_USE_RKNN_BACKEND

#include "nn/device/rknn/rknn_yolo26_adapter.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace cosmo::nn {
namespace {

    constexpr int kMaxDetections = 300;

    struct Anchor {
        float max_logit;
        int branch;
        int point;
        int global_index;
    };

    struct Candidate {
        float logit;
        int anchor_index;
        int class_id;
    };

    float Sigmoid(float value) {
        if (value >= 0.0f)
            return 1.0f / (1.0f + std::exp(-value));
        const float exponential = std::exp(value);
        return exponential / (1.0f + exponential);
    }

    bool BetterAnchor(const Anchor& left, const Anchor& right) {
        if (left.max_logit != right.max_logit)
            return left.max_logit > right.max_logit;
        return left.global_index < right.global_index;
    }

    bool BetterCandidate(const Candidate& left, const Candidate& right) {
        if (left.logit != right.logit)
            return left.logit > right.logit;
        if (left.anchor_index != right.anchor_index)
            return left.anchor_index < right.anchor_index;
        return left.class_id < right.class_id;
    }

}  // namespace

bool DetectRknnYolo26Layout(const std::vector<std::vector<int>>& shapes, RknnYolo26Layout& layout,
                            std::string& error) {
    layout = {};
    if (shapes.size() != 6) {
        error = "YOLO26 Detect requires six box/class heads";
        return false;
    }
    int previous_height = std::numeric_limits<int>::max();
    for (size_t branch = 0; branch < 3; ++branch) {
        const auto& box = shapes[2 * branch];
        const auto& cls = shapes[2 * branch + 1];
        if (box.size() != 4 || cls.size() != 4 || box[0] != 1 || cls[0] != 1 || box[1] != 4 || cls[1] <= 0 ||
            box[2] <= 0 || box[3] <= 0 || box[2] != cls[2] || box[3] != cls[3] || box[2] >= previous_height ||
            (layout.class_count && layout.class_count != cls[1])) {
            error = "YOLO26 Detect heads require ordered NCHW box[4]/class[nc] pairs";
            return false;
        }
        if (box[2] > std::numeric_limits<int>::max() / box[3] ||
            layout.point_count > std::numeric_limits<int>::max() - box[2] * box[3]) {
            error = "YOLO26 Detect point count overflows";
            return false;
        }
        layout.class_count = cls[1];
        layout.point_count += box[2] * box[3];
        previous_height = box[2];
    }
    layout.logical_shape = {1, kMaxDetections, 6};
    error.clear();
    return true;
}

bool ReconstructRknnYolo26(const std::vector<RknnYolo26Head>& heads, int input_height, int input_width,
                           float* output, size_t output_count, std::string& error) {
    std::vector<std::vector<int>> shapes;
    shapes.reserve(heads.size());
    for (const auto& head : heads)
        shapes.push_back(head.shape);
    RknnYolo26Layout layout;
    if (!DetectRknnYolo26Layout(shapes, layout, error))
        return false;
    if (!output || output_count < static_cast<size_t>(kMaxDetections * 6) || input_height <= 0 ||
        input_width <= 0) {
        error = "YOLO26 Detect output buffer or input dimensions are invalid";
        return false;
    }
    for (size_t index = 0; index < heads.size(); ++index) {
        const auto& head      = heads[index];
        const size_t expected = static_cast<size_t>(head.shape[1]) * head.shape[2] * head.shape[3];
        if (!head.data || head.element_count != expected) {
            error = "YOLO26 Detect head buffer does not match its shape";
            return false;
        }
    }

    std::fill(output, output + kMaxDetections * 6, 0.0f);
    std::vector<Anchor> anchors;
    anchors.reserve(layout.point_count);
    for (int branch = 0; branch < 3; ++branch) {
        const auto& cls  = heads[2 * branch + 1];
        const int points = cls.shape[2] * cls.shape[3];
        for (int point = 0; point < points; ++point) {
            float max_logit = -std::numeric_limits<float>::infinity();
            for (int class_id = 0; class_id < layout.class_count; ++class_id) {
                const float logit = cls.data[class_id * points + point];
                if (std::isfinite(logit))
                    max_logit = std::max(max_logit, logit);
            }
            anchors.push_back({max_logit, branch, point, static_cast<int>(anchors.size())});
        }
    }
    const size_t anchor_count = std::min(anchors.size(), static_cast<size_t>(kMaxDetections));
    std::partial_sort(anchors.begin(), anchors.begin() + anchor_count, anchors.end(), BetterAnchor);
    anchors.resize(anchor_count);

    std::vector<Candidate> candidates;
    candidates.reserve(anchor_count * static_cast<size_t>(layout.class_count));
    for (size_t anchor_index = 0; anchor_index < anchor_count; ++anchor_index) {
        const auto& anchor = anchors[anchor_index];
        const auto& cls    = heads[2 * anchor.branch + 1];
        const int points   = cls.shape[2] * cls.shape[3];
        for (int class_id = 0; class_id < layout.class_count; ++class_id) {
            const float logit = cls.data[class_id * points + anchor.point];
            if (std::isfinite(logit))
                candidates.push_back({logit, static_cast<int>(anchor_index), class_id});
        }
    }
    const size_t detection_count = std::min(candidates.size(), static_cast<size_t>(kMaxDetections));
    std::partial_sort(candidates.begin(), candidates.begin() + detection_count, candidates.end(),
                      BetterCandidate);
    for (size_t index = 0; index < detection_count; ++index) {
        const auto& candidate = candidates[index];
        const auto& anchor    = anchors[candidate.anchor_index];
        const auto& box       = heads[2 * anchor.branch];
        const int height      = box.shape[2];
        const int width       = box.shape[3];
        const int points      = height * width;
        const float center_x  = static_cast<float>(anchor.point % width) + 0.5f;
        const float center_y  = static_cast<float>(anchor.point / width) + 0.5f;
        const float stride_x  = static_cast<float>(input_width) / width;
        const float stride_y  = static_cast<float>(input_height) / height;
        float* row            = output + index * 6;
        row[0]                = (center_x - box.data[anchor.point]) * stride_x;
        row[1]                = (center_y - box.data[points + anchor.point]) * stride_y;
        row[2]                = (center_x + box.data[2 * points + anchor.point]) * stride_x;
        row[3]                = (center_y + box.data[3 * points + anchor.point]) * stride_y;
        row[4]                = Sigmoid(candidate.logit);
        row[5]                = static_cast<float>(candidate.class_id);
    }
    error.clear();
    return true;
}

}  // namespace cosmo::nn

#endif  // COSMO_NN_USE_RKNN_BACKEND
