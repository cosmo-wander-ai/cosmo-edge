// AlgChannelDecodeCapture.cc — Image capture and viewer frame distribution for AlgChannelDecode.
// Split from AlgChannelDecode.cc to reduce file size (DEBT-007).

#include <filesystem>

#include "flow/channel/AlgChannelDecode.h"
#include "service/detail/ServiceRegistry.h"
#include "service/media/IVideoFrameCodec.h"
#include "util/FileUtil.h"
#include "util/Log.h"
#include "util/PathUtil.h"

namespace cosmo {

void AlgChannelDecode::DoCaptureImage(VideoFramePtr in_data) {
    if (!in_data) {
        return;
    }
    if (!in_data->Active()) {
        return;
    }
    std::lock_guard<std::mutex> lock(mtx_);
    if (is_capturing_) {
        cap_data_     = in_data;
        is_capturing_ = false;
        cap_cond_.notify_all();
    }
}

VideoFramePtr AlgChannelDecode::CaptureImage(int timeout_ms) {
    auto timeout = std::chrono::milliseconds(timeout_ms);
    std::unique_lock<std::mutex> lock(mtx_);
    is_capturing_ = true;
    cap_cond_.wait_for(lock, timeout, [this] { return cap_data_ != nullptr; });
    if (cap_data_) {
        auto res = cap_data_;
        cap_data_.reset();
        return res;
    }

    return nullptr;
}

void AlgChannelDecode::CaptureJpeg(VideoFramePtr picture) {
    if (picture->GetStreamIndex() != cap_image_stream_index_) {
        auto picture_jpeg =
            service::ServiceRegistry::Instance().Get<service::IVideoFrameCodec>().EncodeJpeg(picture);
        if (picture_jpeg.empty()) {
            return;
        }

        // Named after the channel.
        auto file_channel_name = channel_id_ + ".jpg";
        auto file_channel_path_name =
            (std::filesystem::path(cosmo::path::GetWebLocalPath()) / file_channel_name).string();
        util::WriteFile(file_channel_path_name, reinterpret_cast<const std::uint8_t*>(picture_jpeg.data()),
                        static_cast<int>(picture_jpeg.size()));

        cap_image_stream_index_ = picture->GetStreamIndex();
    }
}

void AlgChannelDecode::AddViewerFrameQueue(const std::string& alg_id,
                                           AsyncQueue<VideoFramePtr>& async_frame_queue) {
    std::lock_guard<std::mutex> lock(mtx_);
    auto it = std::find_if(viewer_queue_.begin(), viewer_queue_.end(),
                           [&](const ChannelTaskViewerQueue& viewer) { return (viewer.alg_id == alg_id); });
    if (it != viewer_queue_.end()) {
        return;
    }
    ChannelTaskViewerQueue viewer;
    viewer.alg_id            = alg_id;
    viewer.async_frame_queue = &async_frame_queue;
    viewer_queue_.push_back(viewer);
}

void AlgChannelDecode::RemoveViewerFrameQueue(const std::string& alg_id) {
    std::lock_guard<std::mutex> lock(mtx_);
    auto it = std::find_if(viewer_queue_.begin(), viewer_queue_.end(),
                           [&](const ChannelTaskViewerQueue& viewer) { return (viewer.alg_id == alg_id); });
    if (it != viewer_queue_.end()) {
        viewer_queue_.erase(it);
    }
    return;
}

void AlgChannelDecode::DistributeViewer(VideoFramePtr in_data) {
    std::lock_guard<std::mutex> lock(mtx_);
    for (auto& viewer : viewer_queue_) {
        if (viewer.async_frame_queue) {
            viewer.async_frame_queue->Insert(in_data);
        }
    }
}

}  // namespace cosmo
