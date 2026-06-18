#ifdef COSMO_NN_USE_CPU_BACKEND

#include "nn/device/cpu/cpu_affine_crop_node.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "nn/node/node_type_utils.h"
#include "nn/utils/dims_vector_utils.h"
#include "nn/utils/op.h"
#include "nn/utils/rect.h"

namespace cosmo::nn {

CpuAffineCropNode::CpuAffineCropNode() : Node() {
    node_type     = NodeType::NODE_AFFINE_CROP;
    name          = NodeTypeUtils::NodeTypeToStr(NODE_AFFINE_CROP).append("_0");
    one_blob_only = false;
}

void CpuAffineCropNode::LoadParam(Op* op) {
    if (!op)
        return;
    auto affine_crop_op = dynamic_cast<AffineCrop*>(op);
    if (!affine_crop_op)
        return;

    norm_ratio   = affine_crop_op->norm_ratio;
    norm_mode    = affine_crop_op->norm_mode;
    center_index = affine_crop_op->center_index;
    dst_height   = affine_crop_op->output_hw.at(0);
    dst_width    = affine_crop_op->output_hw.at(1);
}

DeviceType CpuAffineCropNode::GetTopBlobDeviceType() {
    return DeviceType::DEVICE_NAIVE;
}

Status CpuAffineCropNode::InferTopShapes() {
    shared_resource->net_input_w = dst_width;
    shared_resource->net_input_h = dst_height;

    top_blob_shapes     = {{max_batch, dst_height, dst_width, 3}};  // NHWC
    top_blob_data_types = {DataType::DATA_TYPE_UINT8};
    return COSMO_NN_OK;
}

size_t CpuAffineCropNode::GetTopCount() {
    return 1;
}

size_t CpuAffineCropNode::GetBottomCount() {
    return 2;
}

Status CpuAffineCropNode::CalculateMatrix(float* landmark, std::vector<float>& matrix) {
    Point_2f left_eye(landmark[0], landmark[1]);
    Point_2f right_eye(landmark[2], landmark[3]);
    Point_2f nose(landmark[4], landmark[5]);
    Point_2f left_mouth(landmark[6], landmark[7]);
    Point_2f right_mouth(landmark[8], landmark[9]);

    std::vector<Point_2f> key_points = {left_eye, right_eye, nose, left_mouth, right_mouth};

    Point_2f src_center(0.f, 0.f);
    if (center_index.size() <= 1) {
        src_center.x = (left_eye.x + right_eye.x + left_mouth.x + right_mouth.x) / 4;
        src_center.y = (left_eye.y + right_eye.y + left_mouth.y + right_mouth.y) / 4;
    } else {
        for (int index : center_index) {
            src_center.x += key_points.at(index).x;
            src_center.y += key_points.at(index).y;
        }
        src_center.x /= center_index.size();
        src_center.y /= center_index.size();
    }

    Point_2f dst_center(dst_width / 2.f, dst_height / 2.f);
    float norm_standard_len = std::max(dst_width, dst_height) * norm_ratio;

    float actual_len = norm_standard_len;
    switch (norm_mode) {
        case 0: {  // AVE_LE2LM_RE2RM
            float d1   = std::sqrt(std::pow(left_eye.x - left_mouth.x, 2.0f) +
                                   std::pow(left_eye.y - left_mouth.y, 2.0f));
            float d2   = std::sqrt(std::pow(right_eye.x - right_mouth.x, 2.0f) +
                                   std::pow(right_eye.y - right_mouth.y, 2.0f));
            actual_len = (d1 + d2) / 2.f;
        } break;
        case 1: {  // RECT_LE_RE_LM_RM
            float left   = std::min({left_eye.x, right_eye.x, left_mouth.x, right_mouth.x});
            float top    = std::min({left_eye.y, right_eye.y, left_mouth.y, right_mouth.y});
            float right  = std::max({left_eye.x, right_eye.x, left_mouth.x, right_mouth.x});
            float bottom = std::max({left_eye.y, right_eye.y, left_mouth.y, right_mouth.y});
            float w      = right - left;
            float h      = bottom - top;
            actual_len   = std::sqrt((w * w + h * h) / 2);
        } break;
        case 2: {  // LE2RE
            float dx   = left_eye.x - right_eye.x;
            float dy   = left_eye.y - right_eye.y;
            actual_len = std::sqrt(dx * dx + dy * dy);
        } break;
        default:
            return Status(COSMO_NN_ERR_PARAM, "norm_mode error");
    }

    float scale = actual_len / norm_standard_len;
    float angle = -std::atan2((right_eye.y - left_eye.y), (right_eye.x - left_eye.x));

    float a1 = scale * std::cos(angle);
    float a2 = scale * std::sin(angle);
    float a3 = -a2;
    float a4 = a1;

    float tx = src_center.x - a1 * dst_center.x - a2 * dst_center.y;
    float ty = src_center.y - a3 * dst_center.x - a4 * dst_center.y;

    matrix.push_back(a1);
    matrix.push_back(a2);
    matrix.push_back(tx);
    matrix.push_back(a3);
    matrix.push_back(a4);
    matrix.push_back(ty);

    return COSMO_NN_OK;
}

Status CpuAffineCropNode::Forward(std::vector<std::shared_ptr<Blob>>& image_blobs,
                                  std::vector<std::shared_ptr<Blob>>& landmark_blobs,
                                  std::vector<std::shared_ptr<Blob>>& top_blobs) {
    timer.Start();

    if (image_blobs.empty() || landmark_blobs.empty())
        return Status(COSMO_NN_ERR_INVALID_INPUT, "empty input blobs");

    auto img_blob  = image_blobs.at(0);
    auto img_dims  = img_blob->GetBlobDesc().dims;
    int src_h      = img_dims.at(1);
    int src_w      = img_dims.at(2);
    int channels   = (img_dims.size() > 3) ? img_dims.at(3) : 3;
    auto* src_data = static_cast<const uint8_t*>(img_blob->GetHandle().base);

    auto lm_blob = landmark_blobs.at(0);
    auto lm_desc = lm_blob->GetBlobDesc();
    if (lm_desc.data_type != DataType::DATA_TYPE_FLOAT)
        return Status(COSMO_NN_ERR_INVALID_INPUT, "landmark data type must be float");

    int n_landmarks = lm_desc.dims.at(0);
    float* lm_data  = static_cast<float*>(lm_blob->GetHandle().base);

    auto top_blob = top_blobs.at(0);
    auto top_desc = top_blob->GetBlobDesc();
    auto top_dim  = top_desc.dims;
    top_dim.at(0) = n_landmarks;
    top_desc.dims = top_dim;
    top_blob->SetBlobDesc(top_desc);

    auto* top_data  = static_cast<uint8_t*>(top_blob->GetHandle().base);
    int single_size = dst_height * dst_width * channels;

    for (int i = 0; i < n_landmarks; i++) {
        std::vector<float> matrix;
        RETURN_ON_FAIL(CalculateMatrix(lm_data + i * kLandmarkDataLen, matrix));

        // matrix = [a1, a2, tx, a3, a4, ty]
        // src_point = M * dst_point:
        //   src_x = a1 * dx + a2 * dy + tx
        //   src_y = a3 * dx + a4 * dy + ty
        float a1 = matrix[0], a2 = matrix[1], tx = matrix[2];
        float a3 = matrix[3], a4 = matrix[4], ty = matrix[5];

        uint8_t* dst = top_data + i * single_size;

        for (int dy = 0; dy < dst_height; dy++) {
            for (int dx = 0; dx < dst_width; dx++) {
                float src_xf = a1 * dx + a2 * dy + tx;
                float src_yf = a3 * dx + a4 * dy + ty;

                int sx   = static_cast<int>(src_xf);
                int sy   = static_cast<int>(src_yf);
                float fx = src_xf - sx;
                float fy = src_yf - sy;

                for (int c = 0; c < channels; c++) {
                    float val = 0.0f;
                    if (sx >= 0 && sx < src_w - 1 && sy >= 0 && sy < src_h - 1) {
                        float v00 = src_data[(sy * src_w + sx) * channels + c];
                        float v01 = src_data[(sy * src_w + sx + 1) * channels + c];
                        float v10 = src_data[((sy + 1) * src_w + sx) * channels + c];
                        float v11 = src_data[((sy + 1) * src_w + sx + 1) * channels + c];
                        val       = v00 * (1 - fx) * (1 - fy) + v01 * fx * (1 - fy) + v10 * (1 - fx) * fy +
                              v11 * fx * fy;
                    }
                    dst[(dy * dst_width + dx) * channels + c] =
                        static_cast<uint8_t>(std::min(255.0f, std::max(0.0f, val + 0.5f)));
                }
            }
        }
    }

    timer.Stop();
    return COSMO_NN_OK;
}

Status CpuAffineCropNode::Forward(std::vector<std::shared_ptr<Blob>>& /*bottom_blobs*/,
                                  std::vector<std::shared_ptr<Blob>>& /*top_blobs*/) {
    return COSMO_NN_OK;
}

}  // namespace cosmo::nn

#endif  // COSMO_NN_USE_CPU_BACKEND
