// VideoEncoderSophon — Video Encoder Sophon implementation.

#include "media/VideoEncoderSophon.h"

#include <chrono>
#include <thread>

#include "util/Log.h"
#include "util/TimingConstants.h"

namespace cosmo {

namespace media {

    static void LoggingFn(BmVpuEncLogLevel level, char const* file, int const line, char const* fn,
                          const char* format, ...) {
        va_list args;

        char const* lvlstr = "";
        switch (level) {
            case BMVPU_ENC_LOG_LEVEL_ERROR:
                lvlstr = "ERROR";
                break;
            case BMVPU_ENC_LOG_LEVEL_WARNING:
                lvlstr = "WARNING";
                break;
            case BMVPU_ENC_LOG_LEVEL_INFO:
                lvlstr = "INFO";
                break;
            case BMVPU_ENC_LOG_LEVEL_DEBUG:
                lvlstr = "DEBUG";
                break;
            case BMVPU_ENC_LOG_LEVEL_TRACE:
                lvlstr = "TRACE";
                break;
            case BMVPU_ENC_LOG_LEVEL_LOG:
                lvlstr = "LOG";
                break;
            default:
                break;
        }
        fprintf(stderr, "[%zx] %s:%d (%s)   %s: ", pthread_self(), file, line, fn, lvlstr);

        va_start(args, format);
        vfprintf(stderr, format, args);
        va_end(args);

        fprintf(stderr, "\n");
    }

    // VPU SDK callback — signature is fixed by bmvpu_enc API.
    // Memory allocated here is owned by output_frame_.data and freed via:
    //   - Clean()         → free(output_frame_.data)
    //   - SendYUVFrame()  → unique_ptr<uint8_t, free>
    //   - SendEndFrame()  → unique_ptr<uint8_t, free>
    static void* AcquireOutputBuffer(void* context, size_t size, void** acquired_handle) {
        ((void)(context));
        void* mem        = malloc(size);
        *acquired_handle = mem;
        return mem;
    }

    static void FinishOutputBuffer(void* context, void* /*acquired_handle*/) {
        ((void)(context));
    }

    VideoEncoderSophon::VideoEncoderSophon(void* mediaHandle) : VideoEncoder() {
        bmvpu_enc_set_logging_threshold(BMVPU_ENC_LOG_LEVEL_ERROR);
        bmvpu_enc_set_logging_function(LoggingFn);

        handle_ = reinterpret_cast<bm_handle_t>(mediaHandle);
        closed_ = true;

        soc_idx_  = 0;
        core_idx_ = bmvpu_enc_get_core_idx(soc_idx_);
    }

    VideoEncoderSophon::~VideoEncoderSophon() {
        if (encoder_) {
            SendEndFrame();
        }
        Clean();
    }

    bool VideoEncoderSophon::Open() {
        if (codec_type_ == VideoCodecType::kH264) {
            codec_fmt_ = BM_VPU_CODEC_FORMAT_H264;
        } else if (codec_type_ == VideoCodecType::kH265) {
            codec_fmt_ = BM_VPU_CODEC_FORMAT_H265;
        } else {
            LOG_WARN("invalid codec type: {}", codec_type_);
            return false;
        }

        bmvpu_enc_set_default_open_params(&open_params_, codec_fmt_);
        open_params_.pix_format   = BM_VPU_ENC_PIX_FORMAT_YUV420P;
        open_params_.frame_width  = static_cast<uint32_t>(width_);
        open_params_.frame_height = static_cast<uint32_t>(height_);
        open_params_.timebase_num = 1;
        open_params_.timebase_den = 1;
        open_params_.fps_num      = 25;
        open_params_.fps_den      = 1;
        open_params_.bitrate      = 0;
        open_params_.cqp          = 32;
        open_params_.gop_preset   = BMVpuEncGopPreset::BM_VPU_ENC_GOP_PRESET_IPPPP;

        auto ret = bmvpu_enc_load(soc_idx_);
        if (ret != BM_VPU_ENC_RETURN_CODE_OK) {
            LOG_ERRO("Failed to load VPU encoder_: {}", ret);
            return false;
        }

        bmvpu_enc_get_bitstream_buffer_info(&bs_buffer_size_, &bs_buffer_alignment_);
        constexpr unsigned int bs_buffer_arry[12] = {0, 7, 7, 7, 10, 13, 7, 7, 18, 7};
        bs_buffer_size_ = ((bs_buffer_size_ + (4u * 1024u - 1u)) & (~(4u * 1024u - 1u))) *
                          bs_buffer_arry[open_params_.gop_preset];

        bs_dma_buffer_ = std::make_unique<BmVpuEncDMABuffer>();
        ret            = bmvpu_enc_dma_buffer_allocate(core_idx_, bs_dma_buffer_.get(),
                                                       static_cast<unsigned int>(bs_buffer_size_));
        if (ret != 0) {
            LOG_ERRO("bmvpu_enc_dma_buffer_allocate failed {}", ret);
            Clean();
            return false;
        }

        initial_info_ = {};
        ret           = bmvpu_enc_open(&encoder_, &open_params_, bs_dma_buffer_.get(), &initial_info_);
        if (ret != 0) {
            LOG_ERRO("Failed to open VPU encoder_: {}", ret);
            Clean();
            return false;
        }

        num_src_fb_ = static_cast<int>(initial_info_.min_num_src_fb);
        if (num_src_fb_ <= 0) {
            LOG_ERRO("Invalid number of source framebuffers: {}", num_src_fb_);
            Clean();
            return false;
        }

        src_fbs_      = std::make_unique<BmVpuFramebuffer[]>(static_cast<size_t>(num_src_fb_));
        src_dma_bufs_ = std::make_unique<BmVpuEncDMABuffer[]>(static_cast<size_t>(num_src_fb_));
        for (int i = 0; i < num_src_fb_; i++) {
            int src_id       = i;
            auto src_dma_buf = src_dma_bufs_.get() + i;
            ret              = bmvpu_enc_dma_buffer_allocate(core_idx_, src_dma_buf,
                                                             static_cast<unsigned int>(initial_info_.src_fb.size));
            if (ret != 0) {
                LOG_ERRO("Failed to allocate source framebuffer DMA buffer: {}", ret);
                Clean();
                return false;
            }

            auto fb = src_fbs_.get() + i;
            ret     = bmvpu_fill_framebuffer_params(fb, &initial_info_.src_fb, src_dma_buf, src_id, nullptr);
            if (ret != BM_VPU_ENC_RETURN_CODE_OK) {
                LOG_ERRO("Failed to fill source framebuffer parameters: {}", ret);
                Clean();
                return false;
            }

            ret = bmvpu_dma_buffer_map(core_idx_, src_dma_buf,
                                       BM_VPU_ENC_MAPPING_FLAG_READ | BM_VPU_ENC_MAPPING_FLAG_WRITE);
            if (ret != 0) {
                LOG_ERRO("Failed to mmap source framebuffer DMA buffer: {}", ret);
                Clean();
                return false;
            }

            memset(reinterpret_cast<void*>(src_dma_buf->virt_addr), 0, src_dma_buf->size);
            bmvpu_dma_buffer_unmap(core_idx_, src_dma_buf);

            frame_unused_queue_.push(fb);
        }

        memset(&input_frame_, 0, sizeof(BmVpuRawFrame));

        memset(&output_frame_, 0, sizeof(BmVpuEncodedFrame));
        output_frame_.data_size = ENC_PKT_DATA_SIZE;

        enc_params_                       = {};
        enc_params_.acquire_output_buffer = AcquireOutputBuffer;
        enc_params_.finish_output_buffer  = FinishOutputBuffer;
        enc_params_.output_buffer_context = nullptr;
        enc_params_.skip_frame            = 0;

        closed_ = false;

        return true;
    }

    void VideoEncoderSophon::Clean() {
        closed_ = true;

        if (encoder_) {
            bmvpu_enc_close(encoder_);
            encoder_ = nullptr;
        }

        src_fbs_.reset();

        if (src_dma_bufs_) {
            for (int i = 0; i < num_src_fb_; ++i) {
                bmvpu_enc_dma_buffer_deallocate(core_idx_, src_dma_bufs_.get() + i);
            }
            src_dma_bufs_.reset();
        }

        if (bs_dma_buffer_) {
            bmvpu_enc_dma_buffer_deallocate(core_idx_, bs_dma_buffer_.get());
            bs_dma_buffer_.reset();
        }

        if (output_frame_.data) {
            free(output_frame_.data);
            output_frame_.data      = nullptr;
            output_frame_.data_size = 0;
        }

        bmvpu_enc_unload(soc_idx_);
    }

    void VideoEncoderSophon::CollectFrameBuffer(const BmVpuEncodedFrame& frame) {
        for (int j = 0; j < num_src_fb_; j++) {
            BmVpuFramebuffer* fb = src_fbs_.get() + j;
            if (frame.src_idx == fb->myIndex) {
                frame_unused_queue_.push(fb);
            }
        }
    }

    VideoPacketPtr VideoEncoderSophon::SendYUVFrame(void* data) {
        if (closed_) {
            LOG_ERRO("{}", "Encoder closed_");
            return nullptr;
        }

        auto src_fb = GetUnusedFrameBuffer();
        if (!src_fb) {
            LOG_ERRO("{}", "Failed to get unused framebuffer");
            return nullptr;
        }

        if (!first_pkt_received_ && frame_unused_queue_.empty()) {
            first_pkt_received_ = true;
        }

        if (!CopyToDMA(data, src_fb)) {
            LOG_ERRO("{}", "Failed to copy data to DMA buffer");
            frame_unused_queue_.push(src_fb);
            return nullptr;
        }

        input_frame_.framebuffer = src_fb;

        int send_frame_count    = 0;
        size_t get_stream_count = 0;
        int send_frame_ret      = BM_VPU_ENC_RETURN_CODE_OK;
        int get_stream_ret      = BM_VPU_ENC_RETURN_CODE_OK;

        bool reget_stream = true;
        bool resend_frame = true;

        do {
            if (resend_frame) {
                send_frame_ret = bmvpu_enc_send_frame(encoder_, &input_frame_, &enc_params_);
                resend_frame   = false;
                reget_stream   = true;
            }

            if (reget_stream) {
                get_stream_ret = bmvpu_enc_get_stream(encoder_, &output_frame_, &enc_params_);
                reget_stream   = false;
            }

            if (get_stream_ret == BM_VPU_ENC_RETURN_CODE_ENC_END) {
                LOG_INFO("{}", "Encoder end");
                return nullptr;
            }

            if (output_frame_.data && output_frame_.data_size > 0) {
                CollectFrameBuffer(output_frame_);

                VideoPacketPtr pkt = std::make_shared<VideoPacket>();
                std::unique_ptr<uint8_t, decltype(&free)> safe_data(static_cast<uint8_t*>(output_frame_.data),
                                                                    free);
                pkt->data = std::vector<uint8_t>(safe_data.get(), safe_data.get() + output_frame_.data_size);

                output_frame_.data      = nullptr;
                output_frame_.data_size = 0;
                return pkt;
            }

            if (first_pkt_received_ && get_stream_count < GET_STREAM_BUFFER_MAX_COUNT) {
                std::this_thread::sleep_for(timing::kSpinWaitInterval);
                get_stream_count++;
                reget_stream = true;
            }

            if (send_frame_ret == BM_VPU_ENC_RETURN_CODE_RESEND_FRAME) {
                std::this_thread::sleep_for(timing::kEncoderBusyWait);
                send_frame_count++;
                resend_frame = true;
            }

        } while (resend_frame || reget_stream);

        return nullptr;
    }

    void VideoEncoderSophon::SendEndFrame() {
        while (!closed_) {
            input_frame_.framebuffer = nullptr;
            input_frame_.dts         = 0;
            input_frame_.pts         = 0;

            bmvpu_enc_send_frame(encoder_, &input_frame_, &enc_params_);
            auto ret = bmvpu_enc_get_stream(encoder_, &output_frame_, &enc_params_);
            if (ret == BM_VPU_ENC_RETURN_CODE_ENC_END)
                break;

            if (!output_frame_.data || output_frame_.data_size <= 0)
                continue;

            CollectFrameBuffer(output_frame_);
            std::unique_ptr<uint8_t, decltype(&free)> safe_data(static_cast<uint8_t*>(output_frame_.data),
                                                                free);
            output_frame_.data      = nullptr;
            output_frame_.data_size = 0;
        }
    }

    BmVpuFramebuffer* VideoEncoderSophon::GetUnusedFrameBuffer() {
        if (frame_unused_queue_.empty()) {
            LOG_ERRO("{}", "Frame unused queue is empty");
            return nullptr;
        }

        BmVpuFramebuffer* fb = frame_unused_queue_.front();
        if (!fb) {
            LOG_ERRO("{}", "Failed to pop framebuffer from unused queue");
            return nullptr;
        }
        frame_unused_queue_.pop();

        for (int i = 0; i < num_src_fb_; i++) {
            auto src_fb = src_fbs_.get() + i;
            if (src_fb == fb)
                return fb;
        }

        LOG_ERRO("{}", " Framebuffer not found in source framebuffers");
        return nullptr;
    }

    bool VideoEncoderSophon::CopyToDMA(void* data, BmVpuFramebuffer* fb) {
        if (!fb) {
            LOG_ERRO("{}", "CopyToDMA() - Invalid framebuffer");
            return false;
        }

        if (!data) {
            LOG_ERRO("{}", "CopyToDMA() - Invalid block");
            return false;
        }

        size_t frame_width  = open_params_.frame_width;
        size_t frame_height = open_params_.frame_height;

        auto src_mem            = reinterpret_cast<bm_device_mem_t*>(data);
        bm_device_mem_t dst_mem = bm_mem_from_device(fb->dma_buffer->phys_addr, fb->dma_buffer->size);

        size_t src_y_size = frame_width * frame_height;
        if (frame_width == fb->y_stride) {
            bm_memcpy_d2d_byte(handle_, dst_mem, 0, *src_mem, 0, src_y_size);
        } else {
            for (size_t i = 0; i < frame_height; i++) {
                size_t src_offset = i * frame_width;
                size_t dst_offset = i * fb->y_stride;
                bm_memcpy_d2d_byte(handle_, dst_mem, dst_offset, *src_mem, src_offset, frame_width);
            }
        }

        size_t w_c        = (frame_width + 1) / 2;
        size_t h_c        = (frame_height + 1) / 2;
        size_t src_c_size = w_c * h_c;
        if (w_c == fb->cbcr_stride) {
            bm_memcpy_d2d_byte(handle_, dst_mem, fb->cb_offset, *src_mem, src_y_size, src_c_size);
            bm_memcpy_d2d_byte(handle_, dst_mem, fb->cr_offset, *src_mem, src_y_size + src_c_size,
                               src_c_size);
        } else {
            for (size_t i = 0; i < h_c; i++) {
                size_t src_offset = src_y_size + i * w_c;
                size_t dst_offset = fb->cb_offset + i * fb->cbcr_stride;
                bm_memcpy_d2d_byte(handle_, dst_mem, dst_offset, *src_mem, src_offset, w_c);

                src_offset += src_c_size;
                dst_offset = dst_offset - fb->cb_offset + fb->cr_offset;
                bm_memcpy_d2d_byte(handle_, dst_mem, dst_offset, *src_mem, src_offset, w_c);
            }
        }

        return true;
    }

}  // namespace media

}  // namespace cosmo