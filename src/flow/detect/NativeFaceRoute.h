#pragma once
#ifdef COSMO_NN_USE_AXERA_BACKEND
#include <memory>
#include <optional>
#include <utility>

#include "flow/common/AlgDataQueueDistributor.h"
#include "nn/device/axera/axera_face_native_entry.h"
#include "nn/device/axera/axera_fire_native_entry.h"
#include "nn/device/axera/axera_person_native_entry.h"
#include "nn/device/axera/axera_vehicle_native_entry.h"
#include "nn/device/axera/benchmark/person_entry.h"

namespace cosmo {
class AiDetectorUnify;

// Created only from an initialized detector's model capability. Queue and
// detector identity are object identities, never user-facing names or flags.
class NativeFaceRegistration {
    friend class AiDetectorUnify;
    friend class AiDetector;
#ifdef COSMO_AXERA_HELMET_ROUTE_CANDIDATE
    friend class TaskBase;
#endif
    friend class AlgDataQueueDistributor;
    friend class NativeFaceEnvelope;
    using Queue = AlgDataQueue<AlgDataPtr>;
    NativeFaceRegistration(std::shared_ptr<nn::axera::FaceNativeEntry> entry,
                           std::shared_ptr<const void> detector_identity, std::shared_ptr<Queue> queue,
                           std::shared_ptr<AlgDataQueueDistributor> output_distributor)
        : entry_(entry),
          ticket_(entry->GetTicket()),
          detector_identity_(std::move(detector_identity)),
          queue_(queue),
          queue_lifecycle_(queue->Lifecycle()),
          output_distributor_(output_distributor) {}
    NativeFaceRegistration(std::shared_ptr<nn::axera::PersonNativeEntry> entry,
                           std::shared_ptr<const void> detector_identity, std::shared_ptr<Queue> queue,
                           std::shared_ptr<AlgDataQueueDistributor> output_distributor)
        : person_entry_(entry),
          person_ticket_(entry->GetTicket()),
          detector_identity_(std::move(detector_identity)),
          queue_(queue),
          queue_lifecycle_(queue->Lifecycle()),
          output_distributor_(output_distributor) {}
    NativeFaceRegistration(std::shared_ptr<nn::axera::benchmark::PersonEntry> entry,
                           std::shared_ptr<const void> detector_identity, std::shared_ptr<Queue> queue,
                           std::shared_ptr<AlgDataQueueDistributor> output_distributor)
        : bench_entry_(entry),
          bench_ticket_(entry->GetTicket()),
          detector_identity_(std::move(detector_identity)),
          queue_(queue),
          queue_lifecycle_(queue->Lifecycle()),
          output_distributor_(output_distributor) {}
    NativeFaceRegistration(std::shared_ptr<nn::axera::FireNativeEntry> entry,
                           std::shared_ptr<const void> detector_identity, std::shared_ptr<Queue> queue,
                           std::shared_ptr<AlgDataQueueDistributor> output_distributor)
        : fire_entry_(entry),
          fire_ticket_(entry->GetTicket()),
          detector_identity_(std::move(detector_identity)),
          queue_(queue),
          queue_lifecycle_(queue->Lifecycle()),
          output_distributor_(output_distributor) {}
    NativeFaceRegistration(std::shared_ptr<nn::axera::VehicleNativeEntry> entry,
                           std::shared_ptr<const void> detector_identity, std::shared_ptr<Queue> queue,
                           std::shared_ptr<AlgDataQueueDistributor> output_distributor)
        : vehicle_entry_(entry),
          vehicle_ticket_(entry->GetTicket()),
          detector_identity_(std::move(detector_identity)),
          queue_(queue),
          queue_lifecycle_(queue->Lifecycle()),
          output_distributor_(output_distributor) {}
    struct E24 {};
    NativeFaceRegistration(E24, std::shared_ptr<const void> detector_identity, std::shared_ptr<Queue> queue,
                           std::shared_ptr<AlgDataQueueDistributor> output_distributor)
        : e24_(true),
          detector_identity_(std::move(detector_identity)),
          queue_(queue),
          queue_lifecycle_(queue->Lifecycle()),
          output_distributor_(output_distributor) {}
    const bool e24_{false};
    const std::weak_ptr<nn::axera::FaceNativeEntry> entry_;
    const std::optional<nn::axera::FaceNativeEntry::Ticket> ticket_;
    const std::weak_ptr<nn::axera::benchmark::PersonEntry> bench_entry_;
    const std::optional<nn::axera::benchmark::PersonEntry::Ticket> bench_ticket_;
    const std::weak_ptr<nn::axera::PersonNativeEntry> person_entry_;
    const std::optional<nn::axera::PersonNativeEntry::Ticket> person_ticket_;
    const std::weak_ptr<nn::axera::FireNativeEntry> fire_entry_;
    const std::optional<nn::axera::FireNativeEntry::Ticket> fire_ticket_;
    const std::weak_ptr<nn::axera::VehicleNativeEntry> vehicle_entry_;
    const std::optional<nn::axera::VehicleNativeEntry::Ticket> vehicle_ticket_;
    const std::shared_ptr<const void> detector_identity_;
    const std::weak_ptr<Queue> queue_;
    const Queue::LifecycleSnapshot queue_lifecycle_;
    const std::weak_ptr<AlgDataQueueDistributor> output_distributor_;
};

// Minted only by the distributor from a current queue plan. Keeps the exact
// native frame owner, frame identity and detector ticket together through the
// queue; consumers must acquire the authority lease before using any of them.
class NativeFaceEnvelope {
    friend class AlgDataQueueDistributor;
    friend class AiDetectorUnify;
    friend class AiDetector;
#ifdef COSMO_AXERA_FACE_TEST_SEAM
public:
    RegistrationAuthority::Lease TestAcquireInput() const {
        return authority_->Acquire(generation_);
    }
    const AlgFrameDistributionPlan& TestOutputSnapshot() const {
        return output_snapshot_;
    }

private:
#endif
    NativeFaceEnvelope(std::shared_ptr<const NativeFaceRegistration> registration,
                       std::shared_ptr<const RegistrationAuthority> authority, std::uint64_t generation,
                       nn::axera::FaceFrameKey key, std::int64_t stream_index,
                       media::NativeVideoBufferPtr source, AlgFrameDistributionPlan output_snapshot)
        : registration_(std::move(registration)),
          authority_(std::move(authority)),
          generation_(generation),
          key_(std::move(key)),
          stream_index_(stream_index),
          source_(std::move(source)),
          output_snapshot_(std::move(output_snapshot)) {}
    const std::shared_ptr<const NativeFaceRegistration> registration_;
    const std::shared_ptr<const RegistrationAuthority> authority_;
    const std::uint64_t generation_;
    const nn::axera::FaceFrameKey key_;
    const std::int64_t stream_index_;
    const media::NativeVideoBufferPtr source_;
    const AlgFrameDistributionPlan output_snapshot_;
};
}  // namespace cosmo
#endif
