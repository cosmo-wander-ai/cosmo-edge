#include "nn/device/sophon/sophon_normalize_node.h"

#include <algorithm>
#include <iostream>

#include "bmcv_api.h"
#include "bmcv_api_ext.h"
#include "bmlib_runtime.h"
#include "bmruntime_cpp.h"
#include "nn/device/sophon/sophon_node.h"
#include "nn/node/node_type_utils.h"
#include "nn/utils/dims_vector_utils.h"
#include "nn/utils/string_format.h"
namespace cosmo::nn {

SophonNormalizeNode::SophonNormalizeNode() : Node() {
    node_type     = NodeType::NODE_NORMALIZE;
    name          = NodeTypeUtils::NodeTypeToStr(NODE_NORMALIZE).append("_0");
    one_blob_only = true;
}

SophonNormalizeNode::~SophonNormalizeNode() {}

void SophonNormalizeNode::LoadParam(Op* op) {
    if (!op)
        return;

    auto norm = dynamic_cast<Normalize*>(op);
    is_bgr    = norm->is_bgr;
    scale     = norm->scale;
    mean      = norm->mean;
    std       = norm->std;

    if (std.empty()) {
        std.assign(mean.size(), scale);
    } else {
        std::for_each(std.begin(), std.end(), [](float& v) { v = 1.f / v; });
    }
}

bool SophonNormalizeNode::NeedBottomShapesInfered() {
    return true;
}

Status SophonNormalizeNode::InferTopShapesWithBottoms(std::vector<DimsVector> dims,
                                                      std::vector<DataType> types) {
    auto bottom_shape = dims.at(0);

    bottom_shape.at(1) = shared_resource->net_input_h;
    bottom_shape.at(2) = shared_resource->net_input_w;

    top_blob_shapes     = {DimsVectorUtils::NHWC2NCHW(bottom_shape)};
    top_blob_data_types = {DataType::DATA_TYPE_FLOAT};

    return COSMO_NN_OK;
}

size_t SophonNormalizeNode::GetBottomCount() {
    return 1;
}

size_t SophonNormalizeNode::GetTopCount() {
    return 1;
}

DeviceType SophonNormalizeNode::GetTopBlobDeviceType() {
    return DeviceType::DEVICE_SOPHON_TPU;
}

bool SophonNormalizeNode::NeedSwapRB(ImageFormat fmt) {
    if (fmt == ImageFormat::IMAGE_BGR || fmt == ImageFormat::IMAGE_BGRA)
        return !is_bgr;

    if (fmt == ImageFormat::IMAGE_RGB || fmt == ImageFormat::IMAGE_RGBA)
        return is_bgr;

    return false;
}

Status SophonNormalizeNode::Forward(std::vector<std::shared_ptr<Blob>>& bottom_blobs,
                                    std::vector<std::shared_ptr<Blob>>& top_blobs) {
    timer.Start();

    bm_status_t ret       = BM_SUCCESS;
    bm_handle_t pbmhandle = shared_resource->m_handle;
    if (nullptr == pbmhandle)
        return Status(COSMO_NN_ERR_SOPHON_HANDLE_FAILED, "bm_handle_t is null !");

    auto bottom_blob = bottom_blobs.at(0);
    auto top_blob    = top_blobs.at(0);
    RETURN_ON_FAIL(CheckNodeInputOutput(bottom_blob, top_blob, true));

    auto bottom_desc   = bottom_blob->GetBlobDesc();
    auto bottom_handle = bottom_blob->GetHandle();
    auto bottom_dim    = bottom_desc.dims;

    auto top_desc   = top_blob->GetBlobDesc();
    auto top_handle = top_blob->GetHandle();
    auto top_dim    = top_desc.dims;
    auto top_fmt    = top_desc.image_format;

    uint32_t src_width  = bottom_dim.at(2);
    uint32_t src_height = bottom_dim.at(1);
    uint32_t dst_width  = top_dim.at(3);
    uint32_t dst_height = top_dim.at(2);

    // check batch
    int batch = bottom_dim.at(0);
    if (batch > max_batch)
        return Status(COSMO_NN_ERR_INVALID_INPUT, "batch size too large");

    if (mean.empty() || mean.size() != 3)
        return Status(COSMO_NN_ERR_INVALID_INPUT, "mean size not match");

    ImageFormat bottom_fmt = bottom_desc.image_format;
    if (!ImageFormatIsBGR(bottom_fmt) && !ImageFormatIsRGB(bottom_fmt) && !ImageFormatIsGray(bottom_fmt))
        return Status(COSMO_NN_ERR_INVALID_INPUT, "image format not support");

    bool swap_rb = NeedSwapRB(bottom_fmt);

    // top blob set current shape
    SetCurrentBatch(top_blob, batch);

    bm_device_mem_t* src_dev_mem = reinterpret_cast<bm_device_mem_t*>(bottom_handle.base);
    bm_device_mem_t* dst_dev_mem = reinterpret_cast<bm_device_mem_t*>(top_handle.base);

    bmcv_convert_to_attr converto_attr;
    bm_model_input_scale = shared_resource->model_input_scale;  // from model for INT8 quantization
    // Keep Normalize semantics aligned with the naive backend: y = (x - mean) * scale.
    converto_attr.alpha_0 = bm_model_input_scale * std[0];
    converto_attr.beta_0  = -mean[0] * std[0];
    converto_attr.alpha_1 = bm_model_input_scale * std[1];
    converto_attr.beta_1  = -mean[1] * std[1];
    converto_attr.alpha_2 = bm_model_input_scale * std[2];
    converto_attr.beta_2  = -mean[2] * std[2];

    bm_image_format_ext src_image_format     = FORMAT_BGR_PLANAR;
    bm_image_format_ext convert_image_format = FORMAT_BGR_PLANAR;
    if (swap_rb) {
        src_image_format     = FORMAT_RGB_PLANAR;
        convert_image_format = FORMAT_RGB_PLANAR;
    }
    bm_image_data_format_ext src_data_format     = DATA_TYPE_EXT_1N_BYTE;
    bm_image_data_format_ext convert_data_format = DATA_TYPE_EXT_FLOAT32;

    bm_image src_image;
    ret = bm_image_create(pbmhandle, src_height, src_width, src_image_format, src_data_format, &src_image);
    if (ret != BM_SUCCESS) {
        return Status(COSMO_NN_ERR_SOPHON_BMIMAGE_CREAT_FAILED, "bm_image creat failed.");
    }
    unsigned long long mem_addr;
    bm_device_mem_t input_addr[3];
    mem_addr      = bm_mem_get_device_addr(*src_dev_mem);
    int size      = src_width * src_height;
    input_addr[0] = bm_mem_from_device(mem_addr, size);
    auto g_addr   = (unsigned long long)((uint8_t*)mem_addr + size);
    input_addr[1] = bm_mem_from_device(g_addr, size);
    auto r_addr   = (unsigned long long)((uint8_t*)g_addr + size);
    input_addr[2] = bm_mem_from_device(r_addr, size);
    ret           = bm_image_attach(src_image, input_addr);
    if (ret != BM_SUCCESS) {
        return Status(COSMO_NN_ERR_SOPHON_ATTACH_FAILED, "bm_image attach failed.");
    }

    bm_image convert_image;
    ret = bm_image_create(pbmhandle, dst_height, dst_width, convert_image_format, convert_data_format,
                          &convert_image);
    if (ret != BM_SUCCESS) {
        return Status(COSMO_NN_ERR_SOPHON_BMIMAGE_CREAT_FAILED, "bm_image creat failed.");
    }
    bm_device_mem_t output_addr[3];
    mem_addr = bm_mem_get_device_addr(*dst_dev_mem);
    size     = dst_width * dst_height;
    // Output is float: bm_mem_from_device 2nd param is byte size (dino_encode_node uses size*sizeof(float))
    size_t plane_bytes = static_cast<size_t>(size) * sizeof(float);
    output_addr[0]     = bm_mem_from_device(mem_addr, plane_bytes);
    g_addr             = (unsigned long long)((float*)mem_addr + size);
    output_addr[1]     = bm_mem_from_device(g_addr, plane_bytes);
    r_addr             = (unsigned long long)((float*)g_addr + size);
    output_addr[2]     = bm_mem_from_device(r_addr, plane_bytes);
    ret                = bm_image_attach(convert_image, output_addr);
    if (ret != BM_SUCCESS) {
        return Status(COSMO_NN_ERR_SOPHON_ATTACH_FAILED, "bm_image attach failed.");
    }

    ret = bmcv_image_convert_to(pbmhandle, 1, converto_attr, &src_image, &convert_image);
    if (ret != BM_SUCCESS) {
        return Status(COSMO_NN_ERR_SOPHON_ATTACH_FAILED, "bm_image convert to failed.");
    }

    // free
    ret = bm_image_detach(src_image);
    if (ret != BM_SUCCESS) {
        return Status(COSMO_NN_ERR_SOPHON_DETACH_FAILED, "bm_image detach failed.");
    }
    ret = bm_image_detach(convert_image);
    if (ret != BM_SUCCESS) {
        return Status(COSMO_NN_ERR_SOPHON_DETACH_FAILED, "bm_image detach failed.");
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
    ret = bm_image_destroy(convert_image);
#else
    ret = bm_image_destroy(&convert_image);
#endif
    if (ret != BM_SUCCESS) {
        return Status(COSMO_NN_ERR_SOPHON_BMIMAGE_DESTROY_FAILED, "bm_image destroy failed.");
    }

    timer.Stop();
    return COSMO_NN_OK;
}

}  // namespace cosmo::nn
