#pragma once

#include <functional>
#include <memory>
#include <string>

#include "media/NativeVideoBuffer.h"
#include "media/VideoCodecType.h"
#include "media/VideoFrame.h"

namespace cosmo {
namespace media {

    struct VideoDecoderCapability {
        bool available{false};
        std::string backend;
        std::string implementation;
        std::string detail;
    };

    /// One decoded output whose host pixels may be materialized later.
    ///
    /// CPU and Sophon decoders return an already-materialized VideoFrame. The
    /// Rockchip decoder can instead retain one MPP frame until the channel has
    /// completed its task/viewer sampling decision. A deferred output must be
    /// materialized or explicitly discarded on the decoder thread before the
    /// decoder is closed.
    class DecodedVideoFrame {
    public:
        using Materializer         = std::function<VideoFramePtr()>;
        using DiscardHandler       = std::function<void()>;
        using NativeBufferExporter = std::function<NativeVideoBufferPtr()>;

        DecodedVideoFrame() = default;
        explicit DecodedVideoFrame(VideoFramePtr frame);
        DecodedVideoFrame(uint64_t frame_index, size_t width, size_t height, PixelFormat format,
                          Materializer materializer, DiscardHandler discard_handler,
                          NativeBufferExporter native_buffer_exporter = {});

        DecodedVideoFrame(const DecodedVideoFrame&)                = delete;
        DecodedVideoFrame& operator=(const DecodedVideoFrame&)     = delete;
        DecodedVideoFrame(DecodedVideoFrame&&) noexcept            = default;
        DecodedVideoFrame& operator=(DecodedVideoFrame&&) noexcept = default;

        [[nodiscard]] bool HasFrame() const;
        [[nodiscard]] bool IsDeferred() const;
        [[nodiscard]] uint64_t GetFrameIndex() const;
        [[nodiscard]] size_t GetWidth() const;
        [[nodiscard]] size_t GetHeight() const;
        [[nodiscard]] PixelFormat GetPixelFormat() const;
        NativeVideoBufferPtr ExportNativeBuffer();

        VideoFramePtr Materialize();
        void Discard();

    private:
        VideoFramePtr frame_;
        uint64_t frame_index_{0};
        size_t width_{0};
        size_t height_{0};
        PixelFormat format_{PixelFormat::PIXEL_UNKNOWN};
        Materializer materializer_;
        DiscardHandler discard_handler_;
        NativeBufferExporter native_buffer_exporter_;
        NativeVideoBufferPtr native_buffer_;
    };

    class VideoDecoder {
    public:
        VideoDecoder(size_t name);

        virtual ~VideoDecoder();

        void SetCodecType(VideoCodecType type, int valWidth, int valHeight);

        virtual bool Open()  = 0;
        virtual bool Close() = 0;

        virtual bool IsOpened() = 0;

        /// Allow a backend to retain a compatible decoder context when a new
        /// stream generation starts at an independently decodable keyframe.
        /// Backends that do not explicitly support this keep the existing
        /// Close/Open lifecycle.
        virtual bool ReuseForStreamRestart(VideoCodecType type, int width, int height) {
            (void)type;
            (void)width;
            (void)height;
            return false;
        }

        VideoFramePtr Decode(const uint8_t* pkt, size_t len, int64_t frame_idx, bool& result);

        /// Decode one packet while preserving a backend-specific delayed
        /// materialization boundary when available.
        DecodedVideoFrame DecodeFrame(const uint8_t* pkt, size_t len, int64_t frame_idx, bool& result);

        virtual bool SendPacket(const uint8_t* pkt, size_t len, int64_t frame_idx) = 0;

        virtual VideoFramePtr GetFrame() = 0;

        /// CPU/Sophon default to GetFrame(); Rockchip overrides this to return
        /// an MPP-backed deferred output.
        virtual DecodedVideoFrame GetDecodedFrame();

        size_t GetWidth() const;

        size_t GetHeight() const;

        /// Factory — creates the correct backend decoder (Sophon or CPU).
        /// @param name       Decoder index / channel ID
        /// @param mediaHandle  Device handle (used by Sophon, ignored by CPU)
        static std::unique_ptr<VideoDecoder> Create(size_t name, void* mediaHandle);

        /// Reports the deterministic implementation selected for this codec.
        static VideoDecoderCapability Probe(VideoCodecType type);

    protected:
        std::string idx_name_;

        VideoCodecType codec_type_{VideoCodecType::kInvalid};

        size_t width_  = 1920;
        size_t height_ = 1080;

        size_t send_pkt_cnt_   = 0;
        size_t recv_frame_cnt_ = 0;
    };

}  // namespace media
}  // namespace cosmo
