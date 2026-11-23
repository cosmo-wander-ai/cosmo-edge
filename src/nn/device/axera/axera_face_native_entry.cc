#ifdef COSMO_NN_USE_AXERA_BACKEND
#include "nn/device/axera/axera_face_native_entry.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "nn/device/axera/axera_face_native_runner.h"
#include "util/UuidUtil.h"
#ifdef COSMO_AXERA_FACE_ISOLATED_DEVICE_TEST
#include "nn/device/axera/axera_face_diagnostics.h"
#include "nn/device/axera/axera_face_failure_diagnostics.h"
#endif

#if defined(COSMO_AXERA_FACE_TEST_SEAM) && defined(COSMO_AXERA_FACE_ISOLATED_DEVICE_TEST)
#error "The device acceptance build must use real frame witnesses and the real AX runner"
#endif

namespace cosmo::nn::axera {
FaceNativeEntry::FaceNativeEntry(std::shared_ptr<const face_binding::Capability> capability,
                                 std::shared_ptr<const void> detector_identity, std::uint64_t task_generation,
                                 std::string atomic_code, std::string scene_id)
    : capability_(std::move(capability)),
      detector_identity_(std::move(detector_identity)),
      identity_(std::make_shared<Identity>()),
      generation_(task_generation),
      atomic_code_(std::move(atomic_code)),
      scene_id_(std::move(scene_id)),
      owner_thread_(std::this_thread::get_id()) {}
FaceNativeEntry::Ticket FaceNativeEntry::GetTicket() const {
    return Ticket(identity_, generation_);
}
void FaceNativeEntry::Invalidate() {
    std::lock_guard<std::mutex> lock(mutex_);
    active_ = false;
}
FaceEntryResult FaceNativeEntry::Execute(const Ticket& ticket, const FaceFrameKey& key,
                                         media::NativeVideoBufferPtr source, float threshold) {
#ifdef COSMO_AXERA_FACE_ISOLATED_DEVICE_TEST
    const diagnostics::FrameKey diagnostic_key{key.channel_id, key.stream_generation, key.frame_index,
                                               key.timestamp};
    auto fail = [&](FaceEntryError e) {
        diagnostics::Trace(diagnostics::Event::Rejected, diagnostic_key, static_cast<std::int64_t>(e));
        return FaceEntryResult{e, {}};
    };
#else
    auto fail = [](FaceEntryError e) { return FaceEntryResult{e, {}}; };
#endif
    std::lock_guard<std::mutex> lock(mutex_);
    if (!active_ || !capability_ || !capability_->Active() || !detector_identity_ || atomic_code_.empty() ||
        scene_id_.empty() || scene_id_ != capability_->GetContract().scene_id || !generation_ ||
        owner_thread_ != std::this_thread::get_id())
        return fail(FaceEntryError::Closed);
    if (ticket.id_.lock() != identity_ || ticket.generation_ != generation_)
        return fail(FaceEntryError::InvalidTicket);
    if (!source || !source->Valid() || source->memory_kind != media::NativeVideoMemoryKind::AxeraPhysical ||
        source->frame_index != key.frame_index || key.channel_id.empty() || !key.stream_generation ||
        source->width != 1920 || source->height != 1080)
        return fail(FaceEntryError::InvalidFrame);
    const auto& contract = capability_->GetContract();
    if (!std::isfinite(threshold) || threshold < contract.graph_confidence || threshold > 1.f)
        return fail(FaceEntryError::InvalidThreshold);
    RawYuvResult result;
#ifdef COSMO_AXERA_FACE_TEST_SEAM
    if (!proof_ || !proof_(key, *source))
        return fail(FaceEntryError::UnverifiedProvenance);
    if (!run_)
        return fail(FaceEntryError::Closed);
    result = run_(capability_, std::move(source), threshold);
#else
    // Only a witness minted by the decoder from the matching verified packet
    // may attest a reordered frame. Never infer color from geometry or enums.
    const auto& witness = source->frame_witness;
    if (!witness || !witness->Matches(*source) || witness->Generation() != key.stream_generation ||
        witness->FrameIndex() != key.frame_index || witness->SourceTimestamp() != key.timestamp ||
        source->color_space != media::NativeVideoColorSpace::Bt709 ||
        source->color_range != media::NativeVideoColorRange::Limited)
        return fail(FaceEntryError::UnverifiedProvenance);
#ifdef COSMO_AXERA_FACE_ISOLATED_DEVICE_TEST
    diagnostics::Trace(diagnostics::Event::RunnerStart, diagnostic_key);
#endif
    result = FaceNativeRunner::Run(capability_, std::move(source), threshold);
#endif
    if (result.error != RawYuvError::None || result.face.error != FaceResultError::None) {
#ifdef COSMO_AXERA_FACE_ISOLATED_DEVICE_TEST
        diagnostics::TraceRunnerFailure(diagnostic_key, result, capability_->GetContract().result_capacity);
#endif
        const bool overflow = result.error == RawYuvError::InvalidResult &&
                              result.face.error == FaceResultError::InvalidTensor &&
                              result.face.tensor_error == detections_v1::Error::Overflow;
        return fail(overflow ? FaceEntryError::OutputCapacityExceeded : FaceEntryError::ExecutionFailed);
    }
#ifdef COSMO_AXERA_FACE_ISOLATED_DEVICE_TEST
    diagnostics::Trace(diagnostics::Event::RunnerSuccess, diagnostic_key,
                       static_cast<std::int64_t>(result.face.objects.size()));
#endif
    try {
        std::vector<AiDetectRstEl> targets;
        targets.reserve(result.face.objects.size());
        for (const auto& object : result.face.objects) {
            if (object.infos.size() != 1 || object.infos[0].class_id != 0 ||
                object.infos[0].class_name != "face" || !std::isfinite(object.infos[0].confidence) ||
                object.infos[0].confidence < threshold || object.infos[0].confidence > 1.f ||
                !std::isfinite(object.x1) || !std::isfinite(object.x2) || !std::isfinite(object.y1) ||
                !std::isfinite(object.y2) || object.x1 < 0.f || object.y1 < 0.f || object.x2 > 1920.f ||
                object.y2 > 1080.f || object.x2 <= object.x1 || object.y2 <= object.y1)
                return fail(FaceEntryError::InvalidOutput);
            AiDetectRstEl target;
            target.box.x      = static_cast<int>(object.x1);
            target.box.y      = static_cast<int>(object.y1);
            target.box.width  = static_cast<int>(object.x2) - target.box.x + 1;
            target.box.height = static_cast<int>(object.y2) - target.box.y + 1;
            target.box &= util::Box(0, 0, 1920, 1080);
            if (target.box.width <= 0 || target.box.height <= 0)
                return fail(FaceEntryError::InvalidOutput);
            target.hwRatio = static_cast<float>(target.box.height) / static_cast<float>(target.box.width);
            target.confidence.confidence  = object.infos[0].confidence;
            target.confidence.label       = "face";
            target.confidence.atomic_code = atomic_code_;
            target.confidence.source      = AiAnnotationSource::Detection;
            target.classId                = 1;
            target.targetId               = util::GenerateUUID();
            targets.push_back(std::move(target));
        }
        return {FaceEntryError::None, std::move(targets)};
    } catch (...) {
        return fail(FaceEntryError::InvalidOutput);
    }
}
}  // namespace cosmo::nn::axera
#endif
