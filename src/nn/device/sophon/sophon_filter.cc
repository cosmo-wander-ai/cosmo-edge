#include "nn/device/sophon/sophon_filter.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <sstream>

namespace cosmo::nn {

SophonFilter::SophonFilter(DeviceType type) : AbstractFilter(type) {
    memory_allocated = false;
}

SophonFilter::~SophonFilter() {
    if (memory_allocated) {
        FreeDeviceMemory(resized_image_b.get());
        FreeDeviceMemory(resized_image_g.get());
        FreeDeviceMemory(resized_image_r.get());
        bm_dev_free(bm_handle);
    }
}

void SophonFilter::FreeDeviceMemory(bm_device_mem_t* mem) {
    if (mem) {
        bm_free_device(bm_handle, *mem);
    }
}

Status SophonFilter::Clear() {
    first_frame = true;
    return COSMO_NN_OK;
}

Status SophonFilter::Update(std::shared_ptr<Blob> blob, int x, int y, int w, int h, float* average) {
    auto blob_desc = blob->GetBlobDesc();
    auto device    = blob_desc.device_type;
    if (device != DeviceType::DEVICE_SOPHON_TPU)
        return Status(COSMO_NN_ERR_INVALID_INPUT, "Incompatible device type.");

    auto image_fmt = blob_desc.image_format;
    if (!ImageFormatIsBGR(image_fmt) && !ImageFormatIsRGB(image_fmt))
        return Status(COSMO_NN_ERR_INVALID_INPUT, "Incompatible image format.");

    bm_status_t status = bm_status_t::BM_SUCCESS;
    if (!memory_allocated) {
        // bm handle
        status = bm_dev_request(&bm_handle, 0);
        if (status != bm_status_t::BM_SUCCESS) {
            return Status(COSMO_NN_ERR_SOPHON_REQUEST_HANDLE_FAILED, "bm_dev_request failed.");
        }

        RETURN_ON_FAIL(MallocDeviceMemory(resized_image_b, GetElementCount()));
        RETURN_ON_FAIL(MallocDeviceMemory(resized_image_g, GetElementCount()));
        RETURN_ON_FAIL(MallocDeviceMemory(resized_image_r, GetElementCount()));

        host_current_image.resize(GetElementCount());
        host_previous_image.resize(GetElementCount());
        host_abs_diff_binary.resize(GetElementCount());
        host_dilate.resize(GetElementCount());
        host_blur.resize(GetElementCount());
        host_prob.resize(GetElementCount());
        host_prob_thresh.resize(GetElementCount());

        memory_allocated = true;
    }

    if (first_frame) {
        // prob set to zero
        std::fill(host_prob.begin(), host_prob.end(), 0.0f);

        // extract r channel and reszie to target size
        RETURN_ON_FAIL(ExtractRChannelAndResize(blob, x, y, w, h));

        status =
            bm_memcpy_d2s_partial(bm_handle, host_previous_image.data(), *resized_image_r, GetElementCount());
        if (status != BM_SUCCESS) {
            return Status(COSMO_NN_ERR_SOPHON_MEMCPY_ERR, "bm_memcpy_d2s_partial failed");
        }

        *average = 0;

        first_frame = false;
        return COSMO_NN_OK;
    }

    return StreamOrderedUpdate(blob, x, y, w, h, average);
}

Status SophonFilter::StreamOrderedUpdate(std::shared_ptr<Blob> blob, int x, int y, int w, int h,
                                         float* average) {
    RETURN_ON_FAIL(ExtractRChannelAndResize(blob, x, y, w, h));
    RETURN_ON_FAIL(AbsDiffBinary(10.f, 0.f, 128.f));

    // dilate
    Dilate(host_abs_diff_binary.data(), host_dilate.data(), GetWidth(), GetHeight());

    // avgerage filter
    Blur(host_dilate.data(), host_blur.data(), GetWidth(), GetHeight());

    for (int i = 0; i < GetElementCount(); i++) {
        /**
         * Arithmetic
         *  / 255
         *  * (pos + neg)
         *  - neg
         */
        host_blur[i] = (host_blur[i] / 255.0f) * (GetPosProb() + GetNegProb()) - GetNegProb();

        // prbo add
        host_prob[i] += host_blur[i];
        host_prob[i] = std::max(0.0f, std::min(255.0f, host_prob[i]));

        // prob thresh
        host_prob_thresh[i] = host_prob[i];
        if (host_prob_thresh[i] < sensi_value)
            host_prob_thresh[i] = 0;

        if (host_prob_thresh[i] >= 250)
            host_prob_thresh[i] = 255;
    }

    float sum = 0;
    for (int i = 0; i < GetElementCount(); i++) {
        sum += host_prob_thresh[i];
    }

    *average = sum / GetElementCount();

    // copy current image to previous image
    auto status =
        bm_memcpy_d2s_partial(bm_handle, host_previous_image.data(), *resized_image_r, GetElementCount());
    if (status != BM_SUCCESS) {
        return Status(COSMO_NN_ERR_SOPHON_MEMCPY_ERR, "bm_memcpy_d2s_partial failed");
    }

    return COSMO_NN_OK;
}

Status SophonFilter::SetDeviceId(int id) {
    current_device_id = id;
    return COSMO_NN_OK;
}

Status SophonFilter::MallocDeviceMemory(std::unique_ptr<bm_device_mem_t>& mem, int size) {
    mem         = std::make_unique<bm_device_mem_t>();
    auto status = bm_malloc_device_byte(bm_handle, mem.get(), size);
    if (status != bm_status_t::BM_SUCCESS) {
        mem.reset();
        return Status(COSMO_NN_ERR_SOPHON_ALLOC_MEM_FAILED, "Failed to malloc device memory");
    }
    return COSMO_NN_OK;
}

void SophonFilter::ClearBMImage(bm_image* image) {
    if (image) {
        if (bm_image_is_attached(*image)) {
            bm_image_detach(*image);
        }

#ifdef COSMO_NN_SOPHON_1684X
        bm_image_destroy(*image);
#else
        bm_image_destroy(image);
#endif
    }
}

Status SophonFilter::ExtractRChannelAndResize(std::shared_ptr<Blob> blob, int x, int y, int w, int h) {
    auto blob_desc      = blob->GetBlobDesc();
    auto blob_handle    = blob->GetHandle();
    auto blob_dims      = blob_desc.dims;
    auto blob_image_fmt = blob_desc.image_format;

    int image_width  = blob_dims.at(2);
    int image_height = blob_dims.at(1);

    if (x < 0 || y < 0 || x + w > image_width || y + h > image_height) {
        std::stringstream ss;
        ss << "image size:" << image_width << "x" << image_height << ",Invalid roi: " << x << ", " << y
           << ", " << w << ", " << h;
        return Status(COSMO_NN_ERR_PARAM, ss.str());
    }

    auto src_dev_mem = reinterpret_cast<bm_device_mem_t*>(blob_handle.base);

    auto src_image = std::make_unique<bm_image>();
    RETURN_ON_FAIL(AttachImage(src_image.get(), image_width, image_height,
                               bm_image_format_ext::FORMAT_BGR_PACKED, src_dev_mem));

    auto resize_image = std::make_unique<bm_image>();
    bm_device_mem_t mems[3];
    mems[0] = *resized_image_r;
    mems[1] = *resized_image_g;
    mems[2] = *resized_image_b;
    RETURN_ON_FAIL(AttachImage(resize_image.get(), GetWidth(), GetHeight(),
                               bm_image_format_ext::FORMAT_RGB_PLANAR, mems));

    bmcv_rect_t crop_rect;
    crop_rect.start_x = x;
    crop_rect.start_y = y;
    crop_rect.crop_w  = w;
    crop_rect.crop_h  = h;

    bmcv_image_vpp_convert(bm_handle, 1, *src_image, resize_image.get(), &crop_rect);

    ClearBMImage(src_image.get());
    ClearBMImage(resize_image.get());

    return COSMO_NN_OK;
}

Status SophonFilter::AttachImage(bm_image* image, int width, int height, bm_image_format_ext format,
                                 bm_device_mem_t* mem) {
    auto status = bm_image_create(bm_handle, height, width, format,
                                  bm_image_data_format_ext::DATA_TYPE_EXT_1N_BYTE, image);
    if (status != bm_status_t::BM_SUCCESS) {
        free(image);
        return Status(COSMO_NN_ERR_SOPHON_BMIMAGE_CREAT_FAILED, "bm_image_create failed");
    }

    status = bm_image_attach(*image, mem);
    if (status != bm_status_t::BM_SUCCESS) {
        ClearBMImage(image);
        return Status(COSMO_NN_ERR_SOPHON_ATTACH_FAILED, "bm_image attach failed.");
    }
    return COSMO_NN_OK;
}

Status SophonFilter::AbsDiffBinary(float threshold, float val_low, float val_high) {
    // copy data to host
    auto status =
        bm_memcpy_d2s_partial(bm_handle, host_current_image.data(), *resized_image_r, GetElementCount());
    if (status != BM_SUCCESS) {
        return Status(COSMO_NN_ERR_SOPHON_MEMCPY_ERR, "bm_memcpy_d2d_byte failed");
    }

    for (int i = 0; i < GetElementCount(); i++) {
        auto diff = fabs(host_current_image[i] - host_previous_image[i]);
        if (diff > threshold) {
            host_abs_diff_binary[i] = val_high;
        } else {
            host_abs_diff_binary[i] = val_low;
        }
    }

    return COSMO_NN_OK;
}

void SophonFilter::Dilate(float* src, float* dst, int w, int h, int kernel_size) {
    int pad = kernel_size / 2;

    for (int i = 0; i < h; i++) {
        for (int j = 0; j < w; j++) {
            float max = 0;
            float val = 0;
            for (int ki = -pad; ki <= pad; ki++) {
                for (int kj = -pad; kj <= pad; kj++) {
                    int ni = i + ki;
                    int nj = j + kj;

                    if (ni >= 0 && ni < h && nj >= 0 && nj < w) {
                        val = src[ni * w + nj];
                    } else {
                        // edge
                        int y = std::max(0, std::min(ni, h - 1));
                        int x = std::max(0, std::min(nj, w - 1));

                        val = src[y * w + x];
                    }

                    if (val >= max) {
                        max = val;
                    }
                }
            }

            dst[i * w + j] = max;
        }
    }
}

void SophonFilter::Blur(float* src, float* dst, int w, int h, int kernel_size) {
    int pad = kernel_size / 2;

    const int count = kernel_size * kernel_size;
    for (int i = 0; i < h; i++) {
        for (int j = 0; j < w; j++) {
            float sum = 0;

            for (int ki = -pad; ki <= pad; ki++) {
                for (int kj = -pad; kj <= pad; kj++) {
                    int ni = i + ki;
                    int nj = j + kj;

                    if (ni >= 0 && ni < h && nj >= 0 && nj < w) {
                        sum += src[ni * w + nj];
                    } else {
                        // edge
                        int y = std::max(0, std::min(ni, h - 1));
                        int x = std::max(0, std::min(nj, w - 1));

                        sum += src[y * w + x];
                    }
                }
            }

            dst[i * w + j] = sum / count;
        }
    }
}

DECLARE_FILTER_CREATER(Sophon);
REGISTER_FILTER(Sophon, DEVICE_SOPHON_TPU);

}  // namespace cosmo::nn