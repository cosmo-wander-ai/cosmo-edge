// VideoFrameProcSophon.cc — Color conversion, crop, resize, padding and internal helpers.
// Drawing operations are in VideoFrameDrawing.cc.
// JPEG encode/decode operations are in VideoFrameCodec.cc.

#include "media/VideoFrameProcSophon.h"

#include <mutex>

#include "bmcv_api_ext.h"
#include "media/PixelFormatUtils.h"
#include "util/Log.h"

// #define DEBUG_DURATION

namespace cosmo {
namespace media {

    VideoFrameProcSophon::VideoFrameProcSophon(void* mediaHandle, IOsdTextRenderer& osdService)
        : handle(reinterpret_cast<bm_handle_t>(mediaHandle)), osd_service_(osdService) {}

    VideoFrameProcSophon::~VideoFrameProcSophon() {}

    // Encoding in VRAM on Sophon
    VideoFramePtr VideoFrameProcSophon::CopyFrame(VideoFramePtr frame) {
        VideoFramePtr dst =
            std::make_shared<VideoFrame>(frame->GetWidth(), frame->GetHeight(), frame->GetPixelFormat(),
                                         frame->GetFrameIndex(), frame->GetTimestamp());
        if (!VideoFrameValid(dst, true)) {
            return nullptr;
        }
        dst->SetStreamIndex(frame->GetStreamIndex());

        auto src_host = frame->GetHostData();
        auto dst_mem  = reinterpret_cast<bm_device_mem_t*>(dst->GetData());

        bm_status_t ret = bm_status_t::BM_SUCCESS;
        if (src_host) {
            ret = bm_memcpy_s2d_partial(handle, *dst_mem, src_host,
                                        static_cast<unsigned int>(frame->GetSize()));
        } else {
            auto src_mem = reinterpret_cast<bm_device_mem_t*>(frame->GetData());
            ret          = bm_memcpy_d2d_byte(handle, *dst_mem, 0, *src_mem, 0, frame->GetSize());
        }

        if (ret != BM_SUCCESS) {
            LOG_WARN("CopyFrame() - failed, ret {}", ret);
            dst.reset();
            return nullptr;
        }
        return dst;
    }

    bool VideoFrameProcSophon::EnsureHostData(VideoFramePtr frame) {
        return CopyFrameFromDevice(frame);
    }

    VideoFramePtr VideoFrameProcSophon::ConvertPixelFormat(VideoFramePtr frame, PixelFormat src_fmt,
                                                           PixelFormat dst_fmt, const char* caller) {
        if (!VideoFrameValid(frame)) {
            LOG_ERRO("{}", "INPUT IMAGE INVALID");
            return nullptr;
        }

        auto width  = frame->GetWidth();
        auto height = frame->GetHeight();

        auto src_image = CreateBMImage(width, height, src_fmt);
        if (!src_image) {
            LOG_ERRO("{}() - Create source image failed", caller);
            return nullptr;
        }

        if (!VideoFrameAttach(frame, src_image.get())) {
            LOG_ERRO("{}() - bm_image_attach source failed", caller);
            return nullptr;
        }

        VideoFramePtr dst = std::make_shared<VideoFrame>(width, height, dst_fmt);
        auto dst_image    = CreateBMImage(width, height, dst_fmt);
        if (!dst_image) {
            LOG_ERRO("{}() - Create destination image failed", caller);
            return nullptr;
        }

        if (!VideoFrameAttach(dst, dst_image.get())) {
            LOG_ERRO("{}() - bm_image_attach destination failed", caller);
            return nullptr;
        }

        auto ret = bmcv_image_storage_convert(handle, 1, src_image.get(), dst_image.get());
        if (ret != BM_SUCCESS) {
            LOG_ERRO("bmcv_image_storage_convert failed:{}", ret);
            return nullptr;
        }

        return dst;
    }

    VideoFramePtr VideoFrameProcSophon::BGR2I420(VideoFramePtr frame) {
        return ConvertPixelFormat(frame, PixelFormat::PIXEL_BGR8, PixelFormat::PIXEL_I420, "BGR2I420");
    }

    VideoFramePtr VideoFrameProcSophon::RGB2I420(VideoFramePtr frame) {
        return ConvertPixelFormat(frame, PixelFormat::PIXEL_RGB8, PixelFormat::PIXEL_I420, "RGB2I420");
    }

    VideoFramePtr VideoFrameProcSophon::I4202BGR(VideoFramePtr frame) {
        return ConvertPixelFormat(frame, PixelFormat::PIXEL_I420, PixelFormat::PIXEL_BGR8, "I4202BGR");
    }

    VideoFramePtr VideoFrameProcSophon::I4202RGB(VideoFramePtr frame) {
        return ConvertPixelFormat(frame, PixelFormat::PIXEL_I420, PixelFormat::PIXEL_RGB8, "I4202RGB");
    }

    VideoFramePtr VideoFrameProcSophon::Crop(const VideoFramePtr srcPicture, const util::Box roi) {
#ifdef DEBUG_DURATION
        auto timpointIn = std::chrono::high_resolution_clock::now();
#endif

        if (!VideoFrameValid(srcPicture)) {
            return nullptr;
        }

        if (srcPicture->GetPixelFormat() != PixelFormat::PIXEL_I420) {
            LOG_ERRO("FrameType {} Not Support", srcPicture->GetPixelFormat());
            return nullptr;
        }

        int height = static_cast<int>(srcPicture->GetHeight());
        int width  = static_cast<int>(srcPicture->GetWidth());

#ifdef DEBUG_DURATION
        auto timpointCreate = std::chrono::high_resolution_clock::now();
#endif

        auto image = CreateBMImage(static_cast<size_t>(width), static_cast<size_t>(height),
                                   srcPicture->GetPixelFormat());
        if (!image) {
            LOG_WARN("{}", "Crop() - bm_image_create failed");
            return nullptr;
        }

#ifdef DEBUG_DURATION
        auto timpointAttach = std::chrono::high_resolution_clock::now();
#endif

        if (!VideoFrameAttach(srcPicture, image.get())) {
            LOG_WARN("{}", "bm_image_attach failed");
            return nullptr;
        }

        int crop_width  = roi.width;
        int crop_height = roi.height;

#ifdef DEBUG_DURATION
        auto timpointCreateCrop = std::chrono::high_resolution_clock::now();
#endif

        auto crop_image = CreateBMImage(static_cast<size_t>(crop_width), static_cast<size_t>(crop_height),
                                        srcPicture->GetPixelFormat());
        if (!crop_image) {
            LOG_WARN("{}", "bm_image_create failed ");
            return nullptr;
        }

#ifdef DEBUG_DURATION
        auto timpointDeviceMem = std::chrono::high_resolution_clock::now();
#endif

        VideoFramePtr result =
            std::make_shared<VideoFrame>(crop_width, crop_height, PixelFormat::PIXEL_I420,
                                         srcPicture->GetFrameIndex(), srcPicture->GetTimestamp());

#ifdef DEBUG_DURATION
        auto timpointDeviceMemAtach = std::chrono::high_resolution_clock::now();
#endif

        if (!VideoFrameAttach(result, crop_image.get())) {
            LOG_WARN("{}", "bm_image_attach failed");
            return nullptr;
        }

        bmcv_rect_t rect;
        rect.start_x = static_cast<unsigned int>(roi.x);
        rect.start_y = static_cast<unsigned int>(roi.y);
        rect.crop_w  = static_cast<unsigned int>(roi.width);
        rect.crop_h  = static_cast<unsigned int>(roi.height);

#ifdef DEBUG_DURATION
        auto timpointConvert = std::chrono::high_resolution_clock::now();
#endif

        auto ret = bmcv_image_vpp_convert(handle, 1, *image, crop_image.get(), &rect, BMCV_INTER_LINEAR);

#ifdef DEBUG_DURATION
        auto timpointDestroy = std::chrono::high_resolution_clock::now();
#endif

        if (ret != BM_SUCCESS) {
            LOG_ERRO("bmcv_image_vpp_convert failed {}", ret);
            return nullptr;
        }

#ifdef DEBUG_DURATION
        auto timpointEnd = std::chrono::high_resolution_clock::now();
        LOG_INFO(
            "Duration: In:{} Create:{} Attach:{} CreateCrop:{} DeviceMem:{} DeviceMemAtach:{} "
            "Destroy:{} Free:{}",
            std::chrono::duration_cast<std::chrono::milliseconds>(timpointMalloc - timpointIn).count(),
            std::chrono::duration_cast<std::chrono::milliseconds>(timpointAttach - timpointCreate).count(),
            std::chrono::duration_cast<std::chrono::milliseconds>(timpointCreateCrop - timpointAttach)
                .count(),
            std::chrono::duration_cast<std::chrono::milliseconds>(timpointDeviceMem - timpointCreateCrop)
                .count(),
            std::chrono::duration_cast<std::chrono::milliseconds>(timpointDeviceMemAtach - timpointDeviceMem)
                .count(),
            std::chrono::duration_cast<std::chrono::milliseconds>(timpointConvert - timpointDeviceMemAtach)
                .count(),
            std::chrono::duration_cast<std::chrono::milliseconds>(timpointDestroy - timpointConvert).count(),
            std::chrono::duration_cast<std::chrono::milliseconds>(timpointEnd - timpointDestroy).count());
#endif

        return result;
    }

    VideoFramePtr VideoFrameProcSophon::Resize(VideoFramePtr src, int dst_height, int dst_width) {
        if (dst_height < 0 || dst_width < 0) {
            LOG_ERRO("invalid params {} {}", dst_height, dst_width);
            return nullptr;
        }

        if (!VideoFrameValid(src, true)) {
            LOG_ERRO("{}", "invalid frame ");
            return nullptr;
        }

        VideoFramePtr framePacket = src;
        if (framePacket->GetPixelFormat() != PixelFormat::PIXEL_I420) {
            LOG_ERRO("{}", "FrameType Not Support");
            return nullptr;
        }

        auto src_height = framePacket->GetHeight();
        auto src_width  = framePacket->GetWidth();

        auto image = CreateBMImage(src_width, src_height, framePacket->GetPixelFormat());
        if (!image) {
            LOG_WARN("{}", "Resize() - CreateBMImage failed ");
            return nullptr;
        }

        if (!VideoFrameAttach(src, image.get())) {
            LOG_ERRO("{}", "VideoFrameAttach failed");
            return nullptr;
        }

        auto resize_image = CreateBMImage(static_cast<size_t>(dst_width), static_cast<size_t>(dst_height),
                                          framePacket->GetPixelFormat());
        if (!resize_image) {
            LOG_WARN("{}", "Resize() - CreateBMImage failed ");
            return nullptr;
        }

        VideoFramePtr result = std::make_shared<VideoFrame>(dst_width, dst_height, src->GetPixelFormat(),
                                                            src->GetFrameIndex(), src->GetTimestamp());

        if (!VideoFrameAttach(result, resize_image.get())) {
            LOG_WARN("{}", "Resize() - VideoFrameAttach failed");

            return nullptr;
        }

        bmcv_rect_t crop_rect;
        crop_rect.start_x = 0;
        crop_rect.start_y = 0;
        crop_rect.crop_w  = static_cast<unsigned int>(src_width);
        crop_rect.crop_h  = static_cast<unsigned int>(src_height);

        auto ret = bmcv_image_vpp_convert(handle, 1, *image, resize_image.get(), &crop_rect);

        if (ret != BM_SUCCESS) {
            LOG_ERRO("Resize() - bmcv_image_vpp_basic failed {}", ret);
            return nullptr;
        }
        return result;
    }

    VideoFramePtr VideoFrameProcSophon::Padding(VideoFramePtr src, size_t top, size_t bottom, size_t left,
                                                size_t right, Color color) {
        // Parameters are size_t (unsigned) — no negative-value guard needed

        if (!VideoFrameValid(src, true)) {
            LOG_ERRO("{}", "invalid frame ");
            return nullptr;
        }

        VideoFramePtr framePacket = src;
        if (framePacket->GetPixelFormat() != PixelFormat::PIXEL_I420) {
            LOG_ERRO("{}", "FrameType Not Support");
            return nullptr;
        }

        auto height = framePacket->GetHeight();
        auto width  = framePacket->GetWidth();

        auto image = CreateBMImage(width, height, framePacket->GetPixelFormat());
        if (!image) {
            LOG_WARN("{}", "Padding() - CreateBMImage failed");
            return nullptr;
        }

        if (!VideoFrameAttach(src, image.get())) {
            LOG_ERRO("{}", "VideoFrameAttach failed");
            return nullptr;
        }

        size_t new_width  = width + left + right;
        size_t new_height = height + top + bottom;

        auto padding_image = CreateBMImage(new_width, new_height, framePacket->GetPixelFormat());
        if (!padding_image) {
            LOG_WARN("{}", "Padding() - CreateBMImage failed.");
            return nullptr;
        }

        // attach padding image to device memory
        VideoFramePtr result =
            std::make_shared<VideoFrame>(new_width, new_height, framePacket->GetPixelFormat());
        if (!VideoFrameAttach(result, padding_image.get())) {
            LOG_WARN("{}", "bm_image_attach failed");
            return nullptr;
        }

        bmcv_padding_attr_t padding_attr;
        padding_attr.dst_crop_stx = static_cast<unsigned int>(left);
        padding_attr.dst_crop_sty = static_cast<unsigned int>(top);
        padding_attr.dst_crop_w   = static_cast<unsigned int>(width);
        padding_attr.dst_crop_h   = static_cast<unsigned int>(height);
        padding_attr.if_memset    = 1;
        padding_attr.padding_r    = color.red;
        padding_attr.padding_g    = color.green;
        padding_attr.padding_b    = color.blue;

        auto ret = bmcv_image_vpp_basic(handle, 1, image.get(), padding_image.get(), nullptr, nullptr,
                                        &padding_attr);

        if (ret != BM_SUCCESS) {
            LOG_ERRO("bmcv_image_vpp_basic failed {}", ret);
            return nullptr;
        }

        return result;
    }

    bool VideoFrameProcSophon::VideoFrameAttach(VideoFramePtr frame, bm_image* image) {
        if (!VideoFrameValid(frame, true)) {
            return false;
        }

        if (!image) {
            LOG_ERRO("{}", "VideoFrameAttach() image is nullptr");
            return false;
        }

        auto image_format = image->image_format;
        auto width        = image->width;
        auto height       = image->height;

        auto mem      = reinterpret_cast<bm_mem_desc_t*>(frame->GetData());
        auto dev_addr = bm_mem_get_device_addr(*mem);

        bm_status_t attach_result = bm_status_t::BM_SUCCESS;

        if (image_format == bm_image_format_ext::FORMAT_YUV420P) {
            bm_mem_desc_t devs[3];
            devs[0] = bm_mem_from_device(dev_addr, static_cast<unsigned int>(width * height));
            devs[1] = bm_mem_from_device(dev_addr + static_cast<unsigned long long>(width * height),
                                         static_cast<unsigned int>(width * height / 4));
            devs[2] = bm_mem_from_device(dev_addr + static_cast<unsigned long long>(width * height * 5 / 4),
                                         static_cast<unsigned int>(width * height / 4));

            attach_result = bm_image_attach(*image, devs);
        } else if (image_format == bm_image_format_ext::FORMAT_NV12 ||
                   image_format == bm_image_format_ext::FORMAT_NV21) {
            bm_device_mem_t devs[2];
            devs[0] = bm_mem_from_device(dev_addr, static_cast<unsigned int>(width * height));
            devs[1] = bm_mem_from_device(dev_addr + static_cast<unsigned long long>(width * height),
                                         static_cast<unsigned int>(width * height / 2));

            attach_result = bm_image_attach(*image, devs);
        } else if (image_format == bm_image_format_ext::FORMAT_BGR_PACKED ||
                   image_format == bm_image_format_ext::FORMAT_RGB_PACKED) {
            bm_device_mem_t devs[1];
            devs[0] = bm_mem_from_device(dev_addr, static_cast<unsigned int>(width * height * 3));

            attach_result = bm_image_attach(*image, devs);
        } else {
            // unsupported for now
            return false;
        }

        return attach_result == bm_status_t::BM_SUCCESS;
    }

    bool VideoFrameProcSophon::CopyFrameFromDevice(VideoFramePtr frame) {
        if (!VideoFrameValid(frame, true)) {
            return false;
        }

        auto dev_mem = reinterpret_cast<bm_mem_desc_t*>(frame->GetData());

        // Ownership: malloc'd buffer is transferred to VideoFrame via SetHostData().
        // VideoFrame::Clear()/~VideoFrame() calls free(host_frame_data).
        auto host_mem = frame->GetHostData();
        if (!host_mem) {
            host_mem = static_cast<uint8_t*>(malloc(frame->GetSize()));
            if (!host_mem) {
                LOG_ERRO("CopyFrameFromDevice() - host buffer allocation failed, size {}", frame->GetSize());
                return false;
            }
            frame->SetHostData(host_mem);
        }

        auto ret =
            bm_memcpy_d2s_partial(handle, host_mem, *dev_mem, static_cast<unsigned int>(frame->GetSize()));
        if (ret != bm_status_t::BM_SUCCESS) {
            LOG_ERRO("CopyFrameFromDevice() - bm_memcpy_d2s_partial failed:{}", ret);
            return false;
        }

        return true;
    }

    VideoFrameProcSophon::BmFrameImagePtr VideoFrameProcSophon::VideoFrameToBMImage(VideoFramePtr frame) {
        if (!VideoFrameValid(frame, true)) {
            return nullptr;
        }

        auto width        = frame->GetWidth();
        auto height       = frame->GetHeight();
        auto pixel_format = frame->GetPixelFormat();

        auto image = CreateBMImage(width, height, pixel_format);
        if (!image) {
            LOG_WARN("{}", "bm_image_create failed");
            return nullptr;
        }

        if (!VideoFrameAttach(frame, image.get())) {
            LOG_WARN("{}", "bm_image_attach failed");
            return nullptr;
        }

        return image;
    }

    VideoFrameProcSophon::BmFrameImagePtr VideoFrameProcSophon::CreateBMImage(size_t width, size_t height,
                                                                              PixelFormat pf) {
        // RAII: BmFrameImagePtr (unique_ptr + BmFrameImageDeleter) owns this allocation
        auto raw_mem = static_cast<bm_image*>(malloc(sizeof(bm_image)));
        if (!raw_mem) {
            LOG_ERRO("{}", "CreateBMImage() - malloc failed");
            return nullptr;
        }
        BmFrameImagePtr image(raw_mem);

        auto ret = bm_image_create(handle, static_cast<int>(height), static_cast<int>(width),
                                   MapImageFormat(pf), DATA_TYPE_EXT_1N_BYTE, image.get(), nullptr);
        if (ret != bm_status_t::BM_SUCCESS) {
            LOG_WARN("{}", "bm_image_create failed");

            return nullptr;
        }

        return image;
    }

    bm_image_format_ext VideoFrameProcSophon::MapImageFormat(PixelFormat pf) {
        switch (pf) {
            case PixelFormat::PIXEL_RGB8:
                return bm_image_format_ext::FORMAT_RGB_PACKED;
            case PixelFormat::PIXEL_RGBA8:
                return bm_image_format_ext::FORMAT_ARGB_PACKED;

            case PixelFormat::PIXEL_BGR8:
                return bm_image_format_ext::FORMAT_BGR_PACKED;
            case PixelFormat::PIXEL_BGRA8:
                return bm_image_format_ext::FORMAT_ABGR_PACKED;

            case PixelFormat::PIXEL_YUYV:
                return bm_image_format_ext::FORMAT_YUV422_YUYV;
            case PixelFormat::PIXEL_YVYU:
                return bm_image_format_ext::FORMAT_YUV422_YVYU;
            case PixelFormat::PIXEL_UYVY:
                return bm_image_format_ext::FORMAT_YUV422_UYVY;

            case PixelFormat::PIXEL_I420:
                return bm_image_format_ext::FORMAT_YUV420P;

            case PixelFormat::PIXEL_NV12:
                return bm_image_format_ext::FORMAT_NV12;
            case PixelFormat::PIXEL_NV21:
                return bm_image_format_ext::FORMAT_NV21;
            default:
                break;
        }

        return bm_image_format_ext::FORMAT_ABGR1555_PACKED;
    }

}  // namespace media
}  // namespace cosmo