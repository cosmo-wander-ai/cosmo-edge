#include "nn/device/sophon/sophon_sequence_node.h"

#include <algorithm>
#include <cmath>
#include <iostream>

#include "bmcv_api.h"
#include "bmcv_api_ext.h"
#include "bmlib_runtime.h"
#include "nn/node/node_type_utils.h"
#include "nn/utils/dims_vector_utils.h"
#include "nn/utils/rect.h"

namespace cosmo::nn {

SophonSequenceNode::SophonSequenceNode() : Node() {
    node_type     = NodeType::NODE_SEQUENCE;
    name          = NodeTypeUtils::NodeTypeToStr(NODE_SEQUENCE).append("_0");
    one_blob_only = true;
}

SophonSequenceNode::~SophonSequenceNode() {}

void SophonSequenceNode::LoadParam(Op* op) {
    if (!op)
        return;

    auto sequence_op = dynamic_cast<Sequence*>(op);

    dst_batch  = max_batch;
    dst_depth  = sequence_op->size;
    dst_height = sequence_op->dsize.at(0);
    dst_width  = sequence_op->dsize.at(1);

    scale  = sequence_op->scale;
    is_bgr = sequence_op->is_bgr;
}

size_t SophonSequenceNode::GetTopCount() {
    return 1;
}

size_t SophonSequenceNode::GetBottomCount() {
    return 2;
}

Status SophonSequenceNode::InferTopShapes() {
    top_blob_shapes     = {{max_batch, 3, dst_height, dst_width}};
    top_blob_data_types = {DataType::DATA_TYPE_FLOAT};
    return COSMO_NN_OK;
}

DeviceType SophonSequenceNode::GetTopBlobDeviceType() {
    return DEVICE_SOPHON_TPU;
}

Status SophonSequenceNode::PrepareRect(std::shared_ptr<Blob> host_rect, Size image_size) {
    calculated_rects.clear();

    auto desc   = host_rect->GetBlobDesc();
    auto handle = host_rect->GetHandle();

    auto rect_data_type = desc.data_type;
    if (rect_data_type != DataType::DATA_TYPE_INT32)
        return Status(COSMO_NN_ERR_INVALID_INPUT, "rect data type must be int32.");

    auto dims = desc.dims;
    size_t n  = dims.at(0);

    if (dims.at(1) != 4)
        return Status(COSMO_NN_ERR_INVALID_INPUT, "rect shape must be [n, 4].");

    int32_t* host_rect_data = reinterpret_cast<int32_t*>(handle.base);

    for (size_t i = 0; i < n; ++i) {
        int x1 = host_rect_data[0];
        int y1 = host_rect_data[1];
        int w  = host_rect_data[2];
        int h  = host_rect_data[3];

        host_rect_data += 4;

        int x2 = x1 + w - 1;
        int y2 = y1 + h - 1;

        x1 = std::max(0, x1);
        y1 = std::max(0, y1);
        x2 = std::min(x2, image_size.width - 1);
        y2 = std::min(y2, image_size.height - 1);

        calculated_rects.push_back(x1);
        calculated_rects.push_back(y1);
        calculated_rects.push_back(x2 - x1 + 1);
        calculated_rects.push_back(y2 - y1 + 1);
    }
    return COSMO_NN_OK;
}

Status SophonSequenceNode::Forward(std::vector<std::shared_ptr<Blob>>& bottom_blob,
                                   std::vector<std::shared_ptr<Blob>>& params,
                                   std::vector<std::shared_ptr<Blob>>& top_blobs) {
    timer.Start();
    bm_status_t ret       = BM_SUCCESS;
    bm_handle_t pbmhandle = shared_resource->m_handle;
    if (nullptr == pbmhandle)
        return Status(COSMO_NN_ERR_SOPHON_HANDLE_FAILED, "bm_handle_t is null !");

    RETURN_ON_FAIL(CheckNodeInputOutput(bottom_blob, top_blobs, true));
    RETURN_ON_FAIL(CheckNodeInputOutput(params, top_blobs, false));

    const int image_count = bottom_blob.size();
    const int rect_count  = params.size();
    if (image_count != dst_depth || rect_count != dst_depth)
        return Status(COSMO_NN_ERR_PARAM, "Input size not match with dst_depth");

    auto top_blob            = top_blobs.at(0);
    auto top_handle          = top_blob->GetHandle();
    bm_device_mem_t* top_ptr = reinterpret_cast<bm_device_mem_t*>(top_handle.base);

    int stitch_w = dst_width;
    int stitch_h = dst_height;
    std::vector<bm_image> src_images;
    std::vector<bmcv_rect_t> src_crop_rects;
    std::vector<bmcv_rect_t> dst_crop_rects;
    int start_index_x = 0;
    int start_index_y = 0;
    for (int i = 0; i < image_count; i++) {
        auto bottom        = bottom_blob.at(i);
        auto bottom_handle = bottom->GetHandle();
        auto bottom_dims   = bottom->GetBlobDesc().dims;
        auto image_fmt     = bottom->GetBlobDesc().image_format;
        if (!ImageFormatIsBGR(image_fmt) && !ImageFormatIsRGB(image_fmt))
            return Status(COSMO_NN_ERR_PARAM, "Input image format not support");

        bm_device_mem_t* bottom_ptr = reinterpret_cast<bm_device_mem_t*>(bottom_handle.base);

        const int32_t image_height  = bottom_dims.at(1);
        const int32_t image_width   = bottom_dims.at(2);
        const int32_t image_channel = bottom_dims.at(3);

        auto rect_blob = params.at(i);
        RETURN_ON_FAIL(PrepareRect(rect_blob, Size(image_width, image_height)));
        if (calculated_rects.at(0) + calculated_rects.at(2) > image_width ||
            calculated_rects.at(1) + calculated_rects.at(3) > image_height)
            return Status(COSMO_NN_ERR_PARAM, "Rect value out of image range");

        int hw               = std::sqrt(image_count);
        int single_resize_hw = dst_height / hw;

        bm_image src_image;
        ret = bm_image_create(pbmhandle, image_height, image_width, BM_IMAGE_FORMAT, DATA_TYPE_EXT_1N_BYTE,
                              &src_image);
        if (ret != BM_SUCCESS) {
            return Status(COSMO_NN_ERR_SOPHON_BMIMAGE_CREAT_FAILED, "bm_image creat failed.");
        }
        ret = bm_image_attach(src_image, bottom_ptr);
        if (ret != BM_SUCCESS) {
#ifdef COSMO_NN_SOPHON_1684X
            bm_image_destroy(src_image);
#else
            bm_image_destroy(&src_image);
#endif
            return Status(COSMO_NN_ERR_SOPHON_ATTACH_FAILED, "bm_image attach failed.");
        }
        src_images.push_back(src_image);

        bmcv_rect_t src_crop_rect;
        src_crop_rect.start_x = calculated_rects.at(0);
        src_crop_rect.start_y = calculated_rects.at(1);
        src_crop_rect.crop_w  = calculated_rects.at(2);
        src_crop_rect.crop_h  = calculated_rects.at(3);
        src_crop_rects.push_back(src_crop_rect);

        start_index_x = i % hw;
        start_index_y = i / hw;
        bmcv_rect_t dst_crop_rect;
        dst_crop_rect.start_x = start_index_x * single_resize_hw + 0;
        dst_crop_rect.start_y = start_index_y * single_resize_hw + 0;
        dst_crop_rect.crop_w  = single_resize_hw;
        dst_crop_rect.crop_h  = single_resize_hw;
        dst_crop_rects.push_back(dst_crop_rect);
    }
    bm_image stitch_image;
    ret = bm_image_create(pbmhandle, stitch_h, stitch_w, FORMAT_BGR_PLANAR, DATA_TYPE_EXT_1N_BYTE,
                          &stitch_image, NULL);
    if (ret != BM_SUCCESS) {
        return Status(COSMO_NN_ERR_SOPHON_BMIMAGE_DESTROY_FAILED, "bm_image destroy failed.");
    }
    ret = bm_image_alloc_dev_mem(stitch_image, USE_MEM_HEAP1);
    if (ret != BM_SUCCESS) {
        return Status(COSMO_NN_ERR_SOPHON_ALLOC_MEM_FAILED, "bm_image alloc mem failed.");
    }

    bm_image* src_image_data        = src_images.data();
    bmcv_rect_t* src_crop_rect_data = src_crop_rects.data();
    bmcv_rect_t* dst_crop_rect_data = dst_crop_rects.data();
    ret = bmcv_image_vpp_stitch(pbmhandle, image_count, src_image_data, stitch_image, dst_crop_rect_data,
                                src_crop_rect_data);
    if (ret != BM_SUCCESS) {
        return Status(COSMO_NN_ERR_SOPHON_INFER_ERR, "bm_image vpp stitch failed.");
    }

    bmcv_convert_to_attr converto_attr;
    converto_attr.alpha_0 = 1 / 255.f;
    converto_attr.beta_0  = 0;
    converto_attr.alpha_1 = 1 / 255.f;
    converto_attr.beta_1  = 0;
    converto_attr.alpha_2 = 1 / 255.f;
    converto_attr.beta_2  = 0;

    bm_image convert_image;
    bm_image_create(pbmhandle, dst_height, dst_width, FORMAT_BGR_PLANAR, DATA_TYPE_EXT_FLOAT32,
                    &convert_image, NULL);

    unsigned long long mem_addr;
    bm_device_mem_t input_addr[3];
    mem_addr      = bm_mem_get_device_addr(*top_ptr);
    int size      = dst_width * dst_height;
    input_addr[0] = bm_mem_from_device(mem_addr, size);
    auto g_addr   = (unsigned long long)((float*)mem_addr + size);
    input_addr[1] = bm_mem_from_device(g_addr, size);
    auto r_addr   = (unsigned long long)((float*)g_addr + size);
    input_addr[2] = bm_mem_from_device(r_addr, size);
    ret           = bm_image_attach(convert_image, input_addr);
    if (ret != BM_SUCCESS) {
        return Status(COSMO_NN_ERR_SOPHON_ATTACH_FAILED, "bm_image attach failed.");
    }

    ret = bmcv_image_convert_to(pbmhandle, 1, converto_attr, &stitch_image, &convert_image);
    if (ret != BM_SUCCESS) {
        return Status(COSMO_NN_ERR_SOPHON_ATTACH_FAILED, "bm_image convert to failed.");
    }

    // free
    ret = bm_image_detach(convert_image);
    if (ret != BM_SUCCESS) {
        return Status(COSMO_NN_ERR_SOPHON_DETACH_FAILED, "bm_image detach failed.");
    }
#ifdef COSMO_NN_SOPHON_1684X
    ret = bm_image_destroy(convert_image);
#else
    ret = bm_image_destroy(&convert_image);
#endif
    if (ret != BM_SUCCESS) {
        return Status(COSMO_NN_ERR_SOPHON_BMIMAGE_DESTROY_FAILED, "bm_image destroy failed.");
    }
#ifdef COSMO_NN_SOPHON_1684X
    ret = bm_image_destroy(stitch_image);
#else
    ret = bm_image_destroy(&stitch_image);
#endif
    if (ret != BM_SUCCESS) {
        return Status(COSMO_NN_ERR_SOPHON_BMIMAGE_DESTROY_FAILED, "bm_image destroy failed.");
    }
    for (int i = 0; i < src_images.size(); i++) {
#ifdef COSMO_NN_SOPHON_1684X
        bm_image_destroy(src_images.at(i));
#else
        bm_image_destroy(&src_images.at(i));
#endif
    }
    timer.Stop();
    return COSMO_NN_OK;
}

}  // namespace cosmo::nn