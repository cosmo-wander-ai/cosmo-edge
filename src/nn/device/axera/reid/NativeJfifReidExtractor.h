#pragma once
// Candidate enrollment primitive. Caller supplies a bounded whole-person ROI.
// No identity or workwear threshold is implied; unsupported JPEGs are rejected.
#include "ReidModelBundle.h"
#include "nn/device/axera/facelandmark/NativeJfifFaceExtractor.h"
namespace cosmo::helmet_candidate {
struct JfifReidResult {
    JfifExtractStatus status{JfifExtractStatus::Rejected};
    std::array<float, 1024> feature{};
};
inline JfifReidResult ExtractNativeJfifReid(const std::uint8_t* data, std::size_t size,
                                            const std::array<std::int32_t, 4>& roi,
                                            const std::shared_ptr<ReidVerifiedBundle>& bundle) {
    JfifReidResult out;
    if (!bundle || bundle->Profile() != ReidInputProfile::Jfif601Full ||
        !face_landmark_candidate::AdmitJpeg601Full(data, size))
        return out;
    if (roi[0] < 0 || roi[1] < 0 || roi[2] > 1920 || roi[3] > 1080 || roi[0] >= roi[2] || roi[3] - roi[1] < 2)
        return out;
    auto decoder = std::make_shared<media::VideoDecoderAxera>(0);
    decoder->SetCodecType(media::VideoCodecType::kMjpeg, 1920, 1080);
    decoder->RequireNativeFrames();
    if (!decoder->Open())
        return out;
    if (!decoder->SendPacket(data, size, 1)) {
        LOG_WARN("Native JFIF ReID packet submission failed");
        QuarantineJfifDecoder(std::move(decoder));
        out.status = JfifExtractStatus::CompletionUnknown;
        return out;
    }
    media::NativeVideoBufferPtr frame;
    try {
        for (int n = 0; n < 20 && !frame; ++n)
            frame = decoder->GetNativeFrame();
    } catch (...) {
        QuarantineJfifDecoder(std::move(decoder));
        out.status = JfifExtractStatus::CompletionUnknown;
        return out;
    }
    if (!frame || !frame->frame_witness || !frame->frame_witness->Matches(*frame)) {
        LOG_WARN("Native JFIF ReID frame witness unavailable");
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
    pending->model         = bundle;
    pending->handle        = bundle->HandleForOwnerThread();
    auto result            = ExecuteReidPair(std::move(pending), roi);
    LOG_INFO("Native JFIF ReID result status={} feature_count={}", static_cast<int>(result.result),
             result.features.size());
    if (result.result == GuardResult::CompletionUnknown || result.result == GuardResult::Poisoned) {
        out.status = JfifExtractStatus::CompletionUnknown;
        return out;
    }
    if (result.result == GuardResult::Complete && result.task_permit && result.model_permit) {
        bool committed = false;
        const bool ok  = result.task_permit->permit.CommitIfCurrent([&]() noexcept {
            committed =
                result.model_permit->permit.CommitIfCurrent([&]() noexcept { out.feature = result.feature; });
        });
        if (ok && committed)
            out.status = JfifExtractStatus::Complete;
    }
    frame.reset();
    lease->frame.reset();
    lease->decoder.reset();
    if (!decoder->PrepareNativeInput() || !decoder->Close()) {
        QuarantineJfifDecoder(std::move(decoder));
        out.status  = JfifExtractStatus::CompletionUnknown;
        out.feature = {};
    }
    return out;
}
}  // namespace cosmo::helmet_candidate
