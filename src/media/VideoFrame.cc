// VideoFrame — Legacy global alias — kept for backward compatibility.

#include "media/VideoFrame.h"

#include <atomic>
#include <exception>
#include <mutex>
#include <thread>

#include "media/PixelFormatUtils.h"
#include "mem/MemoryPoolMng.h"
#include "util/Log.h"
#include "util/TimingConstants.h"
#include "util/UuidUtil.h"

namespace cosmo {
namespace media {

    struct VideoFrame::DeferredData {
        explicit DeferredData(Materializer provider) : materializer(std::move(provider)) {}

        std::once_flag once;
        Materializer materializer;
        std::shared_ptr<VideoFrame> frame;
        std::atomic<bool> completed{false};
        std::atomic<bool> failed{false};
    };

    VideoFrame::VideoFrame(int w, int h, PixelFormat fmt, uint64_t frameIndex, int64_t ts) {
        pixel_fmt_            = fmt;
        const auto frame_size = PixelFormatUtils::CalculateFrameSize(w, h, fmt);
        if (!frame_size) {
            LOG_WARN("Invalid frame dimensions or format: {}x{}, format {}", w, h, static_cast<int>(fmt));
            return;
        }

        width_   = static_cast<size_t>(w);
        height_  = static_cast<size_t>(h);
        channel_ = PixelFormatUtils::PixelFormatChannels(fmt);
        size_    = *frame_size;

        int index = 0;
        while (index++ <= 10) {
            auto block__ = mem::GetMemoryPool().Acquire(size_);
            if (block__) {
                block_ = block__;
                break;
            }

            std::this_thread::sleep_for(timing::kFastPollInterval);
        }

        if (!block_) {
            LOG_WARN("Malloc {} Failed", size_);
            return;
        }

        frame_index_ = frameIndex;
        timestamp_   = ts;
        uuid_        = util::GenerateUUID();
        active_      = true;
    }

    VideoFrame::VideoFrame(int w, int h, PixelFormat fmt, uint64_t frameIndex, int64_t ts,
                           Materializer materializer) {
        const auto frame_size = PixelFormatUtils::CalculateFrameSize(w, h, fmt);
        if (!frame_size || !materializer)
            return;
        pixel_fmt_     = fmt;
        width_         = static_cast<size_t>(w);
        height_        = static_cast<size_t>(h);
        channel_       = PixelFormatUtils::PixelFormatChannels(fmt);
        size_          = *frame_size;
        frame_index_   = frameIndex;
        timestamp_     = ts;
        uuid_          = util::GenerateUUID();
        deferred_data_ = std::make_shared<DeferredData>(std::move(materializer));
        active_        = true;
    }

    VideoFrame& VideoFrame::operator=(VideoFrame&& data) noexcept {
        if (&data != this) {
            if (block_) {
                mem::GetMemoryPool().Recycle(block_);
                block_ = nullptr;
            }
            Clear();
            size_         = data.GetSize();
            width_        = data.GetWidth();
            height_       = data.GetHeight();
            channel_      = data.GetChannel();
            pixel_fmt_    = data.GetPixelFormat();
            frame_index_  = data.GetFrameIndex();
            timestamp_    = data.GetTimestamp();
            stream_index_ = data.GetStreamIndex();
            active_       = data.Active();
            uuid_         = util::GenerateUUID();

            block_           = data.block_;
            host_frame_data_ = data.host_frame_data_;
            deferred_data_   = std::move(data.deferred_data_);

            data.block_           = nullptr;
            data.host_frame_data_ = nullptr;

            data.Clear();
        }
        return *this;
    }

    void VideoFrame::Clear() {
        active_      = false;
        size_        = 0;
        width_       = 0;
        height_      = 0;
        frame_index_ = 0;
        timestamp_   = 0;
        deferred_data_.reset();

        if (host_frame_data_) {
            free(host_frame_data_);
            host_frame_data_ = nullptr;
        }
    }

    VideoFrame::~VideoFrame() {
        if (block_) {
            mem::GetMemoryPool().Recycle(block_);
            block_ = nullptr;
        }

        Clear();
    }

    size_t VideoFrame::GetSize() const {
        return size_;
    }

    size_t VideoFrame::GetWidth() const {
        return width_;
    }

    void VideoFrame::SetWidth(size_t w) {
        width_ = w;
    }

    size_t VideoFrame::GetHeight() const {
        return height_;
    }

    void VideoFrame::SetHeight(size_t h) {
        height_ = h;
    }

    PixelFormat VideoFrame::GetPixelFormat() const {
        return pixel_fmt_;
    }

    void VideoFrame::SetFramePixelFormat(PixelFormat fmt) {
        pixel_fmt_ = fmt;
    }

    size_t VideoFrame::GetChannel() const {
        return channel_;
    }

    uint64_t VideoFrame::GetFrameIndex() const {
        return frame_index_;
    }

    void VideoFrame::SetFrameIndex(uint64_t sequence) {
        frame_index_ = sequence;
    }

    int64_t VideoFrame::GetTimestamp() const {
        return timestamp_;
    }

    void VideoFrame::SetTimestamp(int64_t t) {
        timestamp_ = t;
    }

    int64_t VideoFrame::GetStreamIndex() const {
        return stream_index_;
    }

    void VideoFrame::SetStreamIndex(int64_t streamIndex) {
        stream_index_ = streamIndex;
    }

    bool VideoFrame::Active() const {
        return active_ && (!deferred_data_ || !deferred_data_->failed.load(std::memory_order_acquire));
    }

    bool VideoFrame::IsDeferred() const {
        return deferred_data_ && !deferred_data_->completed.load(std::memory_order_acquire);
    }

    std::string VideoFrame::GetName() const {
        return uuid_;
    }

    uint8_t* VideoFrame::GetHostData() {
        return host_frame_data_;
    }

    void VideoFrame::SetHostData(uint8_t* data) {
        host_frame_data_ = data;
    }

    uint8_t* VideoFrame::GetData() {
        if (deferred_data_) {
            std::call_once(deferred_data_->once, [this]() {
                // Consume the provider even on failure, so parallel consumers
                // see the same result and never repeat a failed conversion.
                auto materializer = std::move(deferred_data_->materializer);
                try {
                    auto frame = materializer();
                    if (frame && frame.get() != this && frame->Active() && frame->GetWidth() == width_ &&
                        frame->GetHeight() == height_ && frame->GetPixelFormat() == pixel_fmt_ &&
                        frame->GetData()) {
                        deferred_data_->frame = std::move(frame);
                    }
                } catch (const std::exception& error) {
                    LOG_WARN("Deferred frame materialization failed: {}", error.what());
                } catch (...) {
                    LOG_WARN("{}", "Deferred frame materialization failed");
                }
                deferred_data_->failed.store(!deferred_data_->frame, std::memory_order_release);
                deferred_data_->completed.store(true, std::memory_order_release);
            });
            return deferred_data_->frame ? deferred_data_->frame->GetData() : nullptr;
        }
        if (!block_)
            return nullptr;

        return block_->data;
    }

}  // namespace media
}  // namespace cosmo

bool VideoFrameValid(VideoFramePtr frame, bool blog) {
    if (!frame) {
        if (blog)
            LOG_WARN("{}", "Frame Invalid");
        return false;
    }

    // Pixel consumers already use this validation boundary before accessing
    // data. Metadata-only inference admission uses Active() and can remain lazy.
    if (frame->Active() && frame->GetData()) {
        return true;
    }

    if (blog)
        LOG_WARN("Frame Data Invalid {}x{}", frame->GetWidth(), frame->GetHeight());
    return false;
}
