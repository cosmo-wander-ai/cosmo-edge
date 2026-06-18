#include "nn/device/sophon/sophon_resize_node.h"

#include <cmath>
#include <cstring>
#include <iostream>

#include "bmcv_api.h"
#include "bmcv_api_ext.h"
#include "bmlib_runtime.h"
#include "bmruntime_cpp.h"
#include "nn/core/common.h"
#include "nn/device/sophon/sophon_node.h"
#include "nn/node/node_type_utils.h"
#include "nn/utils/dims_vector_utils.h"
#include "nn/utils/image_format_utils.h"
#include "nn/utils/string_format.h"

namespace cosmo::nn {

SophonResizeNode::SophonResizeNode() : Node() {
    node_type     = NodeType::NODE_RESIZE;
    name          = NodeTypeUtils::NodeTypeToStr(NODE_RESIZE).append("_0");
    one_blob_only = true;
}

SophonResizeNode::~SophonResizeNode() {}

void SophonResizeNode::LoadParam(Op* op) {
    if (!op)
        return;

    auto resize = dynamic_cast<Resize*>(op);
    out_h       = resize->dsize.at(0);
    out_w       = resize->dsize.at(1);
    gravity     = resize->gravity;
    color       = resize->color.at(0);
}

Status SophonResizeNode::InferTopShapes() {
    // set top shape
    shared_resource->net_input_w = out_w;
    shared_resource->net_input_h = out_h;

    int aligned_w       = ALIGN(out_w, 64);
    top_blob_shapes     = {{max_batch, out_h, aligned_w, 3}};
    top_blob_data_types = {DataType::DATA_TYPE_UINT8};
    return COSMO_NN_OK;
}

DeviceType SophonResizeNode::GetTopBlobDeviceType() {
    return DeviceType::DEVICE_SOPHON_TPU;
}

size_t SophonResizeNode::GetBottomCount() {
    return 1;
}

size_t SophonResizeNode::GetTopCount() {
    return 1;
}

Status SophonResizeNode::Forward(std::vector<std::shared_ptr<Blob>>& bottom_blobs,
                                 std::vector<std::shared_ptr<Blob>>& top_blobs) {
    timer.Start();
    RETURN_ON_FAIL(CheckNodeInputOutput(bottom_blobs, top_blobs, true));

    auto top_blob = top_blobs.at(0);
    auto top_desc = top_blob->GetBlobDesc();

    ImageFormat bottom_fmt = bottom_blobs.at(0)->GetBlobDesc().image_format;
    if (ImageFormatIsGray(bottom_fmt))
        top_desc.image_format = IMAGE_GRAY;
    else if (ImageFormatIsRGB(bottom_fmt))
        top_desc.image_format = IMAGE_RGB;
    else if (ImageFormatIsBGR(bottom_fmt))
        top_desc.image_format = IMAGE_BGR;
    else
        return Status(COSMO_NN_ERR_INVALID_INPUT, "image format not support");

    // set top blob image format
    top_blob->SetBlobDesc(top_desc);

    if (first_calculate_node) {
        RETURN_ON_FAIL(Forward(bottom_blobs, top_blob));
    } else {
        RETURN_ON_FAIL(Forward(bottom_blobs.at(0), top_blob));
    }

    timer.Stop();
    return COSMO_NN_OK;
}

Status SophonResizeNode::Forward(std::shared_ptr<Blob>& bottom_blob, std::shared_ptr<Blob>& top_blob) {
    bm_status_t ret       = BM_SUCCESS;
    bm_handle_t pbmhandle = shared_resource->m_handle;
    if (nullptr == pbmhandle)
        return Status(COSMO_NN_ERR_SOPHON_HANDLE_FAILED, "bm_handle_t is null !");

    auto bottom_desc   = bottom_blob->GetBlobDesc();
    auto bottom_handle = bottom_blob->GetHandle();
    auto bottom_dim    = bottom_desc.dims;
    auto bottom_fmt    = bottom_desc.image_format;

    int batch = bottom_dim.at(0);
    if (batch > max_batch)
        return Status(COSMO_NN_ERR_INVALID_INPUT, "batch size is bigger than max_batch size");

    // top blob set current shape
    auto top_desc   = top_blob->GetBlobDesc();
    auto top_handle = top_blob->GetHandle();
    auto top_fmt    = top_desc.image_format;
    auto top_dim    = top_desc.dims;
    top_dim.at(0)   = batch;
    top_desc.dims   = top_dim;
    top_blob->SetBlobDesc(top_desc);

    for (size_t i = 0; i < batch; ++i) {
        uint32_t src_width  = bottom_dim.at(2);
        uint32_t src_height = bottom_dim.at(1);
        uint32_t dst_width  = top_dim.at(2);
        uint32_t dst_height = top_dim.at(1);
        uint32_t src_size   = src_width * src_height * 3;
        uint32_t dst_size   = dst_width * dst_height * 3;

        bm_device_mem_t* src_dev_mem = reinterpret_cast<bm_device_mem_t*>(bottom_handle.base) + i * src_size;
        bm_device_mem_t* dst_dev_mem = reinterpret_cast<bm_device_mem_t*>(top_handle.base) + i * dst_size;

        // Input from LoadImage is NHWC (HWC / BGR_PACKED). Use PACKED so vpp can read it correctly.
        // Using PLANAR here when data is actually PACKED caused Python/C++ alignment issues (HWC vs CHW).
        bm_image_format_ext src_fmt =
            (bottom_desc.data_format == DATA_FORMAT_NHWC) ? FORMAT_BGR_PACKED : FORMAT_BGR_PLANAR;
        bm_image src_image;
        ret = bm_image_create(pbmhandle, src_height, src_width, src_fmt, DATA_TYPE_EXT_1N_BYTE, &src_image);
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

        bm_image resize_image;
        ret = bm_image_create(pbmhandle, dst_height, dst_width, FORMAT_BGR_PLANAR, DATA_TYPE_EXT_1N_BYTE,
                              &resize_image);
        if (ret != BM_SUCCESS) {
            return Status(COSMO_NN_ERR_SOPHON_BMIMAGE_CREAT_FAILED, "bm_image creat failed.");
        }
        unsigned long long mem_addr;
        bm_device_mem_t input_addr[3];
        mem_addr      = bm_mem_get_device_addr(*dst_dev_mem);
        int size      = dst_width * dst_height;
        input_addr[0] = bm_mem_from_device(mem_addr, size);
        auto g_addr   = (unsigned long long)((uint8_t*)mem_addr + size);
        input_addr[1] = bm_mem_from_device(g_addr, size);
        auto r_addr   = (unsigned long long)((uint8_t*)g_addr + size);
        input_addr[2] = bm_mem_from_device(r_addr, size);
        ret           = bm_image_attach(resize_image, input_addr);
        if (ret != BM_SUCCESS) {
            return Status(COSMO_NN_ERR_SOPHON_ATTACH_FAILED, "bm_image attach failed.");
        }

        if (gravity == 0) {  // resize to fit dst size
            bmcv_image_vpp_convert(pbmhandle, 1, src_image, &resize_image, NULL);

        } else if (gravity == 1) {  // resize to fit dst size and keep aspect ratio
            // scale ratio
            float scale_w       = float(dst_width) / float(src_width);
            float scale_h       = float(dst_height) / float(src_height);
            float scale         = std::min(scale_w, scale_h);
            int unPaddingWidth  = int(std::round(src_width * scale));
            int unPaddingHeight = int(std::round(src_height * scale));

            int align_unPaddingWidth  = unPaddingWidth;
            int align_unPaddingHeight = unPaddingHeight;
            int top                   = (dst_height - align_unPaddingHeight) / 2;
            int bottom                = (dst_height - align_unPaddingHeight) / 2;
            int left                  = (dst_width - align_unPaddingWidth) / 2;
            int right                 = (dst_width - align_unPaddingWidth) / 2;

            // resize
            bmcv_padding_atrr_t padding_attr;
            memset(&padding_attr, 0, sizeof(padding_attr));
            padding_attr.dst_crop_sty = top;
            padding_attr.dst_crop_stx = left;
            padding_attr.dst_crop_h   = align_unPaddingHeight;
            padding_attr.dst_crop_w   = align_unPaddingWidth;
            padding_attr.padding_b    = color;
            padding_attr.padding_g    = color;
            padding_attr.padding_r    = color;
            padding_attr.if_memset    = 1;

            ret = bmcv_image_vpp_convert_padding(pbmhandle, 1, src_image, &resize_image, &padding_attr, NULL,
                                                 BMCV_INTER_NEAREST);

            assert(BM_SUCCESS == ret);

        } else if (gravity == 2) {  // resize to fit new size and align top-left corner, make sure no empty
                                    // space in vetival
            int model_input_h = dst_height;
            int model_input_w = dst_width;
            int resize_w;
            float max_wh_ratio = model_input_w * 1.0f / model_input_h;
            float ori_wh_ratio = float(src_width) / float(src_height);
            max_wh_ratio       = std::max(max_wh_ratio, ori_wh_ratio);

            model_input_w = int(model_input_h * max_wh_ratio);
            if (ceilf(model_input_h * ori_wh_ratio) > model_input_w) {
                resize_w = model_input_w;
            } else {
                resize_w = int(ceilf(model_input_h * ori_wh_ratio));
            }

            int align_width = resize_w;
            int top         = 0;
            int left        = (model_input_w - align_width) / 2;  // center padding for letterbox
            int right       = model_input_w - align_width - left;

            if (align_width < model_input_w) {  // need padding
                bmcv_padding_atrr_t padding_attr;
                memset(&padding_attr, 0, sizeof(padding_attr));
                padding_attr.dst_crop_sty = top;
                padding_attr.dst_crop_stx = left;
                padding_attr.dst_crop_h   = model_input_h;
                padding_attr.dst_crop_w   = align_width;
                padding_attr.padding_b    = color;
                padding_attr.padding_g    = color;
                padding_attr.padding_r    = color;
                padding_attr.if_memset    = 1;

                bmcv_rect_t crop_rect = {0, 0, src_width, src_height};
                ret = bmcv_image_vpp_convert_padding(pbmhandle, 1, src_image, &resize_image, &padding_attr,
                                                     &crop_rect, BMCV_INTER_NEAREST);

                assert(BM_SUCCESS == ret);
            } else {  // no need padding
                ret = bmcv_image_vpp_convert(pbmhandle, 1, src_image, &resize_image, NULL);
            }
        } else {
            return Status(COSMO_NN_ERR_PARAM, "gravity not support yet");
        }

        // free
        ret = bm_image_detach(src_image);
        if (ret != BM_SUCCESS) {
            return Status(COSMO_NN_ERR_SOPHON_DETACH_FAILED, "bm_image detach failed.");
        }
        ret = bm_image_detach(resize_image);
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
        ret = bm_image_destroy(resize_image);
#else
        ret = bm_image_destroy(&resize_image);
#endif
        if (ret != BM_SUCCESS) {
            return Status(COSMO_NN_ERR_SOPHON_BMIMAGE_DESTROY_FAILED, "bm_image destroy failed.");
        }
    }

    return COSMO_NN_OK;
}

Status SophonResizeNode::Forward(std::vector<std::shared_ptr<Blob>>& bottom_blobs,
                                 std::shared_ptr<Blob>& top_blob) {
    bm_status_t ret       = BM_SUCCESS;
    bm_handle_t pbmhandle = shared_resource->m_handle;
    if (nullptr == pbmhandle)
        return Status(COSMO_NN_ERR_SOPHON_HANDLE_FAILED, "bm_handle_t is null !");

    size_t batch = bottom_blobs.size();
    if (batch > max_batch)
        return Status(COSMO_NN_ERR_INVALID_INPUT, "batch size is bigger than max_batch size");

    // top blob set current shape
    auto top_desc   = top_blob->GetBlobDesc();
    auto top_handle = top_blob->GetHandle();
    auto top_dim    = top_desc.dims;
    auto top_fmt    = top_desc.image_format;
    top_dim.at(0)   = batch;
    top_desc.dims   = top_dim;
    top_blob->SetBlobDesc(top_desc);

    bm_device_mem_t* dst_dev_mem = reinterpret_cast<bm_device_mem_t*>(top_handle.base);

    for (size_t i = 0; i < batch; ++i) {
        auto bottom_blob             = bottom_blobs.at(i);
        auto bottom_handle           = bottom_blob->GetHandle();
        auto bottom_desc             = bottom_blob->GetBlobDesc();
        auto bottom_dim              = bottom_desc.dims;
        bm_device_mem_t* src_dev_mem = reinterpret_cast<bm_device_mem_t*>(bottom_handle.base);

        uint32_t src_width  = bottom_dim.at(2);
        uint32_t src_height = bottom_dim.at(1);
        uint32_t dst_width  = top_dim.at(2);
        uint32_t dst_height = top_dim.at(1);
        uint32_t src_size   = src_width * src_height * 3;
        uint32_t dst_size   = dst_width * dst_height * 3;

        // src
        bm_image src_image;
        ret = bm_image_create(pbmhandle, src_height, src_width, BM_IMAGE_FORMAT, DATA_TYPE_EXT_1N_BYTE,
                              &src_image);
        if (ret != BM_SUCCESS) {
            return Status(COSMO_NN_ERR_SOPHON_BMIMAGE_CREAT_FAILED, "bm_image creat failed.");
        }
        ret = bm_image_attach(src_image, src_dev_mem);
        if (ret != BM_SUCCESS) {
            return Status(COSMO_NN_ERR_SOPHON_ATTACH_FAILED, "bm_image attach failed.");
        }

        // dst
        bm_image resize_image;
        ret = bm_image_create(pbmhandle, dst_height, dst_width, FORMAT_BGR_PLANAR, DATA_TYPE_EXT_1N_BYTE,
                              &resize_image);
        if (ret != BM_SUCCESS) {
            return Status(COSMO_NN_ERR_SOPHON_BMIMAGE_CREAT_FAILED, "bm_image creat failed.");
        }

        unsigned long long mem_addr;
        bm_device_mem_t input_addr[3];
        mem_addr      = bm_mem_get_device_addr(*dst_dev_mem);
        int size      = dst_width * dst_height;
        input_addr[0] = bm_mem_from_device(mem_addr, size);
        auto g_addr   = (unsigned long long)((uint8_t*)mem_addr + size);
        input_addr[1] = bm_mem_from_device(g_addr, size);
        auto r_addr   = (unsigned long long)((uint8_t*)g_addr + size);
        input_addr[2] = bm_mem_from_device(r_addr, size);
        ret           = bm_image_attach(resize_image, input_addr);
        if (ret != BM_SUCCESS) {
            return Status(COSMO_NN_ERR_SOPHON_ATTACH_FAILED, "bm_image attach failed.");
        }

        if (gravity == 0) {  // resize to fit dst size
            bmcv_image_vpp_convert(pbmhandle, 1, src_image, &resize_image, NULL);

        } else if (gravity == 1) {  // resize to fit dst size and keep aspect ratio
            // scale ratio
            float scale_w       = float(dst_width) / float(src_width);
            float scale_h       = float(dst_height) / float(src_height);
            float scale         = std::min(scale_w, scale_h);
            int unPaddingWidth  = int(std::round(src_width * scale));
            int unPaddingHeight = int(std::round(src_height * scale));

            int align_unPaddingWidth  = unPaddingWidth;
            int align_unPaddingHeight = unPaddingHeight;
            int top                   = (dst_height - align_unPaddingHeight) / 2;
            int bottom                = (dst_height - align_unPaddingHeight) / 2;
            int left                  = (dst_width - align_unPaddingWidth) / 2;
            int right                 = (dst_width - align_unPaddingWidth) / 2;
            // resize
            bmcv_padding_atrr_t padding_attr;
            memset(&padding_attr, 0, sizeof(padding_attr));
            padding_attr.dst_crop_sty = top;
            padding_attr.dst_crop_stx = left;
            padding_attr.dst_crop_h   = align_unPaddingHeight;
            padding_attr.dst_crop_w   = align_unPaddingWidth;
            padding_attr.padding_b    = color;
            padding_attr.padding_g    = color;
            padding_attr.padding_r    = color;
            padding_attr.if_memset    = 1;

            ret = bmcv_image_vpp_convert_padding(pbmhandle, 1, src_image, &resize_image, &padding_attr, NULL,
                                                 BMCV_INTER_NEAREST);

            assert(BM_SUCCESS == ret);

        } else if (gravity == 2) {  // resize to fit new size and align top-left corner, make sure no empty
                                    // space in vetival
            int model_input_h = dst_height;
            int model_input_w = dst_width;
            int resize_w;
            float max_wh_ratio = model_input_w * 1.0f / model_input_h;
            float ori_wh_ratio = float(src_width) / float(src_height);
            max_wh_ratio       = std::max(max_wh_ratio, ori_wh_ratio);

            model_input_w = int(model_input_h * max_wh_ratio);
            if (ceilf(model_input_h * ori_wh_ratio) > model_input_w) {
                resize_w = model_input_w;
            } else {
                resize_w = int(ceilf(model_input_h * ori_wh_ratio));
            }

            int align_width = resize_w;
            int top         = 0;
            int left        = (model_input_w - align_width) / 2;  // center padding for letterbox
            int right       = model_input_w - align_width - left;

            if (align_width < model_input_w) {  // need padding
                bmcv_padding_atrr_t padding_attr;
                memset(&padding_attr, 0, sizeof(padding_attr));
                padding_attr.dst_crop_sty = top;
                padding_attr.dst_crop_stx = left;
                padding_attr.dst_crop_h   = model_input_h;
                padding_attr.dst_crop_w   = align_width;
                padding_attr.padding_b    = color;
                padding_attr.padding_g    = color;
                padding_attr.padding_r    = color;
                padding_attr.if_memset    = 1;

                bmcv_rect_t crop_rect = {0, 0, src_width, src_height};
                ret = bmcv_image_vpp_convert_padding(pbmhandle, 1, src_image, &resize_image, &padding_attr,
                                                     &crop_rect, BMCV_INTER_NEAREST);

                assert(BM_SUCCESS == ret);
            } else {  // no need padding
                ret = bmcv_image_vpp_convert(pbmhandle, 1, src_image, &resize_image, NULL);
            }
        } else {
            return Status(COSMO_NN_ERR_PARAM, "gravity not support yet");
        }
        // free
        ret = bm_image_detach(src_image);
        if (ret != BM_SUCCESS) {
            return Status(COSMO_NN_ERR_SOPHON_DETACH_FAILED, "bm_image detach failed.");
        }
        ret = bm_image_detach(resize_image);
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
        ret = bm_image_destroy(resize_image);
#else
        ret = bm_image_destroy(&resize_image);
#endif
        if (ret != BM_SUCCESS) {
            return Status(COSMO_NN_ERR_SOPHON_BMIMAGE_DESTROY_FAILED, "bm_image destroy failed.");
        }
    }

    return COSMO_NN_OK;
}

}  // namespace cosmo::nn