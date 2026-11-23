#include "nn/device/sophon/sophon_resize_node.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>

#include "bmcv_api.h"
#include "bmcv_api_ext.h"
#include "bmlib_runtime.h"
#include "nn/core/common.h"
#include "nn/device/sophon/sophon_image_guard.h"
#include "nn/device/sophon/sophon_memory_utils.h"
#include "nn/node/node_type_utils.h"
#include "nn/utils/image_format_utils.h"

namespace cosmo::nn {

namespace {

    constexpr uint32_t kSophonVppMaxDimension = 8192;
    constexpr int kSophonVppWidthAlignment    = 64;

    bool AlignUpPositiveInt(int value, int alignment, int* aligned_value) {
        if (value <= 0 || alignment <= 0 || aligned_value == nullptr) {
            return false;
        }
        const int remainder = value % alignment;
        const int padding   = remainder == 0 ? 0 : alignment - remainder;
        if (value > std::numeric_limits<int>::max() - padding) {
            return false;
        }
        *aligned_value = value + padding;
        return true;
    }

    bool GetPackedFrameSize(uint32_t width, uint32_t height, size_t* frame_size) {
        if (frame_size == nullptr || width == 0 || height == 0 || width > kSophonVppMaxDimension ||
            height > kSophonVppMaxDimension) {
            return false;
        }
        const size_t pixel_count = static_cast<size_t>(width) * static_cast<size_t>(height);
        if (pixel_count > std::numeric_limits<size_t>::max() / 3) {
            return false;
        }
        *frame_size = pixel_count * 3;
        return true;
    }

    Status AttachSourceImage(bm_handle_t handle, const bm_device_mem_t& source, const BlobDesc& source_desc,
                             uint32_t width, uint32_t height, SophonImageGuard* image,
                             std::array<bm_device_mem_t, 3>* planar_views) {
        const bool packed = source_desc.data_format == DATA_FORMAT_NHWC;
        const auto format = packed ? FORMAT_BGR_PACKED : FORMAT_BGR_PLANAR;
        auto ret          = bm_image_create(handle, static_cast<int>(height), static_cast<int>(width), format,
                                            DATA_TYPE_EXT_1N_BYTE, image->Out());
        if (ret != BM_SUCCESS) {
            return Status(COSMO_NN_ERR_SOPHON_BMIMAGE_CREAT_FAILED, "bm_image create failed");
        }
        image->MarkCreated();

        if (packed) {
            auto source_view = source;
            ret              = bm_image_attach(image->Get(), &source_view);
        } else {
            const size_t plane_size = static_cast<size_t>(width) * static_cast<size_t>(height);
            if (!MakeThreePlaneDeviceMemoryView(source, 0, plane_size, planar_views)) {
                return Status(COSMO_NN_ERR_INVALID_INPUT, "source image buffer is too small");
            }
            ret = bm_image_attach(image->Get(), planar_views->data());
        }
        if (ret != BM_SUCCESS) {
            return Status(COSMO_NN_ERR_SOPHON_ATTACH_FAILED, "bm_image source attach failed");
        }
        return Status{COSMO_NN_OK};
    }

    Status ResizeFrame(bm_handle_t handle, const bm_device_mem_t& source, const BlobDesc& source_desc,
                       uint32_t src_width, uint32_t src_height,
                       const std::array<bm_device_mem_t, 3>& destination, uint32_t dst_width,
                       uint32_t dst_height, int gravity, uint8_t color) {
        SophonImageGuard source_image;
        std::array<bm_device_mem_t, 3> source_planes{};
        RETURN_ON_FAIL(AttachSourceImage(handle, source, source_desc, src_width, src_height, &source_image,
                                         &source_planes));

        SophonImageGuard destination_image;
        auto ret = bm_image_create(handle, static_cast<int>(dst_height), static_cast<int>(dst_width),
                                   FORMAT_BGR_PLANAR, DATA_TYPE_EXT_1N_BYTE, destination_image.Out());
        if (ret != BM_SUCCESS) {
            return Status(COSMO_NN_ERR_SOPHON_BMIMAGE_CREAT_FAILED, "bm_image create failed");
        }
        destination_image.MarkCreated();
        auto destination_views = destination;
        ret                    = bm_image_attach(destination_image.Get(), destination_views.data());
        if (ret != BM_SUCCESS) {
            return Status(COSMO_NN_ERR_SOPHON_ATTACH_FAILED, "bm_image destination attach failed");
        }

        if (gravity == 0) {
            ret = bmcv_image_vpp_convert(handle, 1, source_image.Get(), destination_image.Out(), nullptr,
                                         BMCV_INTER_LINEAR);
        } else if (gravity == 1) {
            const float scale = std::min(static_cast<float>(dst_width) / static_cast<float>(src_width),
                                         static_cast<float>(dst_height) / static_cast<float>(src_height));
            const int resized_width =
                std::clamp(static_cast<int>(std::round(src_width * scale)), 1, static_cast<int>(dst_width));
            const int resized_height =
                std::clamp(static_cast<int>(std::round(src_height * scale)), 1, static_cast<int>(dst_height));

            bmcv_padding_atrr_t padding{};
            padding.dst_crop_stx = (static_cast<int>(dst_width) - resized_width) / 2;
            padding.dst_crop_sty = (static_cast<int>(dst_height) - resized_height) / 2;
            padding.dst_crop_w   = resized_width;
            padding.dst_crop_h   = resized_height;
            padding.padding_b    = color;
            padding.padding_g    = color;
            padding.padding_r    = color;
            padding.if_memset    = 1;
            ret = bmcv_image_vpp_convert_padding(handle, 1, source_image.Get(), destination_image.Out(),
                                                 &padding, nullptr, BMCV_INTER_LINEAR);
        } else if (gravity == 2) {
            const double source_ratio = static_cast<double>(src_width) / static_cast<double>(src_height);
            const double target_ratio = static_cast<double>(dst_width) / static_cast<double>(dst_height);
            bmcv_rect_t crop{};
            if (source_ratio > target_ratio) {
                crop.crop_h = src_height;
                crop.crop_w = static_cast<unsigned int>(std::max(1.0, std::floor(src_height * target_ratio)));
                crop.start_x = (src_width - crop.crop_w) / 2;
                crop.start_y = 0;
            } else {
                crop.crop_w  = src_width;
                crop.crop_h  = static_cast<unsigned int>(std::max(1.0, std::floor(src_width / target_ratio)));
                crop.start_x = 0;
                crop.start_y = (src_height - crop.crop_h) / 2;
            }
            ret = bmcv_image_vpp_convert(handle, 1, source_image.Get(), destination_image.Out(), &crop,
                                         BMCV_INTER_LINEAR);
        } else {
            return Status(COSMO_NN_ERR_PARAM, "gravity not supported");
        }

        if (ret != BM_SUCCESS) {
            return Status(COSMO_NN_ERR_SOPHON_INFER_ERR, "bmcv resize failed");
        }
        return Status{COSMO_NN_OK};
    }

    bool ValidateImageDims(const DimsVector& dims, uint32_t* width, uint32_t* height) {
        if (dims.size() != 4 || dims[1] <= 0 || dims[2] <= 0 ||
            dims[1] > static_cast<int>(kSophonVppMaxDimension) ||
            dims[2] > static_cast<int>(kSophonVppMaxDimension)) {
            return false;
        }
        *height = static_cast<uint32_t>(dims[1]);
        *width  = static_cast<uint32_t>(dims[2]);
        return true;
    }

}  // namespace

SophonResizeNode::SophonResizeNode() : Node() {
    node_type     = NodeType::NODE_RESIZE;
    name          = NodeTypeUtils::NodeTypeToStr(NODE_RESIZE).append("_0");
    one_blob_only = true;
}

SophonResizeNode::~SophonResizeNode() = default;

void SophonResizeNode::LoadParam(Op* op) {
    if (!op) {
        return;
    }
    auto* resize = dynamic_cast<Resize*>(op);
    if (resize == nullptr || resize->dsize.size() < 2 || resize->color.empty()) {
        return;
    }
    out_h   = resize->dsize[0];
    out_w   = resize->dsize[1];
    gravity = resize->gravity;
    color   = resize->color[0];
}

Status SophonResizeNode::InferTopShapes() {
    if (out_h <= 0 || out_w <= 0 || out_h > static_cast<int>(kSophonVppMaxDimension) ||
        out_w > static_cast<int>(kSophonVppMaxDimension)) {
        return Status(COSMO_NN_ERR_PARAM, "resize output dimensions are invalid");
    }
    shared_resource->net_input_w = out_w;
    shared_resource->net_input_h = out_h;

    int aligned_w = 0;
    if (!AlignUpPositiveInt(out_w, kSophonVppWidthAlignment, &aligned_w)) {
        return Status(COSMO_NN_ERR_PARAM, "resize output width alignment overflow");
    }
    top_blob_shapes     = {{max_batch, out_h, aligned_w, 3}};
    top_blob_data_types = {DataType::DATA_TYPE_UINT8};
    return Status{COSMO_NN_OK};
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

    auto top_blob                = top_blobs.at(0);
    auto top_desc                = top_blob->GetBlobDesc();
    const ImageFormat bottom_fmt = bottom_blobs.at(0)->GetBlobDesc().image_format;
    if (ImageFormatIsGray(bottom_fmt)) {
        top_desc.image_format = IMAGE_GRAY;
    } else if (ImageFormatIsRGB(bottom_fmt)) {
        top_desc.image_format = IMAGE_RGB;
    } else if (ImageFormatIsBGR(bottom_fmt)) {
        top_desc.image_format = IMAGE_BGR;
    } else {
        return Status(COSMO_NN_ERR_INVALID_INPUT, "image format not supported");
    }
    top_blob->SetBlobDesc(top_desc);

    Status status =
        first_calculate_node ? Forward(bottom_blobs, top_blob) : Forward(bottom_blobs.at(0), top_blob);
    if (!bool(status)) {
        return status;
    }
    timer.Stop();
    return Status{COSMO_NN_OK};
}

Status SophonResizeNode::Forward(std::shared_ptr<Blob>& bottom_blob, std::shared_ptr<Blob>& top_blob) {
    bm_handle_t handle = shared_resource->m_handle;
    if (handle == nullptr) {
        return Status(COSMO_NN_ERR_SOPHON_HANDLE_FAILED, "bm_handle_t is null");
    }

    auto bottom_desc = bottom_blob->GetBlobDesc();
    auto top_desc    = top_blob->GetBlobDesc();
    if (bottom_desc.dims.size() != 4 || top_desc.dims.size() != 4 || bottom_desc.dims[0] <= 0 ||
        bottom_desc.dims[0] > max_batch) {
        return Status(COSMO_NN_ERR_INVALID_INPUT, "resize tensor shape is invalid");
    }

    uint32_t src_width  = 0;
    uint32_t src_height = 0;
    uint32_t dst_width  = 0;
    uint32_t dst_height = 0;
    if (!ValidateImageDims(bottom_desc.dims, &src_width, &src_height) ||
        !ValidateImageDims(top_desc.dims, &dst_width, &dst_height)) {
        return Status(COSMO_NN_ERR_INVALID_INPUT, "resize dimensions are invalid");
    }

    size_t src_frame_size = 0;
    size_t dst_frame_size = 0;
    if (!GetPackedFrameSize(src_width, src_height, &src_frame_size) ||
        !GetPackedFrameSize(dst_width, dst_height, &dst_frame_size)) {
        return Status(COSMO_NN_ERR_INVALID_INPUT, "resize frame size is invalid");
    }

    const int batch  = bottom_desc.dims[0];
    top_desc.dims[0] = batch;
    top_blob->SetBlobDesc(top_desc);
    const auto* source_base      = static_cast<const bm_device_mem_t*>(bottom_blob->GetHandle().base);
    const auto* destination_base = static_cast<const bm_device_mem_t*>(top_blob->GetHandle().base);
    const size_t dst_plane_size  = dst_frame_size / 3;
    for (int i = 0; i < batch; ++i) {
        bm_device_mem_t source{};
        std::array<bm_device_mem_t, 3> destination{};
        const size_t source_offset      = static_cast<size_t>(i) * src_frame_size;
        const size_t destination_offset = static_cast<size_t>(i) * dst_frame_size;
        if (!MakeDeviceMemoryView(*source_base, source_offset, src_frame_size, &source) ||
            !MakeThreePlaneDeviceMemoryView(*destination_base, destination_offset, dst_plane_size,
                                            &destination)) {
            return Status(COSMO_NN_ERR_INVALID_INPUT, "resize device buffer is too small");
        }
        RETURN_ON_FAIL(ResizeFrame(handle, source, bottom_desc, src_width, src_height, destination, dst_width,
                                   dst_height, gravity, color));
    }
    return Status{COSMO_NN_OK};
}

Status SophonResizeNode::Forward(std::vector<std::shared_ptr<Blob>>& bottom_blobs,
                                 std::shared_ptr<Blob>& top_blob) {
    bm_handle_t handle = shared_resource->m_handle;
    if (handle == nullptr) {
        return Status(COSMO_NN_ERR_SOPHON_HANDLE_FAILED, "bm_handle_t is null");
    }
    if (bottom_blobs.empty() || bottom_blobs.size() > static_cast<size_t>(max_batch)) {
        return Status(COSMO_NN_ERR_INVALID_INPUT, "resize batch size is invalid");
    }

    auto top_desc       = top_blob->GetBlobDesc();
    uint32_t dst_width  = 0;
    uint32_t dst_height = 0;
    if (!ValidateImageDims(top_desc.dims, &dst_width, &dst_height)) {
        return Status(COSMO_NN_ERR_INVALID_INPUT, "resize output dimensions are invalid");
    }
    size_t dst_frame_size = 0;
    if (!GetPackedFrameSize(dst_width, dst_height, &dst_frame_size)) {
        return Status(COSMO_NN_ERR_INVALID_INPUT, "resize output frame size is invalid");
    }

    top_desc.dims[0] = static_cast<int>(bottom_blobs.size());
    top_blob->SetBlobDesc(top_desc);
    const auto* destination_base = static_cast<const bm_device_mem_t*>(top_blob->GetHandle().base);
    const size_t dst_plane_size  = dst_frame_size / 3;

    for (size_t i = 0; i < bottom_blobs.size(); ++i) {
        auto bottom_desc      = bottom_blobs[i]->GetBlobDesc();
        uint32_t src_width    = 0;
        uint32_t src_height   = 0;
        size_t src_frame_size = 0;
        if (!ValidateImageDims(bottom_desc.dims, &src_width, &src_height) ||
            !GetPackedFrameSize(src_width, src_height, &src_frame_size)) {
            return Status(COSMO_NN_ERR_INVALID_INPUT, "resize input dimensions are invalid");
        }

        const auto* source_base = static_cast<const bm_device_mem_t*>(bottom_blobs[i]->GetHandle().base);
        bm_device_mem_t source{};
        std::array<bm_device_mem_t, 3> destination{};
        if (!MakeDeviceMemoryView(*source_base, 0, src_frame_size, &source) ||
            !MakeThreePlaneDeviceMemoryView(*destination_base, i * dst_frame_size, dst_plane_size,
                                            &destination)) {
            return Status(COSMO_NN_ERR_INVALID_INPUT, "resize device buffer is too small");
        }
        RETURN_ON_FAIL(ResizeFrame(handle, source, bottom_desc, src_width, src_height, destination, dst_width,
                                   dst_height, gravity, color));
    }
    return Status{COSMO_NN_OK};
}

}  // namespace cosmo::nn
