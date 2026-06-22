#include "nn/device/sophon/sophon_affine_crop_node.h"

#include <cstring>
#include <iostream>

#include "bmcv_api.h"
#include "bmcv_api_ext.h"
#include "bmlib_runtime.h"
#include "bmruntime_cpp.h"
#include "nn/device/sophon/sophon_node.h"
#include "nn/node/node_type_utils.h"
#include "nn/utils/dims_vector_utils.h"

namespace cosmo::nn {

SophonAffineCropNode::SophonAffineCropNode() : Node() {
    node_type     = NodeType::NODE_AFFINE_CROP;
    name          = NodeTypeUtils::NodeTypeToStr(NODE_AFFINE_CROP).append("_0");
    one_blob_only = false;
}

SophonAffineCropNode::~SophonAffineCropNode() {}

void SophonAffineCropNode::LoadParam(Op* op) {
    if (!op)
        return;

    auto affine_crop_op = dynamic_cast<AffineCrop*>(op);

    norm_ratio   = affine_crop_op->norm_ratio;
    norm_mode    = affine_crop_op->norm_mode;
    center_index = affine_crop_op->center_index;
    dst_height   = affine_crop_op->output_hw.at(0);
    dst_width    = affine_crop_op->output_hw.at(1);
}

DeviceType SophonAffineCropNode::GetTopBlobDeviceType() {
    return DeviceType::DEVICE_SOPHON_TPU;
}

Status SophonAffineCropNode::InferTopShapes() {
    shared_resource->net_input_w = dst_width;
    shared_resource->net_input_h = dst_height;

    int aligned_w       = ALIGN(dst_width, 64);
    top_blob_shapes     = {{max_batch, dst_height, aligned_w, 3}};
    top_blob_data_types = {DataType::DATA_TYPE_UINT8};

    return COSMO_NN_OK;
}

size_t SophonAffineCropNode::GetTopCount() {
    return 1;
}

size_t SophonAffineCropNode::GetBottomCount() {
    return 2;
}

Status SophonAffineCropNode::Forward(std::vector<std::shared_ptr<Blob>>& image_blobs,
                                     std::vector<std::shared_ptr<Blob>>& landmark_blobs,
                                     std::vector<std::shared_ptr<Blob>>& top_blobs) {
    timer.Start();
    RETURN_ON_FAIL(CheckNodeInputOutput(image_blobs, top_blobs, true));
    RETURN_ON_FAIL(CheckNodeInputOutput(landmark_blobs, top_blobs, false));

    auto top_blob = top_blobs.at(0);
    auto top_desc = top_blob->GetBlobDesc();

    // check image format
    auto bottom_fmt = image_blobs.at(0)->GetBlobDesc().image_format;
    if (ImageFormatIsGray(bottom_fmt))
        top_desc.image_format = IMAGE_GRAY;
    else if (ImageFormatIsRGB(bottom_fmt))
        top_desc.image_format = IMAGE_RGB;
    else if (ImageFormatIsBGR(bottom_fmt))
        top_desc.image_format = IMAGE_BGR;
    else
        return Status(COSMO_NN_ERR_INVALID_INPUT, "image format not support");

    top_blob->SetBlobDesc(top_desc);

    RETURN_ON_FAIL(Forward(image_blobs, landmark_blobs, top_blob));

    timer.Stop();
    return COSMO_NN_OK;
}

Status SophonAffineCropNode::Forward(std::vector<std::shared_ptr<Blob>>& image_blobs,
                                     std::vector<std::shared_ptr<Blob>>& landmark_blobs,
                                     std::shared_ptr<Blob> top_blob) {
    bm_status_t ret           = BM_SUCCESS;
    bm_handle_t pbmhandle     = shared_resource->m_handle;
    auto image_count          = image_blobs.size();
    auto landmark_image_count = landmark_blobs.size();
    if (image_count != landmark_image_count)
        return Status(COSMO_NN_ERR_INVALID_INPUT, "image num is not equal to landmark_image_count num");

    int current_batch = 0;
    for (size_t i = 0; i < landmark_image_count; ++i) {
        auto landmark_blob      = landmark_blobs.at(i);
        auto landmark_blob_desc = landmark_blob->GetBlobDesc();
        auto landmark_blob_dims = landmark_blob_desc.dims;

        current_batch += landmark_blob_dims.at(0);
    }

    if (current_batch > max_batch)
        return Status(COSMO_NN_ERR_INVALID_INPUT, "current_batch size is larger than max batch size");

    // set top shape
    SetCurrentBatch(top_blob, current_batch);

    auto top_blob_handle = top_blob->GetHandle();
    auto top_blob_dims   = top_blob->GetBlobDesc().dims;

    auto image_blob        = image_blobs.at(0);
    auto image_blob_desc   = image_blob->GetBlobDesc();
    auto image_blob_handle = image_blob->GetHandle();
    auto image_blob_dims   = image_blob_desc.dims;
    if (image_blob_dims.at(0) != 1)
        return Status(COSMO_NN_ERR_INVALID_INPUT, "image batch size must be 1");

    uint32_t src_width  = image_blob_dims.at(2);
    uint32_t src_height = image_blob_dims.at(1);
    uint32_t dst_width  = top_blob_dims.at(2);
    uint32_t dst_height = top_blob_dims.at(1);

    auto landmark_blob = landmark_blobs.at(0);
    RETURN_ON_FAIL(PrepareAffineCropMatrixs(landmark_blob));

    bm_device_mem_t* src_dev_mem = reinterpret_cast<bm_device_mem_t*>(image_blob_handle.base);
    bm_device_mem_t* dst_dev_mem = reinterpret_cast<bm_device_mem_t*>(top_blob_handle.base);

    bm_image src_image;
    ret =
        bm_image_create(pbmhandle, src_height, src_width, BM_IMAGE_FORMAT, DATA_TYPE_EXT_1N_BYTE, &src_image);
    if (ret != BM_SUCCESS) {
        return Status(COSMO_NN_ERR_SOPHON_BMIMAGE_CREAT_FAILED, "bm_image creat failed.");
    }
    ret = bm_image_attach(src_image, src_dev_mem);
    if (ret != BM_SUCCESS) {
#ifdef COSMO_NN_SOPHON_1684X
        bm_image_destroy(src_image);
#else
        bm_image_destroy(&src_image);
#endif
        return Status(COSMO_NN_ERR_SOPHON_ATTACH_FAILED, "bm_image attach failed.");
    }

    bm_image_format_ext image_format   = FORMAT_BGR_PLANAR;
    bm_image_data_format_ext data_type = DATA_TYPE_EXT_1N_BYTE;
    bmcv_rect_t crop_rect;
    crop_rect.start_x = 0;
    crop_rect.start_y = 0;
    crop_rect.crop_w  = (int)src_width;
    crop_rect.crop_h  = (int)src_height;

    bm_image input_image;
    bm_image_create(pbmhandle, src_height, src_width, image_format, data_type, &input_image);
    // bm_image_alloc_contiguous_mem(1, &input_image);
    bm_image_alloc_dev_mem(input_image, USE_MEM_HEAP1);

    // image format convert
    bmcv_image_vpp_convert(pbmhandle, 1, src_image, &input_image, &crop_rect);

    bm_image affine_image;
    bm_image_create(pbmhandle, dst_height, dst_width, image_format, data_type, &affine_image);

    unsigned long long mem_addr;
    bm_device_mem_t input_addr[3];
    mem_addr      = bm_mem_get_device_addr(*dst_dev_mem);
    int size      = dst_width * dst_height;
    input_addr[0] = bm_mem_from_device(mem_addr, size);
    auto g_addr   = (unsigned long long)((uint8_t*)mem_addr + size);
    input_addr[1] = bm_mem_from_device(g_addr, size);
    auto r_addr   = (unsigned long long)((uint8_t*)g_addr + size);
    input_addr[2] = bm_mem_from_device(r_addr, size);
    ret           = bm_image_attach(affine_image, input_addr);
    if (ret != BM_SUCCESS) {
        return Status(COSMO_NN_ERR_SOPHON_ATTACH_FAILED, "bm_image attach failed.");
    }

    float* matrix_data = host_matrixs.data();
    bmcv_affine_image_matrix matrix_image[4];
    bmcv_affine_matrix matrix;
    std::memcpy(matrix.m, matrix_data, 6 * sizeof(float));
    matrix_image[0].matrix_num = 1;
    matrix_image[0].matrix     = &matrix;

    ret = bmcv_image_warp_affine(pbmhandle, 1, matrix_image, &input_image, &affine_image);
    if (ret != BM_SUCCESS) {
        return Status(COSMO_NN_ERR_SOPHON_INFER_ERR, "bm_image warp affine failed.");
    }

    // free
    ret = bm_image_detach(src_image);
    if (ret != BM_SUCCESS) {
        return Status(COSMO_NN_ERR_SOPHON_DETACH_FAILED, "bm_image detach failed.");
    }
    ret = bm_image_detach(affine_image);
    if (ret != BM_SUCCESS) {
        return Status(COSMO_NN_ERR_SOPHON_DETACH_FAILED, "bm_image detach failed.");
    }
#ifdef COSMO_NN_SOPHON_1684X
    ret = bm_image_destroy(input_image);
#else
    ret = bm_image_destroy(&input_image);
#endif
    if (ret != BM_SUCCESS) {
        return Status(COSMO_NN_ERR_SOPHON_BMIMAGE_DESTROY_FAILED, "bm_image destroy failed.");
    }
#ifdef COSMO_NN_SOPHON_1684X
    ret = bm_image_destroy(src_image);
#else
    ret = bm_image_destroy(&src_image);
#endif
    if (ret != BM_SUCCESS) {
        return Status(COSMO_NN_ERR_SOPHON_BMIMAGE_DESTROY_FAILED, "bm_image destroy failed.");
    }
#ifdef COSMO_NN_SOPHON_1684X
    ret = bm_image_destroy(affine_image);
#else
    ret = bm_image_destroy(&affine_image);
#endif
    if (ret != BM_SUCCESS) {
        return Status(COSMO_NN_ERR_SOPHON_BMIMAGE_DESTROY_FAILED, "bm_image destroy failed.");
    }

    return COSMO_NN_OK;
}

Status SophonAffineCropNode::Forward(std::vector<std::shared_ptr<Blob>>& bottom_blobs,
                                     std::vector<std::shared_ptr<Blob>>& top_blobs) {
    auto image_blob    = bottom_blobs.at(0);
    auto landmark_blob = bottom_blobs.at(1);

    auto top_blob = top_blobs.at(0);

    return COSMO_NN_OK;
}

Status SophonAffineCropNode::PrepareAffineCropMatrixs(std::shared_ptr<Blob> landmark_blob) {
    auto landmark_blob_desc   = landmark_blob->GetBlobDesc();
    auto landmark_blob_handle = landmark_blob->GetHandle();

    auto landmark_data_type = landmark_blob_desc.data_type;
    if (landmark_data_type != DataType::DATA_TYPE_FLOAT)
        return Status(COSMO_NN_ERR_INVALID_INPUT, "landmark data type must be float");

    auto landmark_dims = landmark_blob_desc.dims;
    if (landmark_dims.size() != 2 && landmark_dims.at(1) != landmark_data_len)
        return Status(COSMO_NN_ERR_INVALID_INPUT, "landmark dims is invalid");

    float* landmark_data = static_cast<float*>(landmark_blob_handle.base);

    const int landmark_count = landmark_blob_desc.dims.at(0);

    host_matrixs.clear();
    for (int i = 0; i < landmark_count; i++) {
        auto data = landmark_data + i * landmark_data_len;
        RETURN_ON_FAIL(CalculateMatrix(data, host_matrixs));
    }

    return COSMO_NN_OK;
}

Status SophonAffineCropNode::CalculateMatrix(float* landmark, std::vector<float>& matrix) {
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
        case 0:  // AVE_LE2LM_RE2RM
        {
            float deltaX1 = left_eye.x - left_mouth.x;
            float deltaY1 = left_eye.y - left_mouth.y;

            float deltaX2 = right_eye.x - right_mouth.x;
            float deltaY2 = right_eye.y - right_mouth.y;

            actual_len = std::sqrt(std::pow(deltaX1, 2.0) + std::pow(deltaY1, 2.0)) +
                         std::sqrt(std::pow(deltaX2, 2.0) + std::pow(deltaY2, 2.0));
            actual_len /= 2.f;
        } break;
        case 1:  // RECT_LE_RE_LM_RM
        {
            float left = std::min(std::min(std::min(left_eye.x, right_eye.x), left_mouth.x), right_mouth.x);

            float top = std::min(std::min(std::min(left_eye.y, right_eye.y), left_mouth.y), right_mouth.y);

            float right = std::max(std::max(std::max(left_eye.x, right_eye.x), left_mouth.x), right_mouth.x);

            float bottom = std::max(std::max(std::max(left_eye.y, right_eye.y), left_mouth.y), right_mouth.y);

            float w = right - left;
            float h = bottom - top;

            actual_len = std::sqrt((std::pow(w, 2.0) + std::pow(h, 2.0)) / 2);
        } break;
        case 2:  // LE2RE
        {
            float deltaX = left_eye.x - right_eye.x;
            float deltaY = left_eye.y - right_eye.y;

            actual_len = std::sqrt(std::pow(deltaX, 2.0) + std::pow(deltaY, 2.0));
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

}  // namespace cosmo::nn