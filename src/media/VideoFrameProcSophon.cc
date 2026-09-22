// VideoFrameProcSophon.cc — Color conversion, crop, resize, padding and internal helpers.
// Drawing operations are in VideoFrameDrawing.cc.
// JPEG encode/decode operations are in VideoFrameCodec.cc.

#include "media/VideoFrameProcSophon.h"

#include <limits>
#include <mutex>

#include "bmcv_api_ext.h"
#include "media/MosaicPixels.h"
#include "media/PixelFormatUtils.h"
#include "util/Log.h"

// #define DEBUG_DURATION

namespace cosmo {
namespace media {

    namespace {

        // BM1688 VPSS rejects 8-pixel source/crop dimensions despite the generic VPP API range.
        constexpr size_t kSophonVppMinDimension = 16;
        constexpr size_t kSophonVppMaxDimension = 8192;

    }  // namespace

    VideoFrameProcSophon::VideoFrameProcSophon(void* mediaHandle, IOsdTextRenderer& osdService)
        : handle(reinterpret_cast<bm_handle_t>(mediaHandle)), osd_service_(osdService) {}

    VideoFrameProcSophon::~VideoFrameProcSophon() {}

    VideoFramePtr VideoFrameProcSophon::MosaicCopy(VideoFramePtr src, const std::vector<util::Box>& boxes,
                                                   int strength) {
        if (!IsMosaicFrameValid(src) || strength < 1 || strength > 3 ||
            src->GetSize() > std::numeric_limits<unsigned int>::max()) {
            return nullptr;
        }
        try {
            // Device memory is authoritative. Do not populate or reuse the
            // shared frame's host cache, which can be stale after device drawing.
            std::vector<uint8_t> pixels(src->GetSize());
            auto src_mem = reinterpret_cast<bm_device_mem_t*>(src->GetData());
            if (bm_memcpy_d2s_partial(handle, pixels.data(), *src_mem,
                                      static_cast<unsigned int>(pixels.size())) != BM_SUCCESS ||
                !ApplyMosaicToPixels(pixels.data(), pixels.size(), static_cast<int>(src->GetWidth()),
                                     static_cast<int>(src->GetHeight()), src->GetPixelFormat(), boxes,
                                     strength)) {
                return nullptr;
            }
            auto dst = std::make_shared<VideoFrame>(static_cast<int>(src->GetWidth()),
                                                    static_cast<int>(src->GetHeight()), src->GetPixelFormat(),
                                                    src->GetFrameIndex(), src->GetTimestamp());
            if (!IsMosaicFrameValid(dst)) {
                return nullptr;
            }
            dst->SetStreamIndex(src->GetStreamIndex());
            auto dst_mem = reinterpret_cast<bm_device_mem_t*>(dst->GetData());
            if (bm_memcpy_s2d_partial(handle, *dst_mem, pixels.data(),
                                      static_cast<unsigned int>(pixels.size())) != BM_SUCCESS) {
                return nullptr;
            }
            return dst;
        } catch (...) {
            LOG_ERRO("{}", "MosaicCopy() - private frame processing failed");
            return nullptr;
        }
    }

    // Encoding in VRAM on Sophon
    VideoFramePtr VideoFrameProcSophon::CopyFrame(VideoFramePtr frame) {
        if (!VideoFrameValid(frame, true)) {
            return nullptr;
        }
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

        const auto source_width  = srcPicture->GetWidth();
        const auto source_height = srcPicture->GetHeight();
        if (source_width < kSophonVppMinDimension || source_height < kSophonVppMinDimension ||
            source_width > kSophonVppMaxDimension || source_height > kSophonVppMaxDimension) {
            LOG_ERRO("Crop() - source dimensions outside VPP range: {}x{}", source_width, source_height);
            return nullptr;
        }

        const auto source_size = PixelFormatUtils::CalculateFrameSize(
            static_cast<int>(source_width), static_cast<int>(source_height), srcPicture->GetPixelFormat());
        if (!source_size || *source_size != srcPicture->GetSize()) {
            LOG_ERRO("Crop() - source frame layout invalid: {}x{}, size {}", source_width, source_height,
                     srcPicture->GetSize());
            return nullptr;
        }

        const auto normalized_roi = PixelFormatUtils::NormalizeCropRoi(
            static_cast<int>(source_width), static_cast<int>(source_height), PixelFormat::PIXEL_I420, roi,
            static_cast<int>(kSophonVppMinDimension), static_cast<int>(kSophonVppMaxDimension));
        if (!normalized_roi) {
            LOG_ERRO("Crop() - invalid ROI: frame {}x{}, x {} y {} width {} height {}", source_width,
                     source_height, roi.x, roi.y, roi.width, roi.height);
            return nullptr;
        }
        const auto& effective_roi = *normalized_roi;
        if (effective_roi != roi) {
            LOG_DEBUG(
                "Crop() - normalized ROI from x {} y {} width {} height {} to x {} y {} width {} "
                "height {}",
                roi.x, roi.y, roi.width, roi.height, effective_roi.x, effective_roi.y, effective_roi.width,
                effective_roi.height);
        }

#ifdef DEBUG_DURATION
        auto timpointCreate = std::chrono::high_resolution_clock::now();
#endif

        auto image = CreateBMImage(source_width, source_height, srcPicture->GetPixelFormat());
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

        const int crop_width  = effective_roi.width;
        const int crop_height = effective_roi.height;

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
        rect.start_x = static_cast<unsigned int>(effective_roi.x);
        rect.start_y = static_cast<unsigned int>(effective_roi.y);
        rect.crop_w  = static_cast<unsigned int>(effective_roi.width);
        rect.crop_h  = static_cast<unsigned int>(effective_roi.height);

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
        if (dst_height < static_cast<int>(kSophonVppMinDimension) ||
            dst_width < static_cast<int>(kSophonVppMinDimension) ||
            dst_height > static_cast<int>(kSophonVppMaxDimension) ||
            dst_width > static_cast<int>(kSophonVppMaxDimension)) {
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
        if (src_width < kSophonVppMinDimension || src_height < kSophonVppMinDimension ||
            src_width > kSophonVppMaxDimension || src_height > kSophonVppMaxDimension) {
            LOG_ERRO("Resize() - source dimensions outside VPP range: {}x{}", src_width, src_height);
            return nullptr;
        }

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

        if (left > std::numeric_limits<size_t>::max() - width ||
            right > std::numeric_limits<size_t>::max() - width - left ||
            top > std::numeric_limits<size_t>::max() - height ||
            bottom > std::numeric_limits<size_t>::max() - height - top) {
            LOG_ERRO("{}", "Padding() - dimensions overflow");
            return nullptr;
        }
        size_t new_width  = width + left + right;
        size_t new_height = height + top + bottom;
        if (width < kSophonVppMinDimension || height < kSophonVppMinDimension ||
            new_width < kSophonVppMinDimension || new_height < kSophonVppMinDimension ||
            width > kSophonVppMaxDimension || height > kSophonVppMaxDimension ||
            new_width > kSophonVppMaxDimension || new_height > kSophonVppMaxDimension ||
            left > std::numeric_limits<unsigned int>::max() ||
            top > std::numeric_limits<unsigned int>::max()) {
            LOG_ERRO("Padding() - dimensions outside VPP range: {}x{}", new_width, new_height);
            return nullptr;
        }

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

        const auto image_format = image->image_format;
        const auto width        = image->width;
        const auto height       = image->height;
        const auto frame_size = PixelFormatUtils::CalculateFrameSize(width, height, frame->GetPixelFormat());
        if (!frame_size || *frame_size != frame->GetSize() || width <= 0 || height <= 0 ||
            frame->GetWidth() != static_cast<size_t>(width) ||
            frame->GetHeight() != static_cast<size_t>(height) ||
            MapImageFormat(frame->GetPixelFormat()) != image_format) {
            LOG_ERRO("{}", "VideoFrameAttach() - frame/image layout mismatch");
            return false;
        }

        if (*frame_size > std::numeric_limits<unsigned int>::max()) {
            LOG_ERRO("VideoFrameAttach() - frame size exceeds device descriptor limit: {}", *frame_size);
            return false;
        }

        auto mem      = reinterpret_cast<bm_mem_desc_t*>(frame->GetData());
        auto dev_addr = bm_mem_get_device_addr(*mem);

        bm_status_t attach_result = bm_status_t::BM_SUCCESS;

        if (image_format == bm_image_format_ext::FORMAT_YUV420P) {
            if ((*frame_size % 6) != 0) {
                LOG_ERRO("VideoFrameAttach() - invalid I420 frame size: {}", *frame_size);
                return false;
            }
            const auto chroma_size = *frame_size / 6;
            const auto luma_size   = *frame_size - chroma_size - chroma_size;
            bm_mem_desc_t devs[3];
            devs[0] = bm_mem_from_device(dev_addr, static_cast<unsigned int>(luma_size));
            devs[1] = bm_mem_from_device(dev_addr + static_cast<unsigned long long>(luma_size),
                                         static_cast<unsigned int>(chroma_size));
            devs[2] = bm_mem_from_device(dev_addr + static_cast<unsigned long long>(luma_size + chroma_size),
                                         static_cast<unsigned int>(chroma_size));

            attach_result = bm_image_attach(*image, devs);
        } else if (image_format == bm_image_format_ext::FORMAT_NV12 ||
                   image_format == bm_image_format_ext::FORMAT_NV21) {
            if ((*frame_size % 3) != 0) {
                LOG_ERRO("VideoFrameAttach() - invalid NV12/NV21 frame size: {}", *frame_size);
                return false;
            }
            const auto chroma_size = *frame_size / 3;
            const auto luma_size   = *frame_size - chroma_size;
            bm_device_mem_t devs[2];
            devs[0] = bm_mem_from_device(dev_addr, static_cast<unsigned int>(luma_size));
            devs[1] = bm_mem_from_device(dev_addr + static_cast<unsigned long long>(luma_size),
                                         static_cast<unsigned int>(chroma_size));

            attach_result = bm_image_attach(*image, devs);
        } else if (image_format == bm_image_format_ext::FORMAT_BGR_PACKED ||
                   image_format == bm_image_format_ext::FORMAT_RGB_PACKED) {
            bm_device_mem_t devs[1];
            devs[0] = bm_mem_from_device(dev_addr, static_cast<unsigned int>(*frame_size));

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
        if (width > static_cast<size_t>(std::numeric_limits<int>::max()) ||
            height > static_cast<size_t>(std::numeric_limits<int>::max())) {
            LOG_ERRO("CreateBMImage() - dimensions exceed SDK integer range: {}x{}", width, height);
            return nullptr;
        }

        const auto frame_size =
            PixelFormatUtils::CalculateFrameSize(static_cast<int>(width), static_cast<int>(height), pf);
        if (!frame_size) {
            LOG_ERRO("CreateBMImage() - invalid dimensions or format: {}x{}, format {}", width, height,
                     static_cast<int>(pf));
            return nullptr;
        }

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
