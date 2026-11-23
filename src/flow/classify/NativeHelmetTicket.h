#pragma once
// Disabled private graph experiment. No production mint/route admission exists.
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include "flow/classify/HelmetFramePermit.h"
#include "util/HelmetLifecycleGate.h"
namespace cosmo {
class AiDetector;
class TaskBase;
class NativeHelmetAdmission;
struct NativeHelmetHop {
    std::weak_ptr<const void> queue;
    std::uint64_t generation{};
    std::string task;
};
class NativeHelmetTicket final : public helmet_candidate::FrameLeaseOwner {
public:
    NativeHelmetTicket(const NativeHelmetTicket&)            = delete;
    NativeHelmetTicket& operator=(const NativeHelmetTicket&) = delete;
    bool Forward(const std::shared_ptr<const void>& q, std::uint64_t generation,
                 const std::string& task) noexcept {
        if (hop_ >= hops_.size() || !q || !generation)
            return false;
        const auto bound = hops_[hop_].queue.lock();
        if (!bound || bound.get() != q.get() || bound.owner_before(q) || q.owner_before(bound) ||
            hops_[hop_].generation != generation || hops_[hop_].task != task)
            return false;
        ++hop_;
        return true;
    }
    bool ReadyForClassifier() const noexcept {
        return hop_ == hops_.size();
    }
    media::NativeVideoBufferPtr Claim() {
        auto frame = ReadyForClassifier() ? permit_.Claim(identity_) : media::NativeVideoBufferPtr{};
        if (frame)
            claimed_ = true;
        return frame;
    }
    std::shared_ptr<helmet_candidate::ManagedPermit<helmet_candidate::TaskTag>> TakeTaskPermit() noexcept {
        return claimed_ ? std::move(task_permit_) : nullptr;
    }
    std::shared_ptr<helmet_candidate::ManagedPermit<helmet_candidate::ModelTag>> TakeModelPermit() noexcept {
        return claimed_ ? std::move(model_permit_) : nullptr;
    }
    const std::string& Channel() const noexcept {
        return channel_;
    }
#ifdef COSMO_AXERA_PHONE_ROUTE_CANDIDATE
    const helmet_candidate::RouteIdentity& RouteIdentity() const noexcept {
        return identity_;
    }
    const std::string& TaskId() const noexcept {
        return hops_[0].task;
    }
#endif
#ifdef COSMO_AXERA_HELMET_TEST_SEAM
    static std::unique_ptr<NativeHelmetTicket> TestOnly(
        std::string channel, std::array<NativeHelmetHop, 3> hops,
        helmet_candidate::FramePermitGate::Permit permit, helmet_candidate::RouteIdentity identity,
        std::shared_ptr<helmet_candidate::ManagedPermit<helmet_candidate::TaskTag>> task,
        std::shared_ptr<helmet_candidate::ManagedPermit<helmet_candidate::ModelTag>> model) {
        return std::unique_ptr<NativeHelmetTicket>(new NativeHelmetTicket(std::move(channel), std::move(hops),
                                                                          std::move(permit), identity,
                                                                          std::move(task), std::move(model)));
    }
#endif
private:
    friend class AiDetector;
    friend class NativeHelmetAdmission;
    NativeHelmetTicket(std::string channel, std::array<NativeHelmetHop, 3> hops,
                       helmet_candidate::FramePermitGate::Permit permit,
                       helmet_candidate::RouteIdentity identity,
                       std::shared_ptr<helmet_candidate::ManagedPermit<helmet_candidate::TaskTag>> task,
                       std::shared_ptr<helmet_candidate::ManagedPermit<helmet_candidate::ModelTag>> model)
        : channel_(std::move(channel)),
          hops_(std::move(hops)),
          permit_(std::move(permit)),
          identity_(identity),
          task_permit_(std::move(task)),
          model_permit_(std::move(model)) {}
    std::string channel_;
    std::array<NativeHelmetHop, 3> hops_;
    helmet_candidate::FramePermitGate::Permit permit_;
    helmet_candidate::RouteIdentity identity_;
    std::shared_ptr<helmet_candidate::ManagedPermit<helmet_candidate::TaskTag>> task_permit_;
    std::shared_ptr<helmet_candidate::ManagedPermit<helmet_candidate::ModelTag>> model_permit_;
    std::size_t hop_{};
    bool claimed_{false};
};
// Only TaskBase may eventually construct a plan after validating the entire
// exclusive graph, decoder association, task/config generation and trusted
// classifier binding. No plan factory or installation is enabled today.
class NativeHelmetAdmission final {
#ifdef COSMO_AXERA_HELMET_TEST_SEAM
public:
    static std::shared_ptr<NativeHelmetAdmission> TestCreate(
        std::string channel, std::string task, std::array<NativeHelmetHop, 3> hops,
        std::shared_ptr<helmet_candidate::FramePermitGate> gate, helmet_candidate::RouteIdentity identity,
        std::shared_ptr<helmet_candidate::LifecycleGate<helmet_candidate::TaskTag>> task_gate,
        std::shared_ptr<helmet_candidate::LifecycleGate<helmet_candidate::ModelTag>> model_gate,
        std::shared_ptr<const void> task_identity, std::shared_ptr<const void> model_identity,
        std::uint64_t task_generation, std::uint64_t model_generation) {
        return std::shared_ptr<NativeHelmetAdmission>(new NativeHelmetAdmission(
            std::move(channel), std::move(task), std::move(hops), std::move(gate), identity,
            std::move(task_gate), std::move(model_gate), std::move(task_identity), std::move(model_identity),
            task_generation, model_generation, std::make_shared<int>(1)));
    }
    std::unique_ptr<NativeHelmetTicket> TestMint(const std::string& task,
                                                 media::NativeVideoBufferPtr frame) const {
        return Mint(task, std::move(frame));
    }

private:
#endif
    friend class TaskBase;
    friend class AiDetector;
    NativeHelmetAdmission(
        std::string channel, std::string task, std::array<NativeHelmetHop, 3> hops,
        std::shared_ptr<helmet_candidate::FramePermitGate> gate, helmet_candidate::RouteIdentity identity,
        std::shared_ptr<helmet_candidate::LifecycleGate<helmet_candidate::TaskTag>> task_gate,
        std::shared_ptr<helmet_candidate::LifecycleGate<helmet_candidate::ModelTag>> model_gate,
        std::shared_ptr<const void> task_identity, std::shared_ptr<const void> model_identity,
        std::uint64_t task_generation, std::uint64_t model_generation,
        std::shared_ptr<const void> detector_registration)
        : channel_(std::move(channel)),
          task_(std::move(task)),
          hops_(std::move(hops)),
          gate_(std::move(gate)),
          identity_(identity),
          task_gate_(std::move(task_gate)),
          model_gate_(std::move(model_gate)),
          task_identity_(std::move(task_identity)),
          model_identity_(std::move(model_identity)),
          task_generation_(task_generation),
          model_generation_(model_generation),
          detector_registration_(std::move(detector_registration)) {}
    std::unique_ptr<NativeHelmetTicket> Mint(const std::string& task,
                                             media::NativeVideoBufferPtr frame) const {
        try {
            if (task != task_ || channel_.empty() || !gate_ || !task_gate_ || !model_gate_)
                return {};
            for (const auto& h : hops_)
                if (h.queue.expired() || !h.generation || h.task != task_)
                    return {};
            auto task_lease  = task_gate_->Acquire(task_identity_, task_generation_);
            auto model_lease = model_gate_->Acquire(model_identity_, model_generation_);
            if (!task_lease || !model_lease)
                return {};
            auto task_owner = std::make_shared<helmet_candidate::ManagedPermit<helmet_candidate::TaskTag>>(
                std::move(*task_lease));
            auto model_owner = std::make_shared<helmet_candidate::ManagedPermit<helmet_candidate::ModelTag>>(
                std::move(*model_lease));
            auto permit = gate_->Reserve(identity_, std::move(frame));
            if (!permit.IsReserved(identity_))
                return {};
            return std::unique_ptr<NativeHelmetTicket>(
                new NativeHelmetTicket(channel_, hops_, std::move(permit), identity_, std::move(task_owner),
                                       std::move(model_owner)));
        } catch (...) {
            return {};
        }
    }
    std::string channel_, task_;
    std::array<NativeHelmetHop, 3> hops_;
    std::shared_ptr<helmet_candidate::FramePermitGate> gate_;
    helmet_candidate::RouteIdentity identity_;
    std::shared_ptr<helmet_candidate::LifecycleGate<helmet_candidate::TaskTag>> task_gate_;
    std::shared_ptr<helmet_candidate::LifecycleGate<helmet_candidate::ModelTag>> model_gate_;
    std::shared_ptr<const void> task_identity_, model_identity_;
    std::uint64_t task_generation_{}, model_generation_{};
    std::shared_ptr<const void> detector_registration_;
};

}  // namespace cosmo
