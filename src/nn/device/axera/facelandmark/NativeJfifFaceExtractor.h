#pragma once
// Explicit restricted enrollment primitive: JFIF bytes + caller-supplied face
// ROI -> native 5-point / 1536-float feature.  No quality claim or threshold.
#include <array>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "FaceTaskExecutor.h"
#include "JpegAdmission.h"
#include "media/VideoDecoderAxera.h"
#include "util/Log.h"
namespace cosmo::helmet_candidate {
enum class JfifExtractStatus { Complete, Rejected, CompletionUnknown };
struct JfifFaceResult {
    JfifExtractStatus status{JfifExtractStatus::Rejected};
    std::array<float, 10> landmarks{};
    std::array<float, 1536> feature{};
};
struct JfifFrameLease final : FrameLeaseOwner {
    std::shared_ptr<media::VideoDecoderAxera> decoder;
    media::NativeVideoBufferPtr frame;
};
struct JfifMapOwner final : MappingOwner {};
inline void QuarantineJfifDecoder(std::shared_ptr<media::VideoDecoderAxera> decoder) noexcept {
    // On an indeterminate decoder operation no owner may release hardware state.
    try {
        static auto* mutex    = new std::mutex;
        static auto* retained = new std::vector<std::shared_ptr<media::VideoDecoderAxera>>;
        std::lock_guard<std::mutex> lock(*mutex);
        retained->push_back(std::move(decoder));
    } catch (...) {
        std::terminate();
    }
}
inline JfifFaceResult ExtractNativeJfifFace(const std::uint8_t* data, std::size_t size,
                                            const std::array<std::int32_t, 4>& roi,
                                            const std::shared_ptr<FaceModelBundle>& bundle) {
    JfifFaceResult out;
    if (!bundle || bundle->profile != FaceInputProfile::Jfif601Full ||
        !face_landmark_candidate::AdmitJpeg601Full(data, size))
        return out;
    if (roi[0] < 0 || roi[1] < 0 || roi[2] > 1920 || roi[3] > 1080 || roi[0] >= roi[2] || roi[1] >= roi[3])
        return out;
    auto decoder = std::make_shared<media::VideoDecoderAxera>(0);
    decoder->SetCodecType(media::VideoCodecType::kMjpeg, 1920, 1080);
    decoder->RequireNativeFrames();
    if (!decoder->Open()) {
        LOG_WARN("Native JFIF decoder open rejected");
        return out;
    }
    if (!decoder->SendPacket(data, size, 1)) {
        LOG_WARN("Native JFIF compressed packet rejected");
        QuarantineJfifDecoder(std::move(decoder));
        out.status = JfifExtractStatus::CompletionUnknown;
        return out;
    }
    media::NativeVideoBufferPtr frame;
    try {
        for (int n = 0; n < 20 && !frame; ++n)
            frame = decoder->GetNativeFrame();
    } catch (const std::exception& e) {
        LOG_WARN("Native JFIF frame retrieval failed: {}", e.what());
        QuarantineJfifDecoder(std::move(decoder));
        out.status = JfifExtractStatus::CompletionUnknown;
        return out;
    }
    if (!frame || !frame->frame_witness || !frame->frame_witness->Matches(*frame)) {
        LOG_WARN("Native JFIF frame or witness unavailable");
        QuarantineJfifDecoder(std::move(decoder));
        out.status = JfifExtractStatus::CompletionUnknown;
        return out;
    }
    auto task_id = std::make_shared<int>(1), model_id = std::make_shared<int>(1);
    auto task_gate  = LifecycleGate<TaskTag>::Create(task_id, 1);
    auto model_gate = LifecycleGate<ModelTag>::Create(model_id, 1);
    auto tp         = task_gate->Acquire(task_id, 1);
    auto mp         = model_gate->Acquire(model_id, 1);
    if (!tp || !mp) {
        QuarantineJfifDecoder(std::move(decoder));
        out.status = JfifExtractStatus::CompletionUnknown;
        return out;
    }
    auto lease             = std::make_shared<JfifFrameLease>();
    lease->decoder         = decoder;
    lease->frame           = frame;
    auto pending           = std::make_unique<PendingResources>();
    pending->frame         = frame;
    pending->frame_lease   = lease;
    pending->mapping_owner = std::make_shared<JfifMapOwner>();
    pending->sdk           = std::make_shared<HelmetSystemSdk>();
    pending->owner_thread  = std::this_thread::get_id();
    pending->task_permit   = std::make_shared<ManagedPermit<TaskTag>>(std::move(*tp));
    pending->model_permit  = std::make_shared<ManagedPermit<ModelTag>>(std::move(*mp));
    auto result            = ExecuteFaceTask(std::move(pending), bundle, {roi});
    LOG_INFO("Native JFIF face chain status={} landmarks={} features={}", static_cast<int>(result.result),
             result.landmarks.size(), result.features.size());
    if (result.result == GuardResult::CompletionUnknown || result.result == GuardResult::Poisoned) {
        out.status = JfifExtractStatus::CompletionUnknown;
        return out;
    }
    if (result.result == GuardResult::Complete && result.landmarks.size() == 1 &&
        result.features.size() == 1 && result.task_permit && result.model_permit) {
        bool committed            = false;
        const bool task_committed = result.task_permit->permit.CommitIfCurrent([&]() noexcept {
            committed = result.model_permit->permit.CommitIfCurrent([&]() noexcept {
                out.landmarks = result.landmarks[0];
                out.feature   = result.features[0];
            });
        });
        if (task_committed && committed)
            out.status = JfifExtractStatus::Complete;
    }
    frame.reset();
    lease->frame.reset();
    lease->decoder.reset();
    if (!decoder->PrepareNativeInput() || !decoder->Close()) {
        LOG_WARN("Native JFIF decoder cleanup failed");
        QuarantineJfifDecoder(std::move(decoder));
        out.status    = JfifExtractStatus::CompletionUnknown;
        out.landmarks = {};
        out.feature   = {};
    }
    return out;
}
}  // namespace cosmo::helmet_candidate
