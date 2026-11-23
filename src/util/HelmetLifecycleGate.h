#pragma once
// Candidate only. Real TaskBase/model/queue lifecycle must own, cancel and
// drain these exact gate instances before this can authorize any device call.
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <type_traits>
#include <utility>
namespace cosmo::helmet_candidate {
struct TaskTag {};
struct ModelTag {};
template <class Tag>
class LifecycleGate final {
    struct State {
        std::mutex mu;
        std::atomic<bool> cancel_requested{false};
        std::shared_ptr<const void> identity;
        std::uint64_t generation{};
        bool active{true};
        std::size_t in_flight{};
    };
    static bool SameOwner(const std::shared_ptr<const void>& a,
                          const std::shared_ptr<const void>& b) noexcept {
        return a && b && a.get() == b.get() && !a.owner_before(b) && !b.owner_before(a);
    }

public:
    class Permit final {
    public:
        Permit(const Permit&)            = delete;
        Permit& operator=(const Permit&) = delete;
        Permit(Permit&& other) noexcept
            : state_(std::move(other.state_)), begun_(other.begun_), committed_(other.committed_) {}
        Permit& operator=(Permit&&) = delete;
        ~Permit() {
            if (!state_)
                return;
            std::lock_guard<std::mutex> lock(state_->mu);
            --state_->in_flight;
        }
        bool TryBegin() {
            if (!state_ || begun_)
                return false;
            std::unique_lock<std::mutex> lock(state_->mu, std::try_to_lock);
            if (!lock || !state_->active || state_->cancel_requested.load(std::memory_order_acquire))
                return false;
            begun_ = true;
            return true;
        }
        bool StillCurrent() const {
            if (!state_ || !begun_ || committed_)
                return false;
            std::unique_lock<std::mutex> lock(state_->mu, std::try_to_lock);
            return lock && state_->active && !state_->cancel_requested.load(std::memory_order_acquire);
        }
        template <class F>
        bool CommitIfCurrent(F&& f) noexcept {
            static_assert(std::is_nothrow_invocable_v<F&>, "commit callback must be noexcept");
            if (!state_ || !begun_ || committed_)
                return false;
            try {
                std::unique_lock<std::mutex> lock(state_->mu, std::try_to_lock);
                if (!lock || !state_->active || state_->cancel_requested.load(std::memory_order_acquire))
                    return false;
                committed_ = true;
                f();
                return true;
            } catch (...) {
                return false;
            }
        }

    private:
        friend class LifecycleGate;
        explicit Permit(std::shared_ptr<State> state) : state_(std::move(state)) {}
        std::shared_ptr<State> state_;
        bool begun_{false}, committed_{false};
    };
    static std::unique_ptr<LifecycleGate> Create(std::shared_ptr<const void> identity,
                                                 std::uint64_t generation) {
        if (!identity || !generation)
            return {};
        auto s        = std::make_shared<State>();
        s->identity   = std::move(identity);
        s->generation = generation;
        return std::unique_ptr<LifecycleGate>(new LifecycleGate(std::move(s)));
    }
    std::optional<Permit> Acquire(const std::shared_ptr<const void>& identity,
                                  std::uint64_t generation) const {
        std::unique_lock<std::mutex> lock(state_->mu, std::try_to_lock);
        if (!lock || !state_->active || state_->cancel_requested.load(std::memory_order_acquire) ||
            !SameOwner(state_->identity, identity) || state_->generation != generation ||
            state_->in_flight != 0)
            return {};
        ++state_->in_flight;
        return Permit(state_);
    }
    // A false result means the caller must defer destruction and retry; it
    // does not reset/abandon any submitted device owner. Never re-enable a gate.
    bool Cancel() const {
        state_->cancel_requested.store(true, std::memory_order_release);
        std::unique_lock<std::mutex> lock(state_->mu, std::try_to_lock);
        if (!lock)
            return false;
        state_->active = false;
        return state_->in_flight == 0;
    }

private:
    explicit LifecycleGate(std::shared_ptr<State> state) : state_(std::move(state)) {}
    std::shared_ptr<State> state_;
};
// Ownership bases live below both flow and nn; destroying an in-flight frame
// lease or permit on an unknown SDK completion remains forbidden.
struct PermitOwner {
    virtual ~PermitOwner() = default;
};
struct FrameLeaseOwner {
    virtual ~FrameLeaseOwner() = default;
};
template <class Tag>
struct ManagedPermit final : PermitOwner {
    using Permit = typename LifecycleGate<Tag>::Permit;
    explicit ManagedPermit(Permit&& value) : permit(std::move(value)) {}
    Permit permit;
};

}  // namespace cosmo::helmet_candidate
