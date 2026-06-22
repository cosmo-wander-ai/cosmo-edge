// AiComponment — Ai Componment implementation.

#include "infer/AiComponment.h"

#ifdef COSMO_NN_USE_SOPHON_BACKEND
#include "bmcv_api_ext.h"
#include "bmlib_runtime.h"
#endif
#include "util/Log.h"

namespace cosmo {
void AppProfiler::ReportGraphInfo(const char* /*msg*/) {}

void AppProfiler::ReportNodeTime(const char* /*node_name*/, double /*time*/) {}

cosmo::nn::DeviceType GetDeviceType() {
#if defined(COSMO_NN_USE_CPU_BACKEND)
    return cosmo::nn::DeviceType::DEVICE_CPU;
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
    if (images.empty()) {
        LOG_INFO("{}", "Input Images Is Empty.");
        return util::ErrorEnum::InvalidParam;
    }

    for (size_t i = 0; i < images.size(); i++) {
        auto image = images[i];
        if (!image || !image->Active())
            continue;

#ifdef COSMO_NN_USE_CPU_BACKEND
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

#ifdef COSMO_NN_USE_CPU_BACKEND
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

util::ErrorEnum ConvertDatasToBlobs(const std::vector<std::vector<int>>& datas,
                                    std::vector<std::shared_ptr<cosmo::nn::Blob>>& blobs) {
    if (datas.empty()) {
        LOG_INFO("{}", "Input Datas Is Empty.");
        return util::ErrorEnum::InvalidParam;
    }

    for (size_t i = 0; i < datas.size(); i++) {
        auto data = datas[i];
        cosmo::nn::BlobDesc desc;
        desc.data_type = cosmo::nn::DataType::DATA_TYPE_INT32;
        desc.dims      = {1, static_cast<int>(data.size())};
        auto blob      = std::make_shared<cosmo::nn::Blob>(desc, true);
        auto blob_ptr  = reinterpret_cast<int32_t*>(blob->GetHandle().base);
        for (size_t j = 0; j < data.size(); j++) {
            blob_ptr[j] = data[j];
        }
        blobs.push_back(blob);
    }

    return util::ErrorEnum::Success;
}

util::ErrorEnum ConvertDatasToBlobsFloat(const std::vector<std::vector<int>>& datas,
                                         std::vector<std::shared_ptr<cosmo::nn::Blob>>& blobs) {
    if (datas.empty()) {
        LOG_INFO("{}", "Input Datas Is Empty.");
        return util::ErrorEnum::InvalidParam;
    }

    for (size_t i = 0; i < datas.size(); i++) {
        auto data = datas[i];
        cosmo::nn::BlobDesc desc;
        desc.data_type = cosmo::nn::DataType::DATA_TYPE_FLOAT;
        desc.dims      = {1, static_cast<int>(data.size())};
        auto blob      = std::make_shared<cosmo::nn::Blob>(desc, true);
        auto blob_ptr  = reinterpret_cast<float*>(blob->GetHandle().base);
        for (size_t j = 0; j < data.size(); j++) {
            blob_ptr[j] = static_cast<float>(data[j]);
        }
        blobs.push_back(blob);
    }

    return util::ErrorEnum::Success;
}

util::ErrorEnum ConvertDatasToBlobs(const std::vector<std::vector<std::vector<int>>>& datas,
                                    std::vector<std::shared_ptr<cosmo::nn::Blob>>& blobs) {
    if (datas.empty()) {
        LOG_INFO("{}", "Input Datas Is Empty.");
        return util::ErrorEnum::InvalidParam;
    }

    for (size_t i = 0; i < datas.size(); i++) {
        auto data = datas[i];
        if (data.size() < 1) {
            LOG_INFO("{}", "Data Is Empty.");
            return util::ErrorEnum::InvalidParam;
        }

        int targets = static_cast<int>(data.size());
        int stride  = static_cast<int>(data[0].size());
        cosmo::nn::BlobDesc desc;
        desc.data_type = cosmo::nn::DataType::DATA_TYPE_INT32;
        desc.dims      = {targets, stride};
        auto blob      = std::make_shared<cosmo::nn::Blob>(desc, true);
        auto blob_ptr  = reinterpret_cast<int32_t*>(blob->GetHandle().base);
        for (size_t j = 0; j < data.size(); j++) {
            for (size_t k = 0; k < data[j].size(); k++) {
                blob_ptr[j * static_cast<size_t>(stride) + k] = data[j][k];
            }
        }
        blobs.push_back(blob);
    }

    return util::ErrorEnum::Success;
}

util::ErrorEnum ConvertDatasToBlobsFloat(const std::vector<std::vector<std::vector<int>>>& datas,
                                         std::vector<std::shared_ptr<cosmo::nn::Blob>>& blobs) {
    if (datas.empty()) {
        LOG_INFO("{}", "Input Datas Is Empty.");
        return util::ErrorEnum::InvalidParam;
    }

    for (size_t i = 0; i < datas.size(); i++) {
        auto data = datas[i];
        if (data.size() < 1) {
            LOG_INFO("{}", "Data Is Empty.");
            return util::ErrorEnum::InvalidParam;
        }

        int targets = static_cast<int>(data.size());
        int stride  = static_cast<int>(data[0].size());
        cosmo::nn::BlobDesc desc;
        desc.data_type = cosmo::nn::DataType::DATA_TYPE_FLOAT;
        desc.dims      = {targets, stride};
        auto blob      = std::make_shared<cosmo::nn::Blob>(desc, true);
        auto blob_ptr  = reinterpret_cast<float*>(blob->GetHandle().base);
        for (size_t j = 0; j < data.size(); j++) {
            for (size_t k = 0; k < data[j].size(); k++) {
                blob_ptr[j * static_cast<size_t>(stride) + k] = static_cast<float>(data[j][k]);
            }
        }
        blobs.push_back(blob);
    }

    return util::ErrorEnum::Success;
}

void FreeBlobs(std::vector<std::shared_ptr<cosmo::nn::Blob>>& /*blobs*/) {}

}  // namespace cosmo