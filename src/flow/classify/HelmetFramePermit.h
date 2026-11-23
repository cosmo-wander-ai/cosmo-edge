#pragma once

// Private, disabled candidate. This is a pre-submit routing permit, not an SDK
// completion/retention owner and not a production graph authorization.
#include <cstdint>
#include <memory>
#include <mutex>
#include <type_traits>
#include <utility>

#include "media/NativeVideoBuffer.h"

namespace cosmo::helmet_candidate {
struct RouteIdentity {
    uint64_t channel{}, route{}, queue{}, task{}, task_generation{}, model{}, model_generation{},
        config_generation{};
    bool operator==(const RouteIdentity& o) const noexcept {
        return channel == o.channel && route == o.route && queue == o.queue && task == o.task &&
               task_generation == o.task_generation && model == o.model &&
               model_generation == o.model_generation && config_generation == o.config_generation;
    }
    bool Valid() const noexcept {
        return channel && route && queue && task && task_generation && model && model_generation &&
               config_generation;
    }
};
class FramePermitGate {
    struct State {
        std::mutex mu;
        RouteIdentity current{};
        uint64_t serial{0}, epoch{0};
        bool enabled{false}, occupied{false}, claimed{false}, published{false};
        cosmo::media::NativeVideoBufferPtr frame;
    };
    static bool ValidFrame(const cosmo::media::NativeVideoBufferPtr& f) noexcept {
        using namespace cosmo::media;
        return f && f->Valid() && f->memory_kind == NativeVideoMemoryKind::AxeraPhysical &&
               f->format == NativeVideoBufferFormat::NV12 && f->width == 1920 && f->height == 1080 &&
               f->crop_x == 0 && f->crop_y == 0 && f->planes[0].stride == 2048 &&
               f->planes[1].stride == 2048 && f->planes[0].bytes / f->planes[0].stride >= 1080 &&
               f->planes[1].bytes / f->planes[1].stride >= 540 &&
               f->color_space == NativeVideoColorSpace::Bt709 &&
               f->color_range == NativeVideoColorRange::Limited && f->frame_witness &&
               f->frame_witness->Matches(*f);
    }

public:
    class Permit {
    public:
        Permit()                         = default;
        Permit(const Permit&)            = delete;
        Permit& operator=(const Permit&) = delete;
        Permit(Permit&& o) noexcept
            : state_(std::move(o.state_)), id_(o.id_), serial_(o.serial_), epoch_(o.epoch_) {}
        Permit& operator=(Permit&& o) noexcept {
            if (this != &o) {
                Reset();
                state_  = std::move(o.state_);
                id_     = o.id_;
                serial_ = o.serial_;
                epoch_  = o.epoch_;
            }
            return *this;
        }
        ~Permit() {
            Reset();
        }
        bool IsReserved(const RouteIdentity& expected) const {
            if (!state_)
                return false;
            std::lock_guard<std::mutex> lock(state_->mu);
            return Current(expected) && !state_->claimed && ValidFrame(state_->frame);
        }
        // Only the one downstream classifier may claim, after tracker/filter.
        cosmo::media::NativeVideoBufferPtr Claim(const RouteIdentity& expected) {
            if (!state_)
                return {};
            std::lock_guard<std::mutex> lock(state_->mu);
            if (!Current(expected) || state_->claimed || !ValidFrame(state_->frame))
                return {};
            state_->claimed = true;
            return state_->frame;
        }
        bool CurrentForSubmit(const RouteIdentity& expected) const {
            if (!state_)
                return false;
            std::lock_guard<std::mutex> lock(state_->mu);
            return Current(expected) && state_->claimed && !state_->published && ValidFrame(state_->frame);
        }
        // Callback is a nonthrowing in-memory swap only; no I/O or re-entry.
        // Actual production task and model gates must enclose this publication.
        template <typename F>
        bool PublishIfCurrent(const RouteIdentity& expected, F&& publish) {
            static_assert(std::is_nothrow_invocable_v<F>, "publication must be noexcept");
            if (!state_)
                return false;
            std::lock_guard<std::mutex> lock(state_->mu);
            if (!Current(expected) || !state_->claimed || state_->published || !ValidFrame(state_->frame))
                return false;
            std::forward<F>(publish)();
            state_->published = true;
            return true;
        }
        void Reset() noexcept {
            if (!state_)
                return;
            cosmo::media::NativeVideoBufferPtr release;
            {
                std::lock_guard<std::mutex> lock(state_->mu);
                if (state_->occupied && state_->serial == serial_) {
                    release          = std::move(state_->frame);
                    state_->occupied = state_->claimed = state_->published = false;
                }
            }
            // Decoder owner may need to process the final release on its own thread.
            state_.reset();
        }

    private:
        friend class FramePermitGate;
        Permit(std::shared_ptr<State> s, RouteIdentity id, uint64_t serial, uint64_t epoch)
            : state_(std::move(s)), id_(id), serial_(serial), epoch_(epoch) {}
        bool Current(const RouteIdentity& expected) const noexcept {
            return state_->enabled && state_->occupied && state_->serial == serial_ &&
                   state_->epoch == epoch_ && state_->current == id_ && expected == id_;
        }
        std::shared_ptr<State> state_;
        RouteIdentity id_{};
        uint64_t serial_{}, epoch_{};
    };
    FramePermitGate() : state_(std::make_shared<State>()) {}
    FramePermitGate(const FramePermitGate&)            = delete;
    FramePermitGate& operator=(const FramePermitGate&) = delete;
    // Called by trusted route lifecycle only, which this prototype does not implement.
    bool Configure(const RouteIdentity& id) {
        if (!id.Valid())
            return false;
        std::lock_guard<std::mutex> lock(state_->mu);
        if (state_->epoch == UINT64_MAX) {
            state_->enabled = false;
            return false;
        }
        ++state_->epoch;
        state_->current = id;
        state_->enabled = true;
        return true;
    }
    void Disable() noexcept {
        std::lock_guard<std::mutex> lock(state_->mu);
        state_->enabled = false;
        if (state_->epoch != UINT64_MAX)
            ++state_->epoch;
    }
    Permit Reserve(const RouteIdentity& id, cosmo::media::NativeVideoBufferPtr frame) {
        std::lock_guard<std::mutex> lock(state_->mu);
        if (!state_->enabled || state_->occupied || !(state_->current == id) || !ValidFrame(frame) ||
            state_->serial == UINT64_MAX)
            return {};
        ++state_->serial;
        state_->occupied = true;
        state_->claimed = state_->published = false;
        state_->frame                       = std::move(frame);
        return Permit(state_, id, state_->serial, state_->epoch);
    }

private:
    std::shared_ptr<State> state_;
};
}  // namespace cosmo::helmet_candidate
