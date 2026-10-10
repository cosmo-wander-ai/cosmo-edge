// AiComponment — Ai Componment implementation.

#include "infer/AiComponment.h"

#include <algorithm>
#include <limits>
#include <new>

#include "nn/utils/blob_memory_size_info.h"

#ifdef COSMO_NN_USE_SOPHON_BACKEND
#include "bmcv_api_ext.h"
#include "bmlib_runtime.h"
#endif
#include "util/Log.h"

namespace cosmo {
cosmo::nn::DeviceType GetDeviceType() {
#if defined(COSMO_NN_USE_CPU_BACKEND)
    return cosmo::nn::DeviceType::DEVICE_CPU;
#elif defined(COSMO_NN_USE_RKNN_BACKEND)
    return cosmo::nn::DeviceType::DEVICE_RKNN;
#else
    return cosmo::nn::DeviceType::DEVICE_SOPHON_TPU;
#endif
}

cosmo::nn::DataFormat GetDataFormatType() {
    return cosmo::nn::DataFormat::DATA_FORMAT_NHWC;
}
cosmo::nn::ImageFormat GetImageFormatType() {
    return cosmo::nn::ImageFormat::IMAGE_BGR;
}

util::ErrorEnum ConvertImagesToBlobs(const std::vector<VideoFramePtr>& images,
                                     std::vector<std::shared_ptr<cosmo::nn::Blob>>& blobs) {
    return ConvertImagesToBlobs(images, {}, blobs);
}

util::ErrorEnum ConvertImagesToBlobs(const std::vector<VideoFramePtr>& images,
                                     const std::vector<media::NativeVideoBufferPtr>& native_buffers,
                                     std::vector<std::shared_ptr<cosmo::nn::Blob>>& blobs) {
    if (images.empty()) {
        LOG_INFO("{}", "Input Images Is Empty.");
        return util::ErrorEnum::InvalidParam;
    }

    for (size_t i = 0; i < images.size(); i++) {
        auto image = images[i];
        if (!image || !image->Active())
            continue;

#ifdef COSMO_NN_USE_HOST_BACKEND
        // CPU backend: CpuResizeNode/CpuNormalizeNode expect packed BGR/RGB (NHWC)
        // input, but DecodeJpeg() on CPU produces I420 (YUV420P planar) format.
        // Convert I420 to packed BGR before wrapping in a self-allocating blob.
        if (image->GetPixelFormat() == media::PixelFormat::PIXEL_I420) {
            int w = static_cast<int>(image->GetWidth());
            int h = static_cast<int>(image->GetHeight());

            cosmo::nn::BlobDesc bgr_desc;
            bgr_desc.data_format  = GetDataFormatType();
            bgr_desc.image_format = GetImageFormatType();  // IMAGE_BGR
            bgr_desc.data_type    = cosmo::nn::DataType::DATA_TYPE_UINT8;
            bgr_desc.dims         = {1, h, w, 3};
            bgr_desc.device_type  = GetDeviceType();

            // Blob owns its memory so data stays alive during inference
            auto bgr_blob = std::make_shared<cosmo::nn::Blob>(bgr_desc, true);
            auto* src     = image->GetData();
            auto* dst     = static_cast<uint8_t*>(bgr_blob->GetHandle().base);

            // I420 plane pointers: Y[w*h], U[w*h/4], V[w*h/4]
            const uint8_t* yp = src;
            const uint8_t* up = src + w * h;
            const uint8_t* vp = up + (w / 2) * (h / 2);

            auto clamp8 = [](int v) -> uint8_t {
                return static_cast<uint8_t>(v < 0 ? 0 : (v > 255 ? 255 : v));
            };

            for (int row = 0; row < h; row++) {
                for (int col = 0; col < w; col++) {
                    int yv = yp[row * w + col];
                    int uv = up[(row / 2) * (w / 2) + (col / 2)];
                    int vv = vp[(row / 2) * (w / 2) + (col / 2)];

                    // BT.601 YUV -> RGB
                    int c = yv - 16;
                    int d = uv - 128;
                    int e = vv - 128;

                    int r = (298 * c + 409 * e + 128) >> 8;
                    int g = (298 * c - 100 * d - 208 * e + 128) >> 8;
                    int b = (298 * c + 516 * d + 128) >> 8;

                    int idx      = (row * w + col) * 3;
                    dst[idx + 0] = clamp8(b);  // B
                    dst[idx + 1] = clamp8(g);  // G
                    dst[idx + 2] = clamp8(r);  // R
                }
            }

            LOG_DEBUG("ConvertImagesToBlobs: I420 {}x{} -> BGR for CPU pipeline", w, h);
            blobs.push_back(bgr_blob);
            continue;
        }
#endif

        cosmo::nn::BlobDesc desc;
        auto blob         = std::make_shared<cosmo::nn::Blob>(desc);
        desc.data_format  = GetDataFormatType();
        desc.image_format = GetImageFormatType();
        desc.data_type    = cosmo::nn::DataType::DATA_TYPE_UINT8;
        desc.dims         = {1, static_cast<int>(image->GetHeight()), static_cast<int>(image->GetWidth()),
                             static_cast<int>(cosmo::nn::ImageFormatChannels(desc.image_format))};
        desc.device_type  = GetDeviceType();
        cosmo::nn::BlobHandle handle;
        handle.base = image->GetData();
        if (i < native_buffers.size() && native_buffers[i] && native_buffers[i]->Valid() &&
            native_buffers[i]->width == static_cast<int>(image->GetWidth()) &&
            native_buffers[i]->height == static_cast<int>(image->GetHeight())) {
            const auto& native                = *native_buffers[i];
            handle.native_image.fd            = native.fd;
            handle.native_image.bytes         = native.bytes;
            handle.native_image.width         = native.width;
            handle.native_image.height        = native.height;
            handle.native_image.width_stride  = native.width_stride;
            handle.native_image.height_stride = native.height_stride;
            if (native.color_space == media::NativeVideoColorSpace::Bt601) {
                handle.native_image.color_space = cosmo::nn::NativeImageColorSpace::Bt601;
            } else if (native.color_space == media::NativeVideoColorSpace::Bt709) {
                handle.native_image.color_space = cosmo::nn::NativeImageColorSpace::Bt709;
            } else if (native.color_space == media::NativeVideoColorSpace::Bt2020) {
                handle.native_image.color_space = cosmo::nn::NativeImageColorSpace::Bt2020;
            }
            if (native.color_range == media::NativeVideoColorRange::Limited) {
                handle.native_image.color_range = cosmo::nn::NativeImageColorRange::Limited;
            } else if (native.color_range == media::NativeVideoColorRange::Full) {
                handle.native_image.color_range = cosmo::nn::NativeImageColorRange::Full;
            }
            if (native.format == media::NativeVideoBufferFormat::NV12) {
                handle.native_image.format = cosmo::nn::IMAGE_NV12;
            } else if (native.format == media::NativeVideoBufferFormat::I420) {
                handle.native_image.format = cosmo::nn::IMAGE_I420;
            }
        }
        blob->SetBlobDesc(desc);
        blob->SetHandle(handle);
        blobs.push_back(blob);
    }

    return blobs.empty() ? util::ErrorEnum::InvalidParam : util::ErrorEnum::Success;
}

std::shared_ptr<cosmo::nn::Blob> ConvertImageToBlob(VideoFramePtr image) {
    if (!image) {
        LOG_INFO("{}", "Input Image Is Empty.");
        return nullptr;
    }

#ifdef COSMO_NN_USE_HOST_BACKEND
    // CPU backend: same I420 -> BGR conversion as ConvertImagesToBlobs
    if (image->GetPixelFormat() == media::PixelFormat::PIXEL_I420) {
        int w = static_cast<int>(image->GetWidth());
        int h = static_cast<int>(image->GetHeight());

        cosmo::nn::BlobDesc bgr_desc;
        bgr_desc.data_format  = GetDataFormatType();
        bgr_desc.image_format = GetImageFormatType();
        bgr_desc.data_type    = cosmo::nn::DataType::DATA_TYPE_UINT8;
        bgr_desc.dims         = {1, h, w, 3};
        bgr_desc.device_type  = GetDeviceType();

        auto bgr_blob = std::make_shared<cosmo::nn::Blob>(bgr_desc, true);
        auto* src     = image->GetData();
        auto* dst     = static_cast<uint8_t*>(bgr_blob->GetHandle().base);

        const uint8_t* yp = src;
        const uint8_t* up = src + w * h;
        const uint8_t* vp = up + (w / 2) * (h / 2);

        auto clamp8 = [](int v) -> uint8_t { return static_cast<uint8_t>(v < 0 ? 0 : (v > 255 ? 255 : v)); };

        for (int row = 0; row < h; row++) {
            for (int col = 0; col < w; col++) {
                int yv = yp[row * w + col];
                int uv = up[(row / 2) * (w / 2) + (col / 2)];
                int vv = vp[(row / 2) * (w / 2) + (col / 2)];

                int c = yv - 16;
                int d = uv - 128;
                int e = vv - 128;

                int r = (298 * c + 409 * e + 128) >> 8;
                int g = (298 * c - 100 * d - 208 * e + 128) >> 8;
                int b = (298 * c + 516 * d + 128) >> 8;

                int idx      = (row * w + col) * 3;
                dst[idx + 0] = clamp8(b);
                dst[idx + 1] = clamp8(g);
                dst[idx + 2] = clamp8(r);
            }
        }

        return bgr_blob;
    }
#endif

    cosmo::nn::BlobDesc desc;
    auto blob         = std::make_shared<cosmo::nn::Blob>(desc);
    desc.data_format  = GetDataFormatType();
    desc.image_format = GetImageFormatType();
    desc.data_type    = cosmo::nn::DataType::DATA_TYPE_UINT8;
    desc.dims         = {1, static_cast<int>(image->GetHeight()), static_cast<int>(image->GetWidth()),
                         static_cast<int>(cosmo::nn::ImageFormatChannels(desc.image_format))};
    desc.device_type  = GetDeviceType();
    cosmo::nn::BlobHandle handle;
    handle.base = image->GetData();
    blob->SetBlobDesc(desc);
    blob->SetHandle(handle);

    return blob;
}

namespace {

    util::ErrorEnum AllocateDataBlob(size_t rows, size_t columns, nn::DataType type,
                                     std::shared_ptr<nn::Blob>& blob) {
        const auto max_dimension = static_cast<size_t>(std::numeric_limits<int>::max());
        if (rows == 0 || columns == 0 || columns > max_dimension || rows > max_dimension / columns) {
            return util::ErrorEnum::InvalidParam;
        }
        // Match NaiveDevice's flattened element count before narrowing or allocating.
        const nn::BlobMemorySizeInfo size_info{type, {static_cast<int>(rows * columns)}};
        if (nn::GetBlobMemoryBytesSize(size_info) <= 0) {
            return util::ErrorEnum::InvalidParam;
        }
        nn::BlobDesc desc;
        desc.data_type = type;
        desc.dims      = {static_cast<int>(rows), static_cast<int>(columns)};
        blob           = std::make_shared<nn::Blob>(desc, true);
        return blob->GetHandle().base ? util::ErrorEnum::Success : util::ErrorEnum::NoMem;
    }

}  // namespace

util::ErrorEnum ConvertDatasToBlobs(const std::vector<std::vector<int>>& datas,
                                    std::vector<std::shared_ptr<nn::Blob>>& blobs) {
    if (datas.empty()) {
        return util::ErrorEnum::InvalidParam;
    }
    try {
        std::vector<std::shared_ptr<nn::Blob>> pending;
        pending.reserve(datas.size());
        for (const auto& data : datas) {
            std::shared_ptr<nn::Blob> blob;
            const auto result = AllocateDataBlob(1, data.size(), nn::DATA_TYPE_INT32, blob);
            if (result != util::ErrorEnum::Success) {
                return result;
            }
            auto* destination = static_cast<int32_t*>(blob->GetHandle().base);
            for (size_t column = 0; column < data.size(); ++column) {
                destination[column] = static_cast<int32_t>(data[column]);
            }
            pending.push_back(std::move(blob));
        }
        blobs.insert(blobs.end(), pending.begin(), pending.end());
    } catch (const std::bad_alloc&) {
        return util::ErrorEnum::NoMem;
    }
    return util::ErrorEnum::Success;
}

util::ErrorEnum ConvertDatasToBlobsFloat(const std::vector<std::vector<int>>& datas,
                                         std::vector<std::shared_ptr<nn::Blob>>& blobs) {
    if (datas.empty()) {
        return util::ErrorEnum::InvalidParam;
    }
    try {
        std::vector<std::shared_ptr<nn::Blob>> pending;
        pending.reserve(datas.size());
        for (const auto& data : datas) {
            std::shared_ptr<nn::Blob> blob;
            const auto result = AllocateDataBlob(1, data.size(), nn::DATA_TYPE_FLOAT, blob);
            if (result != util::ErrorEnum::Success) {
                return result;
            }
            auto* destination = static_cast<float*>(blob->GetHandle().base);
            for (size_t column = 0; column < data.size(); ++column) {
                destination[column] = static_cast<float>(data[column]);
            }
            pending.push_back(std::move(blob));
        }
        blobs.insert(blobs.end(), pending.begin(), pending.end());
    } catch (const std::bad_alloc&) {
        return util::ErrorEnum::NoMem;
    }
    return util::ErrorEnum::Success;
}

util::ErrorEnum ConvertDatasToBlobs(const std::vector<std::vector<std::vector<int>>>& datas,
                                    std::vector<std::shared_ptr<nn::Blob>>& blobs) {
    if (datas.empty()) {
        return util::ErrorEnum::InvalidParam;
    }
    try {
        std::vector<std::shared_ptr<nn::Blob>> pending;
        pending.reserve(datas.size());
        for (const auto& data : datas) {
            if (data.empty() || data.front().empty() ||
                !std::all_of(data.begin(), data.end(),
                             [&](const auto& row) { return row.size() == data.front().size(); })) {
                return util::ErrorEnum::InvalidParam;
            }
            std::shared_ptr<nn::Blob> blob;
            const auto result = AllocateDataBlob(data.size(), data.front().size(), nn::DATA_TYPE_INT32, blob);
            if (result != util::ErrorEnum::Success) {
                return result;
            }
            auto* destination  = static_cast<int32_t*>(blob->GetHandle().base);
            const auto columns = data.front().size();
            for (size_t row = 0; row < data.size(); ++row) {
                for (size_t column = 0; column < columns; ++column) {
                    destination[row * columns + column] = static_cast<int32_t>(data[row][column]);
                }
            }
            pending.push_back(std::move(blob));
        }
        blobs.insert(blobs.end(), pending.begin(), pending.end());
    } catch (const std::bad_alloc&) {
        return util::ErrorEnum::NoMem;
    }
    return util::ErrorEnum::Success;
}

util::ErrorEnum ConvertDatasToBlobsFloat(const std::vector<std::vector<std::vector<int>>>& datas,
                                         std::vector<std::shared_ptr<nn::Blob>>& blobs) {
    if (datas.empty()) {
        return util::ErrorEnum::InvalidParam;
    }
    try {
        std::vector<std::shared_ptr<nn::Blob>> pending;
        pending.reserve(datas.size());
        for (const auto& data : datas) {
            if (data.empty() || data.front().empty() ||
                !std::all_of(data.begin(), data.end(),
                             [&](const auto& row) { return row.size() == data.front().size(); })) {
                return util::ErrorEnum::InvalidParam;
            }
            std::shared_ptr<nn::Blob> blob;
            const auto result = AllocateDataBlob(data.size(), data.front().size(), nn::DATA_TYPE_FLOAT, blob);
            if (result != util::ErrorEnum::Success) {
                return result;
            }
            auto* destination  = static_cast<float*>(blob->GetHandle().base);
            const auto columns = data.front().size();
            for (size_t row = 0; row < data.size(); ++row) {
                for (size_t column = 0; column < columns; ++column) {
                    destination[row * columns + column] = static_cast<float>(data[row][column]);
                }
            }
            pending.push_back(std::move(blob));
        }
        blobs.insert(blobs.end(), pending.begin(), pending.end());
    } catch (const std::bad_alloc&) {
        return util::ErrorEnum::NoMem;
    }
    return util::ErrorEnum::Success;
}

void FreeBlobs(std::vector<std::shared_ptr<cosmo::nn::Blob>>& /*blobs*/) {}

}  // namespace cosmo
