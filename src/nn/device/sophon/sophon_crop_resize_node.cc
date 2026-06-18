#include "nn/device/sophon/sophon_crop_resize_node.h"

#include <cstring>
#include <iostream>

#include "bmcv_api.h"
#include "bmcv_api_ext.h"
#include "bmlib_runtime.h"
#include "nn/device/sophon/sophon_node.h"
#include "nn/node/node_type_utils.h"
#include "nn/utils/dims_vector_utils.h"
#include "nn/utils/rect.h"
#include "nn/utils/string_format.h"

namespace cosmo::nn {

SophonCropResizeNode::SophonCropResizeNode() : Node() {
    node_type     = NodeType::NODE_CROP_RESIZE;
    name          = NodeTypeUtils::NodeTypeToStr(NODE_CROP_RESIZE).append("_0");
    one_blob_only = false;
}

SophonCropResizeNode::~SophonCropResizeNode() {}

void SophonCropResizeNode::LoadParam(Op* op) {
    if (!op)
        return;

    auto crop_resize = dynamic_cast<CropResize*>(op);

    type          = crop_resize->type;
    h_top_crop    = crop_resize->h_top_crop;
    h_bottom_crop = crop_resize->h_bottom_crop;
    w_left_crop   = crop_resize->w_left_crop;
    w_right_crop  = crop_resize->w_right_crop;
    square        = crop_resize->square;
    square_mode   = crop_resize->square_mode;
    dst_height    = crop_resize->dsize.at(0);
    dst_width     = crop_resize->dsize.at(1);
    gravity       = crop_resize->gravity;
    color         = crop_resize->color;
}

Status SophonCropResizeNode::InferTopShapes() {
    shared_resource->net_input_w = dst_width;
    shared_resource->net_input_h = dst_height;

    int aligned_w       = ALIGN(dst_width, 64);
    top_blob_shapes     = {{max_batch, dst_height, aligned_w, 3}};
    top_blob_data_types = {DataType::DATA_TYPE_UINT8};
    return COSMO_NN_OK;
}

DeviceType SophonCropResizeNode::GetTopBlobDeviceType() {
    return DeviceType::DEVICE_SOPHON_TPU;
}

size_t SophonCropResizeNode::GetTopCount() {
    return 1;
}

size_t SophonCropResizeNode::GetBottomCount() {
    return 2;
}

Status SophonCropResizeNode::PrepareRect(std::shared_ptr<Blob> host_rect, Size image_size) {
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

        if (square) {
            int base_len = 0;
            if (square_mode == 0) {
                base_len = std::max(w, h);
            } else if (square_mode == 1) {
                base_len = std::min(w, h);
            } else if (square_mode == 2) {
                base_len = (w + h) / 2;
            } else {
                return Status(COSMO_NN_ERR_PARAM, "Invalid square_mode");
            }

            int center_x = x1 + w / 2;
            int center_y = y1 + h / 2;

            x1 = center_x - base_len * (w_left_crop.at(0) + 0.5f);
            x2 = center_x + base_len * (w_right_crop.at(0) + 0.5f);
            y1 = center_y - base_len * (h_top_crop.at(0) + 0.5f);
            y2 = center_y + base_len * (h_bottom_crop.at(0) + 0.5f);

            x1 = std::max(0, x1);
            y1 = std::max(0, y1);
            x2 = std::min(x2, image_size.width - 1);
            y2 = std::min(y2, image_size.height - 1);

            calculated_rects.push_back(x1);
            calculated_rects.push_back(y1);
            calculated_rects.push_back(x2 - x1 + 1);
            calculated_rects.push_back(y2 - y1 + 1);
            continue;
        }

        // Unified signed ratio: negative or 0 means crop, positive means expand. Formula: x1 -= w*left_ratio
        // etc.
        float top_ratio    = h_top_crop.at(0);
        float bottom_ratio = h_bottom_crop.at(0);
        float left_ratio   = w_left_crop.at(0);
        float right_ratio  = w_right_crop.at(0);

        x1 -= w * left_ratio;
        y1 -= h * top_ratio;
        x2 += w * right_ratio;
        y2 += h * bottom_ratio;

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

Status SophonCropResizeNode::Forward(std::vector<std::shared_ptr<Blob>>& bottom_blob,
                                     std::vector<std::shared_ptr<Blob>>& params,
                                     std::vector<std::shared_ptr<Blob>>& top_blobs) {
    timer.Start();
    RETURN_ON_FAIL(CheckNodeInputOutput(bottom_blob, top_blobs, true));
    RETURN_ON_FAIL(CheckNodeInputOutput(params, top_blobs, false));

    auto top_blob = top_blobs.at(0);
    auto top_desc = top_blob->GetBlobDesc();

    ImageFormat bottom_fmt = bottom_blob.at(0)->GetBlobDesc().image_format;
    if (ImageFormatIsGray(bottom_fmt))
        top_desc.image_format = IMAGE_GRAY;
    else if (ImageFormatIsRGB(bottom_fmt))
        top_desc.image_format = IMAGE_RGB;
    else if (ImageFormatIsBGR(bottom_fmt))
        top_desc.image_format = IMAGE_BGR;
    else
        return Status(COSMO_NN_ERR_INVALID_INPUT, "image format not support");

    top_blob->SetBlobDesc(top_desc);

    RETURN_ON_FAIL(Forward(bottom_blob, params, top_blob));  // not contiguous memory

    timer.Stop();
    return COSMO_NN_OK;
}

Status SophonCropResizeNode::Forward(std::vector<std::shared_ptr<Blob>>& bottom_blobs,
                                     std::vector<std::shared_ptr<Blob>>& top_blobs) {
    return COSMO_NN_OK;
}

Status SophonCropResizeNode::Forward(std::vector<std::shared_ptr<Blob>>& image_blobs,
                                     std::vector<std::shared_ptr<Blob>>& rect_blobs,
                                     std::shared_ptr<Blob> top_blob) {
    bm_status_t ret       = BM_SUCCESS;
    bm_handle_t pbmhandle = shared_resource->m_handle;
    if (nullptr == pbmhandle)
        return Status(COSMO_NN_ERR_SOPHON_HANDLE_FAILED, "bm_handle_t is null !");

    auto image_count = image_blobs.size();
    auto rect_count  = rect_blobs.size();
    if (image_count != rect_count)
        return Status(COSMO_NN_ERR_INVALID_INPUT, "image num is not equal to rect num");

    int current_batch = 0;
    for (size_t i = 0; i < image_count; ++i) {
        auto rect_blob      = rect_blobs.at(i);
        auto rect_blob_desc = rect_blob->GetBlobDesc();
        auto rect_blob_dims = rect_blob_desc.dims;

        current_batch += rect_blob_dims.at(0);
    }

    if (current_batch > max_batch)
        return Status(COSMO_NN_ERR_INVALID_INPUT, "batch size is bigger than max_batch size");

    // set top shape
    auto top_blob_desc   = top_blob->GetBlobDesc();
    auto top_blob_handle = top_blob->GetHandle();
    auto top_blob_dims   = top_blob_desc.dims;
    top_blob_dims.at(0)  = current_batch;
    top_blob_desc.dims   = top_blob_dims;
    top_blob->SetBlobDesc(top_blob_desc);

    bm_device_mem_t* dst_dev_mem = reinterpret_cast<bm_device_mem_t*>(top_blob_handle.base);

    int processed_rect_num = 0;
    for (size_t i = 0; i < image_count; ++i) {
        auto image_blob        = image_blobs.at(i);
        auto image_blob_desc   = image_blob->GetBlobDesc();
        auto image_blob_handle = image_blob->GetHandle();
        auto image_blob_dims   = image_blob_desc.dims;

        auto rect_blob      = rect_blobs.at(i);
        auto rect_blob_desc = rect_blob->GetBlobDesc();
        auto rect_blob_dims = rect_blob_desc.dims;

        uint32_t src_width  = image_blob_dims.at(2);
        uint32_t src_height = image_blob_dims.at(1);
        uint32_t dst_width  = top_blob_dims.at(2);
        uint32_t dst_height = top_blob_dims.at(1);

        uint32_t src_size = src_width * src_height * 3;
        uint32_t dst_size = dst_width * dst_height * 3;

        bm_image src_image;
        ret = bm_image_create(pbmhandle, src_height, src_width, BM_IMAGE_FORMAT, DATA_TYPE_EXT_1N_BYTE,
                              &src_image);
        if (ret != BM_SUCCESS) {
            return Status(COSMO_NN_ERR_SOPHON_BMIMAGE_CREAT_FAILED, "bm_image creat failed.");
        }

        bm_device_mem_t* src_dev_mem = reinterpret_cast<bm_device_mem_t*>(image_blob_handle.base);
        ret                          = bm_image_attach(src_image, src_dev_mem);
        if (ret != BM_SUCCESS) {
            bm_image_destroy(src_image);
            return Status(COSMO_NN_ERR_SOPHON_ATTACH_FAILED, "bm_image attach failed.");
        }

        bm_image resize_image;
        ret = bm_image_create(pbmhandle, dst_height, dst_width, FORMAT_BGR_PLANAR, DATA_TYPE_EXT_1N_BYTE,
                              &resize_image);
        if (ret != BM_SUCCESS) {
            return Status(COSMO_NN_ERR_SOPHON_BMIMAGE_CREAT_FAILED, "bm_image creat failed.");
        }

        bm_device_mem_t* current_dev_mem = dst_dev_mem + dst_size * processed_rect_num;

        size_t rect_count = rect_blob_dims.at(0);
        for (size_t j = 0; j < rect_count; ++j) {
            RETURN_ON_FAIL(PrepareRect(rect_blob, Size(src_width, src_height)));
            bm_device_mem_t* current_ptr = current_dev_mem + j * dst_size;

            unsigned long long mem_addr;
            bm_device_mem_t input_addr[3];
            mem_addr      = bm_mem_get_device_addr(*current_ptr);
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

            bmcv_rect_t crop_rect;
            crop_rect.start_x = (int)calculated_rects.at(4 * j);
            crop_rect.start_y = (int)calculated_rects.at(4 * j + 1);
            crop_rect.crop_w  = (int)calculated_rects.at(4 * j + 2);
            crop_rect.crop_h  = (int)calculated_rects.at(4 * j + 3);

            double paddingVal = static_cast<double>(color.at(0));

            if (gravity == 0) {  // resize to fit dst size
                bmcv_image_vpp_convert(pbmhandle, 1, src_image, &resize_image, &crop_rect);
            } else if (gravity == 1) {  // resize to fit dst size and keep aspect ratio
                // scale ratio
                float scale_w       = float(dst_width) / float(src_width);
                float scale_h       = float(dst_height) / float(src_height);
                float scale         = std::min(scale_w, scale_h);
                int unPaddingWidth  = int(std::round(src_width * scale));
                int unPaddingHeight = int(std::round(src_height * scale));

                int align_unPaddingWidth  = unPaddingWidth;
                int align_unPaddingHeight = unPaddingHeight;
                int top                   = (dst_height - align_unPaddingHeight) / 2;  // even
                int bottom                = (dst_height - align_unPaddingHeight) / 2;  // even
                int left                  = (dst_width - align_unPaddingWidth) / 2;    // even
                int right                 = (dst_width - align_unPaddingWidth) / 2;    // even

                bmcv_padding_atrr_t padding_attr;
                memset(&padding_attr, 0, sizeof(padding_attr));
                padding_attr.dst_crop_sty = top;
                padding_attr.dst_crop_stx = left;
                padding_attr.dst_crop_h   = align_unPaddingHeight;
                padding_attr.dst_crop_w   = align_unPaddingWidth;
                padding_attr.padding_b    = color.at(0);
                padding_attr.padding_g    = color.at(1);
                padding_attr.padding_r    = color.at(2);
                padding_attr.if_memset    = 1;
                ret = bmcv_image_vpp_convert_padding(pbmhandle, 1, src_image, &resize_image, &padding_attr,
                                                     &crop_rect);

                assert(BM_SUCCESS == ret);
            } else if (gravity == 2) {  // resize to fit new size and align top-left corner, make sure no
                                        // empty space in vetival
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

                int align_width = resize_w;  // must even number
                int top         = 0;
                int bottom      = 0;
                int left        = 0;
                int right       = 0;

                if (align_width < model_input_w) {  // need padding
                    right = model_input_w - align_width;
                    bmcv_padding_atrr_t padding_attr;
                    memset(&padding_attr, 0, sizeof(padding_attr));
                    padding_attr.dst_crop_sty = top;
                    padding_attr.dst_crop_stx = left;
                    padding_attr.dst_crop_h   = model_input_h;
                    padding_attr.dst_crop_w   = align_width;
                    padding_attr.padding_b    = color.at(0);
                    padding_attr.padding_g    = color.at(1);
                    padding_attr.padding_r    = color.at(2);
                    padding_attr.if_memset    = 1;

                    ret = bmcv_image_vpp_convert_padding(pbmhandle, 1, src_image, &resize_image,
                                                         &padding_attr, &crop_rect);

                    assert(BM_SUCCESS == ret);
                } else {  // no need padding
                    bmcv_image_vpp_convert(pbmhandle, 1, src_image, &resize_image, &crop_rect);
                }
            } else {
                return Status(COSMO_NN_ERR_PARAM, "gravity not support yet");
            }

            ret = bm_image_detach(resize_image);
            if (ret != BM_SUCCESS) {
                return Status(COSMO_NN_ERR_SOPHON_DETACH_FAILED, "bm_image detach failed.");
            }
        }
        processed_rect_num += rect_count;

        // free
        ret = bm_image_detach(src_image);
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