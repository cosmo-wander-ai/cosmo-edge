#ifdef COSMO_NN_USE_AXERA_BACKEND
#include "nn/device/axera/axera_vehicle_native_entry.h"

#include <cmath>
#include <utility>

#include "nn/device/axera/axera_vehicle_native_runner.h"
#include "util/UuidUtil.h"
#ifdef COSMO_AXERA_FACE_ISOLATED_DEVICE_TEST
#include "nn/device/axera/axera_face_diagnostics.h"
#endif

#if defined(COSMO_AXERA_VEHICLE_TEST_SEAM) && defined(COSMO_AXERA_FACE_ISOLATED_DEVICE_TEST)
#error "Device acceptance cannot use the vehicle test seam"
#endif

namespace cosmo::nn::axera {
VehicleNativeEntry::VehicleNativeEntry(std::shared_ptr<const vehicle_binding::Capability> capability,
                                       std::shared_ptr<const void> detector_identity,
                                       std::uint64_t task_generation, std::string atomic_code,
                                       std::string scene_id)
    : capability_(std::move(capability)),
      detector_identity_(std::move(detector_identity)),
      identity_(std::make_shared<Identity>()),
      generation_(task_generation),
      atomic_code_(std::move(atomic_code)),
      scene_id_(std::move(scene_id)),
      owner_thread_(std::this_thread::get_id()) {}
VehicleNativeEntry::Ticket VehicleNativeEntry::GetTicket() const {
    return Ticket(identity_, generation_);
}
void VehicleNativeEntry::Invalidate() {
    std::lock_guard<std::mutex> lock(mutex_);
    active_ = false;
}
VehicleEntryResult VehicleNativeEntry::Execute(const Ticket& ticket, const VehicleFrameKey& key,
                                               media::NativeVideoBufferPtr source,
                                               const std::array<vehicle_binding::ClassPolicy, 3> policies) {
#ifdef COSMO_AXERA_FACE_ISOLATED_DEVICE_TEST
    const diagnostics::FrameKey diagnostic_key{key.channel_id, key.stream_generation, key.frame_index,
                                               key.timestamp};
    auto fail = [&](VehicleEntryError e) {
        diagnostics::Trace(diagnostics::Event::Rejected, diagnostic_key, static_cast<std::int64_t>(e));
        return VehicleEntryResult{e, {}};
    };
#else
    auto fail = [](VehicleEntryError e) { return VehicleEntryResult{e, {}}; };
#endif
    std::lock_guard<std::mutex> lock(mutex_);
    if (!active_ || !capability_ || !capability_->Active() || !detector_identity_ || atomic_code_.empty() ||
        atomic_code_ != capability_->GetContract().atomic_code || scene_id_.empty() ||
        scene_id_ != capability_->GetContract().scene_id || !generation_ ||
        owner_thread_ != std::this_thread::get_id())
        return fail(VehicleEntryError::Closed);
    if (ticket.id_.lock() != identity_ || ticket.generation_ != generation_)
        return fail(VehicleEntryError::InvalidTicket);
    if (!source || !source->Valid() || source->memory_kind != media::NativeVideoMemoryKind::AxeraPhysical ||
        source->frame_index != key.frame_index || key.channel_id.empty() || !key.stream_generation ||
        source->width != 1920 || source->height != 1080)
        return fail(VehicleEntryError::InvalidFrame);
    const auto& contract = capability_->GetContract();
    for (std::size_t i = 0; i < policies.size(); ++i) {
        if (policies[i].label != contract.classes[i].label ||
            policies[i].enabled != contract.classes[i].enabled ||
            !detections_v1::SameBits(policies[i].threshold, contract.classes[i].threshold))
            return fail(VehicleEntryError::InvalidThreshold);
    }
    RawYuvVehicleResult result;
#ifdef COSMO_AXERA_VEHICLE_TEST_SEAM
    if (!proof_ || !proof_(key, *source))
        return fail(VehicleEntryError::UnverifiedProvenance);
    if (!run_)
        return fail(VehicleEntryError::Closed);
    result = run_(capability_, std::move(source));
#else
    const auto& witness = source->frame_witness;
    if (!witness || !witness->Matches(*source) || witness->Generation() != key.stream_generation ||
        witness->FrameIndex() != key.frame_index || witness->SourceTimestamp() != key.timestamp ||
        source->color_space != media::NativeVideoColorSpace::Bt709 ||
        source->color_range != media::NativeVideoColorRange::Limited)
        return fail(VehicleEntryError::UnverifiedProvenance);

#ifdef COSMO_AXERA_FACE_ISOLATED_DEVICE_TEST
    diagnostics::Trace(diagnostics::Event::RunnerStart, diagnostic_key);
#endif
    result = VehicleNativeRunner::Run(capability_, std::move(source));
#endif
    if (result.error != RawYuvError::None || result.vehicle.error != VehicleResultError::None) {
        const bool overflow = result.error == RawYuvError::InvalidResult &&
                              result.vehicle.error == VehicleResultError::InvalidTensor &&
                              result.vehicle.tensor_error == detections_v1::Error::Overflow;
#ifdef COSMO_AXERA_FACE_ISOLATED_DEVICE_TEST
        if (overflow)
            diagnostics::Trace(diagnostics::Event::TensorOverflow, diagnostic_key,
                               static_cast<std::int64_t>(contract.result_capacity));
        diagnostics::Trace(diagnostics::Event::RunnerFailure, diagnostic_key,
                           static_cast<std::int64_t>(result.error));
#endif
        return fail(overflow ? VehicleEntryError::OutputCapacityExceeded
                             : VehicleEntryError::ExecutionFailed);
    }

#ifdef COSMO_AXERA_FACE_ISOLATED_DEVICE_TEST
    if (result.vehicle.diagnostic_raw_valid)
        diagnostics::TraceVehicleRaw(diagnostic_key, result.vehicle.diagnostic_raw_counts,
                                     result.vehicle.diagnostic_tensor_sha256);
    diagnostics::Trace(diagnostics::Event::RunnerSuccess, diagnostic_key,
                       static_cast<std::int64_t>(result.vehicle.objects.size()));
#endif
    try {
        std::vector<AiDetectRstEl> targets;
        targets.reserve(result.vehicle.objects.size());
        for (const auto& object : result.vehicle.objects) {
            if (object.infos.size() != 1 || object.infos[0].class_id < 0 ||
                object.infos[0].class_id >= static_cast<int>(policies.size()))
                return fail(VehicleEntryError::InvalidOutput);
            const auto& policy = policies[static_cast<std::size_t>(object.infos[0].class_id)];
            if (!policy.enabled || object.infos[0].class_name != policy.label ||
                !std::isfinite(object.infos[0].confidence) || object.infos[0].confidence < policy.threshold ||
                object.infos[0].confidence > 1.f || !std::isfinite(object.x1) || !std::isfinite(object.x2) ||
                !std::isfinite(object.y1) || !std::isfinite(object.y2) || object.x1 < 0.f ||
                object.y1 < 0.f || object.x2 > 1920.f || object.y2 > 1080.f || object.x2 <= object.x1 ||
                object.y2 <= object.y1)
                return fail(VehicleEntryError::InvalidOutput);
            AiDetectRstEl target;
            target.box.x      = static_cast<int>(object.x1);
            target.box.y      = static_cast<int>(object.y1);
            target.box.width  = static_cast<int>(object.x2) - target.box.x + 1;
            target.box.height = static_cast<int>(object.y2) - target.box.y + 1;
            target.box &= util::Box(0, 0, 1920, 1080);
            if (target.box.width <= 0 || target.box.height <= 0)
                return fail(VehicleEntryError::InvalidOutput);
            target.hwRatio = static_cast<float>(target.box.height) / static_cast<float>(target.box.width);
            target.confidence.confidence  = object.infos[0].confidence;
            target.confidence.label       = policy.label;
            target.confidence.atomic_code = atomic_code_;
            target.confidence.source      = AiAnnotationSource::Detection;
            target.classId                = object.infos[0].class_id + 1;
            target.targetId               = util::GenerateUUID();
            targets.push_back(std::move(target));
        }
#ifdef COSMO_AXERA_FACE_ISOLATED_DEVICE_TEST
        std::array<std::uint64_t, 3> filtered_counts{};
        for (const auto& target : targets)
            ++filtered_counts[static_cast<std::size_t>(target.classId - 1)];
        diagnostics::TraceVehicleFiltered(diagnostic_key, filtered_counts);
#endif
        return {VehicleEntryError::None, std::move(targets)};
    } catch (...) {
        return fail(VehicleEntryError::InvalidOutput);
    }
}
}  // namespace cosmo::nn::axera
#endif
