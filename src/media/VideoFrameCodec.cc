// VideoFrameCodec.cc — JPEG encode/decode operations for VideoFrameProcSophon.
// Split from VideoFrameProcSophon.cc to reduce file size (DEBT-007).

#include <memory>
#include <vector>

#include "bmcv_api_ext.h"
#include "media/PixelFormatUtils.h"
#include "media/VideoFrameProcSophon.h"
#include "util/Log.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb/stb_image_write.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb/stb_image.h"

namespace cosmo {
namespace media {
    // stb_image_write callback — appends encoded bytes to a std::vector buffer.
    static void CodecMemoryWriteFunc(void* ctx, void* data, int size) {
        auto* buffer = reinterpret_cast<std::vector<uint8_t>*>(ctx);
        auto* src    = reinterpret_cast<const uint8_t*>(data);
        buffer->insert(buffer->end(), src, src + size);
    }

    static constexpr int kImageDefaultSaveQuality = 95;

    std::vector<u_char> VideoFrameProcSophon::EncodeJpeg(const VideoFramePtr src) {
        if (!VideoFrameValid(src)) {
            return {};
        }

        auto pixel_format = src->GetPixelFormat();
        auto width        = src->GetWidth();
        auto height       = src->GetHeight();

        // convert frame to RGB format
        VideoFramePtr rgb_frame = nullptr;
        if (pixel_format != PixelFormat::PIXEL_RGB8) {
            auto src_image = VideoFrameToBMImage(src);
            if (!src_image) {
                LOG_ERRO("{}", "EncodeJPEG() - create image failed.");
                return std::vector<uint8_t>();
            }

            auto image = std::make_shared<VideoFrame>(width, height, PixelFormat::PIXEL_RGB8,
                                                      src->GetFrameIndex(), src->GetTimestamp());

            auto rgb_image = VideoFrameToBMImage(image);
            if (!rgb_image) {
                LOG_ERRO("{}", "EncodeJPEG() - create rgb image failed.");
                return std::vector<uint8_t>();
            }

            auto ret = bmcv_image_storage_convert(handle, 1, src_image.get(), rgb_image.get());

            if (ret != bm_status_t::BM_SUCCESS) {
                LOG_ERRO("EncodeJPEG() - bmcv_image_storage_convert failed {}", ret);
                return std::vector<uint8_t>();
            }

            rgb_frame = image;
        } else {
            rgb_frame = src;
        }

        // download frame data to cpu
        if (!CopyFrameFromDevice(rgb_frame)) {
            LOG_ERRO("{}", "EncodeJPEG() - copy device memory to host failed");
            return std::vector<uint8_t>();
        }

        // encode frame use stb — std::vector manages buffer lifetime automatically
        auto channel = PixelFormatUtils::PixelFormatChannels(PixelFormat::PIXEL_RGB8);
        std::vector<uint8_t> buffer;
        buffer.reserve(width * height * channel / 10);

        auto success = stbi_write_jpg_to_func(CodecMemoryWriteFunc, &buffer, static_cast<int>(width),
                                              static_cast<int>(height), static_cast<int>(channel),
                                              reinterpret_cast<void*>(rgb_frame->GetHostData()),
                                              kImageDefaultSaveQuality);
        if (!success) {
            return {};
        }

        return buffer;
    }

    VideoFramePtr VideoFrameProcSophon::DecodeJpeg(const std::vector<u_int8_t>& data) {
        if (data.empty()) {
            LOG_ERRO("{}", "DecodeJpeg() - empty data");
            return nullptr;
        }

        // Try hardware decode via Sophon JPU
        auto frame = DecodeJpegHardware(data);
        if (frame) {
            return frame;
        }

        // Hardware decode failed, try software decode (supports PNG and other formats)
        int w = 0, h = 0, channels = 0;
        // RAII: unique_ptr ensures stbi_image_free is called on all exit paths,
        // preventing leaks if subsequent allocations throw or fail.
        std::unique_ptr<unsigned char, void (*)(void*)> img_guard(
            stbi_load_from_memory(data.data(), static_cast<int>(data.size()), &w, &h, &channels, 3),
            stbi_image_free);
        if (img_guard) {
            auto rgb_frame = std::make_shared<VideoFrame>(w, h, PixelFormat::PIXEL_RGB8);
            if (!VideoFrameValid(rgb_frame, true)) {
                LOG_ERRO("{}", "DecodeJpeg() - allocate rgb frame failed");
                return nullptr;
            }

            auto dst_mem = reinterpret_cast<bm_device_mem_t*>(rgb_frame->GetData());
            auto ret     = bm_memcpy_s2d_partial(handle, *dst_mem, img_guard.get(),
                                                 static_cast<unsigned int>(rgb_frame->GetSize()));

            if (ret != BM_SUCCESS) {
                LOG_ERRO("DecodeJpeg() - bm_memcpy_s2d_partial failed {}", ret);
                return nullptr;
            }

            auto yuv_frame = RGB2I420(rgb_frame);
            if (yuv_frame) {
                return yuv_frame;
            }
        }

        // Both hardware and software decode failed
        LOG_WARN("{}", "DecodeJpeg() - decode failed for both hardware and software");
        return nullptr;
    }

    VideoFramePtr VideoFrameProcSophon::DecodeJpegHardware(const std::vector<u_int8_t>& data) {
        // SAFETY: bmcv_image_jpeg_dec takes void** (non-const) but only reads
        // the JPEG data. const_cast required at Sophon SDK C API boundary.
        void* jpeg_data  = const_cast<void*>(static_cast<const void*>(data.data()));
        size_t jpeg_size = data.size();
        bm_image decode_img{};

        auto ret = bmcv_image_jpeg_dec(handle, &jpeg_data, &jpeg_size, 1, &decode_img);
        if (ret != BM_SUCCESS) {
            LOG_WARN("DecodeJpegHardware() - bmcv_image_jpeg_dec failed, ret {}", ret);
            return nullptr;
        }

        int img_width  = decode_img.width;
        int img_height = decode_img.height;

        if (img_width <= 0 || img_height <= 0) {
            LOG_ERRO("DecodeJpegHardware() - invalid dimensions {}x{}", img_width, img_height);
            bm_image_destroy(&decode_img);
            return nullptr;
        }

        // Create output VideoFrame and bm_image, associate memory via VideoFrameAttach
        auto pixel_fmt = PixelFormat::PIXEL_I420;
        auto frame     = std::make_shared<VideoFrame>(img_width, img_height, pixel_fmt);
        if (!VideoFrameValid(frame)) {
            LOG_ERRO("{}", "DecodeJpegHardware() - create VideoFrame failed");
            bm_image_destroy(&decode_img);
            return nullptr;
        }

        auto yuv_image =
            CreateBMImage(static_cast<size_t>(img_width), static_cast<size_t>(img_height), pixel_fmt);
        if (!yuv_image) {
            LOG_ERRO("{}", "DecodeJpegHardware() - CreateBMImage failed");
            bm_image_destroy(&decode_img);
            return nullptr;
        }

        if (!VideoFrameAttach(frame, yuv_image.get())) {
            LOG_ERRO("{}", "DecodeJpegHardware() - VideoFrameAttach failed");
            bm_image_destroy(&decode_img);
            return nullptr;
        }

        // JPU decode output may be various YUV formats, convert to I420 (YUV420P)
        ret = bmcv_image_vpp_convert(handle, 1, decode_img, yuv_image.get(), nullptr, BMCV_INTER_LINEAR);
        if (ret != BM_SUCCESS) {
            // vpp convert fail, try storage convert fallback
            ret = bmcv_image_storage_convert(handle, 1, &decode_img, yuv_image.get());
        }
        bm_image_destroy(&decode_img);

        if (ret != BM_SUCCESS) {
            LOG_ERRO("DecodeJpegHardware() - image convert failed, ret {}", ret);
            return nullptr;
        }

        return frame;
    }

}  // namespace media
}  // namespace cosmo
