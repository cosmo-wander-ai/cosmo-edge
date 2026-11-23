#ifdef COSMO_NN_USE_AXERA_BACKEND
#include "nn/device/axera/axera_person_native_entry.h"

#include <cmath>
#include <utility>

#include "nn/device/axera/Person720Geometry.h"
#include "nn/device/axera/Person720Scale.h"
#include "nn/device/axera/axera_hs_contract.h"
#include "nn/device/axera/axera_mouse_contract.h"
#include "nn/device/axera/axera_person_native_runner.h"
#include "nn/device/axera/axera_route_identity.h"
#include "nn/device/axera/axera_rubbish_contract.h"
#include "util/UuidUtil.h"
#ifdef COSMO_AXERA_FACE_ISOLATED_DEVICE_TEST
#include "nn/device/axera/axera_face_diagnostics.h"
#endif

#if defined(COSMO_AXERA_PERSON_TEST_SEAM) && defined(COSMO_AXERA_FACE_ISOLATED_DEVICE_TEST)
#error "Device acceptance cannot use the person test seam"
#endif

namespace cosmo::nn::axera {
PersonNativeEntry::PersonNativeEntry(std::shared_ptr<const person_binding::Capability> capability,
                                     std::shared_ptr<const void> detector_identity,
                                     std::uint64_t task_generation, std::string atomic_code,
                                     std::string scene_id, bool trusted_person_template)
    : capability_(std::move(capability)),
      detector_identity_(std::move(detector_identity)),
      identity_(std::make_shared<Identity>()),
      generation_(task_generation),
      atomic_code_(std::move(atomic_code)),
      scene_id_(std::move(scene_id)),
      trusted_person_template_(trusted_person_template),
      owner_thread_(std::this_thread::get_id()) {}
#ifdef COSMO_AXERA_HELMET_ROUTE_CANDIDATE
bool PersonNativeEntry::MatchesHelmetPersonModel() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return active_ && capability_ && capability_->Active() && atomic_code_ == "1001003" &&
           capability_->GetContract().model_sha256 ==
               cosmo::nn::axera::route_identity::Digest("person_720.sha256");
}
#ifdef COSMO_AXERA_HSHELMET_ROUTE_CANDIDATE
bool PersonNativeEntry::MatchesHsHelmetHeadModel() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return active_ && capability_ && capability_->Active() && atomic_code_ == hs_contract::kAtomicCode &&
           capability_->GetContract().model_sha256 == hs_contract::ModelSha256();
}
#endif
#endif
bool PersonNativeEntry::AllowsPerson720Standalone() const {
    return capability_ &&
           person720::IsSelected(atomic_code_, scene_id_, capability_->GetContract().semantic_contract,
                                 capability_->GetContract().model_sha256, trusted_person_template_);
}
PersonNativeEntry::Ticket PersonNativeEntry::GetTicket() const {
    return Ticket(identity_, generation_);
}
void PersonNativeEntry::Invalidate() {
    std::lock_guard<std::mutex> lock(mutex_);
    active_ = false;
}
PersonEntryResult PersonNativeEntry::Execute(const Ticket& ticket, const PersonFrameKey& key,
                                             media::NativeVideoBufferPtr source, float threshold) {
#ifdef COSMO_AXERA_FACE_ISOLATED_DEVICE_TEST
    const diagnostics::FrameKey diagnostic_key{key.channel_id, key.stream_generation, key.frame_index,
                                               key.timestamp};
    auto fail = [&](PersonEntryError e) {
        diagnostics::Trace(diagnostics::Event::Rejected, diagnostic_key, static_cast<std::int64_t>(e));
        return PersonEntryResult{e, {}};
    };
#else
    auto fail = [](PersonEntryError e) { return PersonEntryResult{e, {}}; };
#endif
    std::lock_guard<std::mutex> lock(mutex_);
    if (!capability_)
        return fail(PersonEntryError::Closed);
    const auto& bound_contract = capability_->GetContract();
    const bool person_bound =
        atomic_code_ == "1001003" && bound_contract.semantic_contract ==
                                         "ax650-nv12-1080p-pitch2048-bt709-limited-person-bgr-pad140-nms-v1";
    bool hs_bound = false;
#ifdef COSMO_AXERA_HS_ROUTE_CANDIDATE
    hs_bound =
        hs_contract::IsCandidate(atomic_code_, bound_contract.semantic_contract, bound_contract.model_sha256);
#endif
    bool mouse_bound = false;
#ifdef COSMO_AXERA_MOUSE_ROUTE_CANDIDATE
    mouse_bound = mouse_contract::IsCandidate(atomic_code_, bound_contract.semantic_contract,
                                              bound_contract.model_sha256);
#endif
    bool rubbish_bound = false;
#ifdef COSMO_AXERA_RUBBISH_ROUTE_CANDIDATE
    rubbish_bound = rubbish_contract::IsCandidate(atomic_code_, bound_contract.semantic_contract,
                                                  bound_contract.model_sha256);
#endif
    if (!active_ || !capability_->Active() || !detector_identity_ ||
        !(person_bound || hs_bound || mouse_bound || rubbish_bound) || scene_id_.empty() ||
        scene_id_ != bound_contract.scene_id || !generation_ || owner_thread_ != std::this_thread::get_id())
        return fail(PersonEntryError::Closed);
    if (ticket.id_.lock() != identity_ || ticket.generation_ != generation_)
        return fail(PersonEntryError::InvalidTicket);
    const bool is_person720 =
        source && source->width == 1280 && source->height == 720 && AllowsPerson720Standalone();
    if (!source || !source->Valid() || source->memory_kind != media::NativeVideoMemoryKind::AxeraPhysical ||
        source->frame_index != key.frame_index || key.channel_id.empty() || !key.stream_generation ||
        (!(source->width == 1920 && source->height == 1080) && !is_person720))
        return fail(PersonEntryError::InvalidFrame);
    const auto& contract = capability_->GetContract();
    if (!std::isfinite(threshold) || threshold < contract.graph_confidence || threshold > 1.f ||
        (hs_bound && !hs_contract::IsThreshold(threshold)) ||
        (mouse_bound && !mouse_contract::IsThreshold(threshold)) ||
        (rubbish_bound && !rubbish_contract::IsThreshold(threshold)))
        return fail(PersonEntryError::InvalidThreshold);
    RawYuvPersonResult result;
#ifdef COSMO_AXERA_PERSON_TEST_SEAM
    if (!proof_ || !proof_(key, *source))
        return fail(PersonEntryError::UnverifiedProvenance);
    if (!run_)
        return fail(PersonEntryError::Closed);
    result = run_(capability_, std::move(source), threshold);
#else
    const auto& witness = source->frame_witness;
    if (!witness || !witness->Matches(*source) || witness->Generation() != key.stream_generation ||
        witness->FrameIndex() != key.frame_index || witness->SourceTimestamp() != key.timestamp ||
        source->color_space != media::NativeVideoColorSpace::Bt709 ||
        source->color_range != media::NativeVideoColorRange::Limited)
        return fail(PersonEntryError::UnverifiedProvenance);
#ifdef COSMO_AXERA_FACE_ISOLATED_DEVICE_TEST
    diagnostics::Trace(diagnostics::Event::RunnerStart, diagnostic_key);
#endif
    if (is_person720) {
        auto scaled = person720::Scale(source);
        if (!scaled)
            return fail(PersonEntryError::ExecutionFailed);
        // Retain the scaled owner until the executor releases the global device mutex.
        result = PersonNativeRunner::Run(capability_, scaled, threshold);
    } else {
        result = PersonNativeRunner::Run(capability_, std::move(source), threshold);
    }
#endif
    if (result.error != RawYuvError::None || result.person.error != PersonResultError::None) {
        const bool overflow = result.error == RawYuvError::InvalidResult &&
                              result.person.error == PersonResultError::InvalidTensor &&
                              result.person.tensor_error == detections_v1::Error::Overflow;
#ifdef COSMO_AXERA_FACE_ISOLATED_DEVICE_TEST
        if (overflow)
            diagnostics::Trace(diagnostics::Event::TensorOverflow, diagnostic_key,
                               static_cast<std::int64_t>(contract.result_capacity));
        diagnostics::Trace(diagnostics::Event::RunnerFailure, diagnostic_key,
                           static_cast<std::int64_t>(result.error));
#endif
        return fail(overflow ? (rubbish_bound ? PersonEntryError::RubbishOutputCapacityExceeded
                                : mouse_bound ? PersonEntryError::MouseOutputCapacityExceeded
                                              : (hs_bound ? PersonEntryError::HsOutputCapacityExceeded
                                                          : PersonEntryError::OutputCapacityExceeded))
                             : PersonEntryError::ExecutionFailed);
    }
#ifdef COSMO_AXERA_FACE_ISOLATED_DEVICE_TEST
    diagnostics::Trace(diagnostics::Event::RunnerSuccess, diagnostic_key,
                       static_cast<std::int64_t>(result.person.objects.size()));
#endif
    try {
        std::vector<AiDetectRstEl> targets;
        targets.reserve(result.person.objects.size());
        for (const auto& object : result.person.objects) {
            if (object.infos.size() != 1 || object.infos[0].class_id != 0 ||
                object.infos[0].class_name != (rubbish_bound ? rubbish_contract::kClassLabel
                                               : mouse_bound
                                                   ? mouse_contract::kClassLabel
                                                   : (hs_bound ? hs_contract::kClassLabel : "pedestrian")) ||
                !std::isfinite(object.infos[0].confidence) || object.infos[0].confidence < threshold ||
                object.infos[0].confidence > 1.f || !std::isfinite(object.x1) || !std::isfinite(object.x2) ||
                !std::isfinite(object.y1) || !std::isfinite(object.y2) || object.x1 < 0.f ||
                object.y1 < 0.f || object.x2 > 1920.f || object.y2 > 1080.f || object.x2 <= object.x1 ||
                object.y2 <= object.y1)
                return fail(PersonEntryError::InvalidOutput);
            AiDetectRstEl target;
            person720::Box box{object.x1, object.y1, object.x2, object.y2};
            if (is_person720 && !person720::Map1080To720(box, box))
                return fail(PersonEntryError::InvalidOutput);
            target.box.x      = static_cast<int>(box.x1);
            target.box.y      = static_cast<int>(box.y1);
            target.box.width  = static_cast<int>(box.x2) - target.box.x + 1;
            target.box.height = static_cast<int>(box.y2) - target.box.y + 1;
            target.box &= util::Box(0, 0, is_person720 ? 1280 : 1920, is_person720 ? 720 : 1080);
            if (target.box.width <= 0 || target.box.height <= 0)
                return fail(PersonEntryError::InvalidOutput);
            target.hwRatio = static_cast<float>(target.box.height) / static_cast<float>(target.box.width);
            target.confidence.confidence  = object.infos[0].confidence;
            target.confidence.label       = rubbish_bound ? rubbish_contract::kClassLabel
                                            : mouse_bound ? mouse_contract::kClassLabel
                                                          : (hs_bound ? hs_contract::kClassLabel : "pedestrian");
            target.confidence.atomic_code = atomic_code_;
            target.confidence.source      = AiAnnotationSource::Detection;
            target.classId                = 1;
            target.targetId               = util::GenerateUUID();
            targets.push_back(std::move(target));
        }
        return {PersonEntryError::None, std::move(targets)};
    } catch (...) {
        return fail(PersonEntryError::InvalidOutput);
    }
}
}  // namespace cosmo::nn::axera
#endif
