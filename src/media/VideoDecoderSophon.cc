// VideoDecoderSophon — Video Decoder Sophon implementation.

#include "media/VideoDecoderSophon.h"

#include <chrono>
#include <thread>

#include "media/PixelFormatUtils.h"
#include "util/Log.h"
#include "util/TimingConstants.h"

// BM1688 VPU hardware decoder's create/delete share underlying hardware resources (VPU core, frame buffer
// pool). Concurrent execution of bmvpu_dec_create and bmvpu_dec_delete may corrupt the active decoder's
// handle, returning BM_ERR_VDEC_ILLEGAL_PARAM. This global lock serializes all VPU lifecycle operations.
static std::mutex g_vpuLifecycleMutex;

namespace cosmo {
namespace media {

    struct BmImageDeleter {
        void operator()(bm_image* p) const {
            if (p) {
                if (bm_image_is_attached(*p)) {
                    bm_image_detach(*p);
                }
                bm_image_destroy(p);
                delete p;
            }
        }
    };

    using BmImagePtr = std::unique_ptr<bm_image, BmImageDeleter>;

    VideoDecoderSophon::VideoDecoderSophon(size_t name, void* mediaHandle) : VideoDecoder(name) {
        bmvpu_dec_set_logging_threshold(BmVpuDecLogLevel::BMVPU_DEC_LOG_LEVEL_ERR);

        bm_handle_ = reinterpret_cast<bm_handle_t>(mediaHandle);
        stop_      = true;
    }

    VideoDecoderSophon::~VideoDecoderSophon() {
        VideoDecoderSophon::Close();
    }

    bool VideoDecoderSophon::IsOpened() {
        return opened_;
    }

    bool VideoDecoderSophon::Open() {
        if (codec_type_ == VideoCodecType::kH265) {
            stream_format_ = BmVpuDecStreamFormat::BMDEC_HEVC;
        } else if (codec_type_ == VideoCodecType::kH264) {
            stream_format_ = BmVpuDecStreamFormat::BMDEC_AVC;
        } else {
            LOG_WARN("{} OpenDecoder But CodecType({}) Not Support", idx_name_, codec_type_);
            return false;
        }

        BMVidDecParam dec_params{};
        dec_params.streamFormat            = stream_format_;
        constexpr size_t kStreamBufferSize = 0x500000;  // 5 MB

        dec_params.extraFrameBufferNum = static_cast<int>(cache_size_);
        dec_params.streamBufferSize    = kStreamBufferSize;
        dec_params.enable_cache        = 1;
        dec_params.bsMode              = BmVpuDecBitStreamMode::BMDEC_BS_MODE_PIC_END;
        dec_params.pcie_board_id       = 0;
        dec_params.cmd_queue_depth     = static_cast<int>(cache_size_);
        dec_params.wtlFormat           = BmVpuDecOutputMapType::BMDEC_OUTPUT_UNMAP;
        dec_params.pixel_format        = BmVpuDecPixFormat::BM_VPU_DEC_PIX_FORMAT_YUV420P;

        int ret;
        {
            std::lock_guard<std::mutex> vpuLock(g_vpuLifecycleMutex);
            ret = bmvpu_dec_create(&code_handle_, dec_params);
        }
        if (ret != BM_SUCCESS) {
            LOG_WARN("{} bmvpu_dec_create failed ret:{}", idx_name_, ret);
            return false;
        }

        frame_  = std::make_unique<BMVidFrame>();
        stop_   = false;
        opened_ = true;
        LOG_INFO("{} VPU decoder opened", idx_name_);
        return true;
    }

    bool VideoDecoderSophon::Close() {
        std::lock_guard<std::mutex> lock(close_mutex_);
        if (!opened_)
            return true;

        stop_ = true;

        std::this_thread::sleep_for(timing::kMediumPollInterval);
        {
            std::lock_guard<std::mutex> vpuLock(g_vpuLifecycleMutex);
            if (code_handle_) {
                bmvpu_dec_delete(code_handle_);
                code_handle_ = nullptr;
            }
        }

        frame_.reset();

        opened_ = false;
        LOG_INFO("{} VPU decoder closed", idx_name_);
        return true;
    }

    bool VideoDecoderSophon::SendPacket(const uint8_t* pkt, size_t len, int64_t frame_idx) {
        if (stop_)
            return false;

        BMVidStream stream;
        // SAFETY: BMVidStream.buf is non-const but bmvpu_dec_decode only reads
        // the packet data. const_cast required at Sophon VPU C API boundary.
        stream.buf            = const_cast<uint8_t*>(pkt);
        stream.length         = static_cast<unsigned int>(len);
        stream.end_of_stream  = (len == 0 || !pkt) ? 1 : 0;
        stream.dts            = static_cast<uint64_t>(frame_idx);
        stream.pts            = 0;
        stream.header_buf     = nullptr;
        stream.header_size    = 0;
        stream.extradata      = nullptr;
        stream.extradata_size = 0;

        BMVidDecRetStatus ret;
        while ((ret = bmvpu_dec_decode(code_handle_, stream)) != BMVidDecRetStatus::BM_ERR_VDEC_SUCCESS) {
            if (ret == BMVidDecRetStatus::BM_ERR_VDEC_ILLEGAL_PARAM) {
                return false;
            }

            std::this_thread::sleep_for(timing::kSpinWaitInterval);
        }

        return true;
    }

    VideoFramePtr VideoDecoderSophon::GetFrame() {
        if (stop_ || !code_handle_)
            return nullptr;

        // cppcheck-suppress knownConditionTrueFalse
        while (!stop_) {
            auto ret = bmvpu_dec_get_output(code_handle_, frame_.get());
            if (ret != BMVidDecRetStatus::BM_ERR_VDEC_SUCCESS) {
                if (ret != BMVidDecRetStatus::BM_ERR_VDEC_BUF_EMPTY) {
                    LOG_WARN("bmvpu_dec_get_output failed: {}", ret);
                }
                return nullptr;
            }

            auto pkt_cnt = bmvpu_dec_get_pkt_in_buf_cnt(code_handle_);
            if (static_cast<size_t>(pkt_cnt) > cache_size_ / 2) {
                LOG_WARN("{}", "throw away a frame");
                bmvpu_dec_clear_output(code_handle_, frame_.get());
                continue;
            }
            break;
        }

        BmImagePtr vpu_frame_image(new bm_image());
        if (!AttachVideoFrame(vpu_frame_image.get(), frame_.get())) {
            bmvpu_dec_clear_output(code_handle_, frame_.get());
            return nullptr;
        }

        width_                    = frame_->width;
        height_                   = frame_->height;
        VideoFramePtr video_frame = std::make_shared<VideoFrame>(width_, height_, PixelFormat::PIXEL_I420);

        BmImagePtr output_frame_image(new bm_image());
        if (!AttachOutputFrame(output_frame_image.get(), frame_.get(), video_frame)) {
            video_frame.reset();
            bmvpu_dec_clear_output(code_handle_, frame_.get());
            return nullptr;
        }

        (void)bmcv_image_storage_convert(bm_handle_, 1, vpu_frame_image.get(), output_frame_image.get());
        bmvpu_dec_clear_output(code_handle_, frame_.get());

        video_frame->SetFrameIndex(frame_->dts);

        return video_frame;
    }

    bool VideoDecoderSophon::AttachVideoFrame(bm_image* image, BMVidFrame* frame) {
        auto status =
            bm_image_create(bm_handle_, static_cast<int>(frame->height), static_cast<int>(frame->width),
                            bm_image_format_ext::FORMAT_YUV420P, DATA_TYPE_EXT_1N_BYTE, image, frame->stride);
        if (status != bm_status_t::BM_SUCCESS) {
            LOG_ERRO("bm_image_create failed {}", status);
            return false;
        }

        auto y_stride = frame->stride[4];
        auto c_stride = frame->stride[5];

        size_t y_size = static_cast<size_t>(y_stride) * frame->height;

        bm_device_mem_t devs[3];
        devs[0] = bm_mem_from_device(reinterpret_cast<unsigned long long>(frame->buf[4]),
                                     static_cast<unsigned int>(y_size));

        size_t c_size = static_cast<size_t>(c_stride) * frame->height / 2;
        devs[1]       = bm_mem_from_device(reinterpret_cast<unsigned long long>(frame->buf[5]),
                                           static_cast<unsigned int>(c_size));
        devs[2]       = bm_mem_from_device(reinterpret_cast<unsigned long long>(frame->buf[6]),
                                           static_cast<unsigned int>(c_size));

        status = bm_image_attach(*image, devs);
        if (status != bm_status_t::BM_SUCCESS) {
            LOG_ERRO("bm_image_attach failed: {}", status);
            return false;
        }

        return true;
    }

    bool VideoDecoderSophon::AttachOutputFrame(bm_image* image, BMVidFrame* frame, VideoFramePtr frame_ptr) {
        auto image_width  = frame->width;
        auto image_height = frame->height;

        auto status =
            bm_image_create(bm_handle_, static_cast<int>(frame->height), static_cast<int>(frame->width),
                            bm_image_format_ext::FORMAT_YUV420P, DATA_TYPE_EXT_1N_BYTE, image, nullptr);
        if (status != bm_status_t::BM_SUCCESS) {
            LOG_ERRO("bm_image_create failed {}", status);
            return false;
        }

        auto frame_device_mem = reinterpret_cast<bm_mem_desc_t*>(frame_ptr->GetData());
        if (!frame_device_mem) {
            LOG_ERRO("{}", "get bm_mem_desc_t failed");
            return false;
        }

        auto addr = bm_mem_get_device_addr(*frame_device_mem);

        bm_device_mem_t devs[3];
        devs[0] = bm_mem_from_device(addr, image_width * image_height);

        auto u_addr = reinterpret_cast<unsigned long long>(reinterpret_cast<uint8_t*>(addr) +
                                                           image_width * image_height);
        devs[1]     = bm_mem_from_device(u_addr, image_width * image_height / 4);

        auto v_addr = reinterpret_cast<unsigned long long>(reinterpret_cast<uint8_t*>(u_addr) +
                                                           image_width * image_height / 4);
        devs[2]     = bm_mem_from_device(v_addr, image_width * image_height / 4);

        status = bm_image_attach(*image, devs);
        if (status != bm_status_t::BM_SUCCESS) {
            LOG_ERRO("bm_image_attach failed {}", status);
            return false;
        }

        return true;
    }

}  // namespace media
}  // namespace cosmo