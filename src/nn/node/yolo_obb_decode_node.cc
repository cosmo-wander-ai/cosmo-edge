#include "nn/node/yolo_obb_decode_node.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "nn/node/node_type_utils.h"
#include "nn/utils/dims_vector_utils.h"

namespace cosmo::nn {
namespace {

    bool ValidFloatTensor(const std::shared_ptr<Blob>& blob, int columns) {
        if (!blob || !blob->GetHandle().base)
            return false;
        const auto& desc = blob->GetBlobDesc();
        return UsesHostMemory(desc.device_type) && desc.data_type == DATA_TYPE_FLOAT &&
               desc.dims.size() == 3 && desc.dims[0] > 0 && desc.dims[1] > 0 && desc.dims[2] == columns &&
               DimsVectorUtils::Count(desc.dims) > 0;
    }

    bool ValidClassId(float class_id) {
        return std::isfinite(class_id) && class_id >= 0.f &&
               static_cast<double>(class_id) <= std::numeric_limits<int>::max() &&
               std::floor(class_id) == class_id;
    }

}  // namespace

YoloObbDecodeNode::YoloObbDecodeNode() : Node() {
    node_type     = NodeType::NODE_YOLO_OBB_DECODE;
    name          = NodeTypeUtils::NodeTypeToStr(NODE_YOLO_OBB_DECODE).append("_0");
    one_blob_only = true;
}

YoloObbDecodeNode::~YoloObbDecodeNode() = default;

void YoloObbDecodeNode::LoadParam(Op* op) {
    auto* post    = dynamic_cast<YoloPost*>(op);
    valid_params_ = false;
    if (!post)
        return;
    top_k                   = post->top_k;
    base_conf               = post->nms_detection_conf;
    input_width_            = post->input_width;
    input_height_           = post->input_height;
    normalized_coordinates_ = post->normalized_coordinates;
    valid_params_           = top_k > 0 && std::isfinite(base_conf) && base_conf >= 0.f && base_conf <= 1.f &&
                    (!normalized_coordinates_ || (input_width_ > 0 && input_height_ > 0));
}

Status YoloObbDecodeNode::InferTopShapes() {
    if (!valid_params_ || max_batch <= 0 || DimsVectorUtils::Count({max_batch, top_k, kOutputColumns}) <= 0)
        return Status(COSMO_NN_ERR_PARAM, "Invalid OBB decoder parameters");
    top_blob_shapes     = {{max_batch, top_k, kOutputColumns}};
    top_blob_data_types = {DATA_TYPE_FLOAT};
    return COSMO_NN_OK;
}

size_t YoloObbDecodeNode::GetBottomCount() {
    return 1;
}
size_t YoloObbDecodeNode::GetTopCount() {
    return 1;
}

Status YoloObbDecodeNode::Forward(std::vector<std::shared_ptr<Blob>>& bottom_blobs,
                                  std::vector<std::shared_ptr<Blob>>& top_blobs) {
    if (!valid_params_ || bottom_blobs.size() != 1 || top_blobs.size() != 1 ||
        !ValidFloatTensor(bottom_blobs[0], 7) || !ValidFloatTensor(top_blobs[0], kOutputColumns))
        return Status(COSMO_NN_ERR_INVALID_INPUT, "OBB expects float32 [batch, rows, 7] and corner output");

    const auto& input_dims  = bottom_blobs[0]->GetBlobDesc().dims;
    const auto& output_dims = top_blobs[0]->GetBlobDesc().dims;
    const int batch         = input_dims[0];
    const int rows          = input_dims[1];
    if (batch > max_batch || output_dims[1] != top_k || output_dims[0] < batch)
        return Status(COSMO_NN_ERR_INVALID_INPUT, "OBB output capacity is insufficient");

    timer.Start();
    const auto* input = static_cast<const float*>(bottom_blobs[0]->GetHandle().base);
    auto* output      = static_cast<float*>(top_blobs[0]->GetHandle().base);
    std::fill(output, output + DimsVectorUtils::Count(output_dims), 0.f);
    for (int b = 0; b < batch; ++b) {
        int kept = 0;
        for (int i = 0; i < rows && kept < top_k; ++i) {
            const auto* row = input + (b * rows + i) * 7;
            if (!std::all_of(row, row + 7, [](float value) { return std::isfinite(value); }) ||
                row[2] <= 0.f || row[3] <= 0.f || row[4] < base_conf || row[4] > 1.f || !ValidClassId(row[5]))
                continue;
            const float sx = normalized_coordinates_ ? static_cast<float>(input_width_) : 1.f;
            const float sy = normalized_coordinates_ ? static_cast<float>(input_height_) : 1.f;
            const auto quad =
                util::MakeOrientedQuad(row[0] * sx, row[1] * sy, row[2] * sx, row[3] * sy, row[6]);
            if (!std::all_of(quad.begin(), quad.end(),
                             [](const auto& p) { return std::isfinite(p.x) && std::isfinite(p.y); }))
                continue;
            auto* dst = output + (b * top_k + kept) * kOutputColumns;
            for (size_t corner = 0; corner < quad.size(); ++corner) {
                dst[corner * 2]     = quad[corner].x;
                dst[corner * 2 + 1] = quad[corner].y;
            }
            dst[8] = row[4];
            dst[9] = row[5];
            ++kept;
        }
    }
    // Keep the allocated output batch dimension: padded batches remain zero,
    // and the parser uses the count of actual source-image transforms.
    timer.Stop();
    return COSMO_NN_OK;
}

Status ParseYoloObbOutput(const std::shared_ptr<Blob>& blob,
                          const std::vector<ObbResizeTransform>& transforms,
                          const std::vector<int>& selected_indices,
                          const std::vector<float>& selected_thresholds,
                          const std::vector<std::string>& selected_labels,
                          std::vector<std::vector<ObjectInfoV1>>& outputs, float default_threshold) {
    outputs.clear();
    if (!std::isfinite(default_threshold) || default_threshold < 0.f || default_threshold > 1.f ||
        !ValidFloatTensor(blob, 10) || transforms.empty() ||
        transforms.size() > static_cast<size_t>(blob->GetBlobDesc().dims[0]))
        return Status(COSMO_NN_ERR_INVALID_INPUT, "Invalid OBB corner tensor or source-image batch");
    for (const auto& t : transforms) {
        if (!std::isfinite(t.scale_x) || !std::isfinite(t.scale_y) || !std::isfinite(t.offset_x) ||
            !std::isfinite(t.offset_y) || t.scale_x <= 0.f || t.scale_y <= 0.f)
            return Status(COSMO_NN_ERR_INVALID_INPUT, "Invalid OBB preprocessing transform");
    }
    const int rows   = blob->GetBlobDesc().dims[1];
    const auto* data = static_cast<const float*>(blob->GetHandle().base);
    for (size_t b = 0; b < transforms.size(); ++b) {
        const auto& transform = transforms[b];
        std::vector<ObjectInfoV1> objects;
        for (int i = 0; i < rows; ++i) {
            const float* row = data + (b * rows + i) * 10;
            if (!std::all_of(row, row + 10, [](float value) { return std::isfinite(value); }) ||
                row[8] <= 0.f || row[8] > 1.f || !ValidClassId(row[9]))
                continue;
            const int class_id  = static_cast<int>(row[9]);
            const auto selected = std::find(selected_indices.begin(), selected_indices.end(), class_id);
            if (!selected_indices.empty() && selected == selected_indices.end())
                continue;
            const auto index      = static_cast<size_t>(selected - selected_indices.begin());
            const float threshold = !selected_indices.empty() && index < selected_thresholds.size()
                                        ? selected_thresholds[index]
                                        : default_threshold;
            if (!std::isfinite(threshold) || row[8] < threshold)
                continue;
            util::Quad corners;
            for (size_t c = 0; c < corners.size(); ++c) {
                corners[c].x = (row[c * 2] - transform.offset_x) / transform.scale_x;
                corners[c].y = (row[c * 2 + 1] - transform.offset_y) / transform.scale_y;
            }
            if (!std::all_of(corners.begin(), corners.end(),
                             [](const auto& p) { return std::isfinite(p.x) && std::isfinite(p.y); }))
                continue;
            const auto bounds = util::QuadBounds(corners);
            if (bounds.x2 <= bounds.x1 || bounds.y2 <= bounds.y1)
                continue;
            ObjectInfoV1 object;
            object.x1               = bounds.x1;
            object.y1               = bounds.y1;
            object.x2               = bounds.x2;
            object.y2               = bounds.y2;
            object.oriented_corners = corners;
            ClassifyInfo info;
            info.class_id   = class_id;
            info.confidence = row[8];
            info.class_name = !selected_indices.empty() && index < selected_labels.size()
                                  ? selected_labels[index]
                                  : "class_" + std::to_string(class_id);
            object.infos.push_back(std::move(info));
            objects.push_back(std::move(object));
        }
        outputs.push_back(std::move(objects));
    }
    return COSMO_NN_OK;
}

}  // namespace cosmo::nn
