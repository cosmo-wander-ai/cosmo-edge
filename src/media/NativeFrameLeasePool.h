#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <exception>
#include <limits>
#include <memory>
#include <new>
#include <optional>
#include <thread>
#include <type_traits>
#include <utility>

namespace cosmo::native_frame_candidate {
// SDK-free ownership primitive. Resource must be a passive SDK frame descriptor,
// not an RAII SDK owner. Context must retain every SDK object needed for cleanup.
template <class Resource>
class FrameLeasePool final {
    static_assert(std::is_trivially_copyable_v<Resource> && std::is_trivially_destructible_v<Resource>,
                  "passive descriptor required");
    struct Entry {
        Resource frame{};
        std::atomic<bool> returned{false};
        std::uint64_t sequence{};
        bool occupied{}, published{};
    };
    struct State {
        explicit State(std::weak_ptr<void> c, std::size_t limit)
            : context(std::move(c)), owner(std::this_thread::get_id()), capacity(limit) {}
        std::weak_ptr<void> context;
        std::shared_ptr<void> quarantine;
        const std::thread::id owner;
        const std::size_t capacity;
        std::array<Entry, 5> slots{};
        std::uint64_t next{1};
        std::atomic<bool> fault{false};
        bool closing{}, closed{};
    };
    struct Token {
        std::shared_ptr<State> state;
        std::shared_ptr<void> decoder_owner;
        Entry* entry{};
        bool armed{};
        Token(std::shared_ptr<State> s, std::shared_ptr<void> o, Entry* e)
            : state(std::move(s)), decoder_owner(std::move(o)), entry(e) {}
        ~Token() {
            if (armed)
                entry->returned.store(true, std::memory_order_release);
        }
    };
    static bool Owner(const std::shared_ptr<State>& s) noexcept {
        return s && s->owner == std::this_thread::get_id();
    }
    static void Preserve(std::shared_ptr<State> s) noexcept {
        auto* retained = new (std::nothrow) std::shared_ptr<State>(std::move(s));
        if (!retained)
            std::terminate();
    }

public:
    struct Status {
        std::size_t outstanding{}, returned_pending{};
        bool closing{}, fault{}, closed{}, owner_thread{};
    };
    class Reservation final {
    public:
        Reservation(const Reservation&)            = delete;
        Reservation& operator=(const Reservation&) = delete;
        Reservation(Reservation&& r) noexcept
            : state_(std::move(r.state_)), entry_(r.entry_), token_(std::move(r.token_)), active_(r.active_) {
            r.active_ = false;
            r.entry_  = nullptr;
        }
        Reservation& operator=(Reservation&&) = delete;
        ~Reservation() {
            if (active_)
                state_->fault.store(true, std::memory_order_release);
        }
        Resource& FrameData() noexcept {
            return entry_->frame;
        }
        std::shared_ptr<void> Publish() noexcept {
            if (!active_ || !Owner(state_) || state_->fault.load(std::memory_order_acquire) ||
                state_->closing)
                return {};
            entry_->published = true;
            token_->armed     = true;
            active_           = false;
            return std::move(token_);
        }
        bool CancelEmpty() noexcept {
            if (!active_ || !Owner(state_))
                return false;
            entry_->occupied  = false;
            entry_->published = false;
            active_           = false;
            token_.reset();
            return true;
        }
        void PoisonUncertain() noexcept {
            if (active_) {
                state_->fault.store(true, std::memory_order_release);
                active_ = false;
            }
        }

    private:
        friend class FrameLeasePool;
        Reservation(std::shared_ptr<State> s, Entry* e, std::shared_ptr<Token> t)
            : state_(std::move(s)), entry_(e), token_(std::move(t)), active_(true) {}
        std::shared_ptr<State> state_;
        Entry* entry_{};
        std::shared_ptr<Token> token_;
        bool active_{};
    };
    explicit FrameLeasePool(std::weak_ptr<void> context, std::size_t capacity = 1)
        : state_(std::make_shared<State>(std::move(context), capacity)) {
        if (state_->capacity == 0 || state_->capacity > state_->slots.size() || state_->context.expired())
            state_->fault.store(true, std::memory_order_release);
    }
    FrameLeasePool(const FrameLeasePool&)            = delete;
    FrameLeasePool& operator=(const FrameLeasePool&) = delete;
    ~FrameLeasePool() noexcept {
        if (state_ && !state_->closed) {
            state_->quarantine = state_->context.lock();
            Preserve(std::move(state_));
        }
    }
    std::optional<Reservation> TryReserve() {
        if (!Owner(state_) || state_->closing || state_->fault.load(std::memory_order_acquire) ||
            state_->next == UINT64_MAX)
            return {};
        auto decoder_owner = state_->context.lock();
        if (!decoder_owner) {
            state_->fault.store(true, std::memory_order_release);
            return {};
        }
        for (std::size_t i = 0; i < state_->capacity; ++i) {
            auto& e = state_->slots[i];
            if (!e.occupied) {
                auto token = std::make_shared<Token>(state_, decoder_owner,
                                                     &e);  // allocate before SDK may acquire a frame
                e.frame    = Resource{};
                e.returned.store(false, std::memory_order_relaxed);
                e.sequence  = state_->next++;
                e.occupied  = true;
                e.published = false;
                return Reservation(state_, &e, std::move(token));
            }
        }
        return {};
    }
    Status GetStatus() const noexcept {
        Status s;
        s.owner_thread = Owner(state_);
        if (!s.owner_thread)
            return s;
        s.closing = state_->closing;
        s.closed  = state_->closed;
        s.fault   = state_->fault.load(std::memory_order_acquire);
        for (const auto& e : state_->slots)
            if (e.occupied) {
                ++s.outstanding;
                if (e.published && e.returned.load(std::memory_order_acquire))
                    ++s.returned_pending;
            }
        return s;
    }
    bool BeginStop() noexcept {
        if (!Owner(state_) || state_->closed)
            return false;
        state_->closing = true;
        return true;
    }
    template <class Release>
    bool DrainOrdered(Release&& release) noexcept {
        static_assert(std::is_nothrow_invocable_r_v<bool, Release&, Resource&>,
                      "release must return bool and be noexcept");
        if (!Owner(state_) || state_->closed || state_->fault.load(std::memory_order_acquire))
            return false;
        if (state_->closing) {
            for (const auto& e : state_->slots)
                if (e.occupied && (!e.published || !e.returned.load(std::memory_order_acquire)))
                    return false;
        }
        for (;;) {
            Entry* first = nullptr;
            for (auto& e : state_->slots)
                if (e.occupied && (!first || e.sequence < first->sequence))
                    first = &e;
            if (!first || !first->published || !first->returned.load(std::memory_order_acquire))
                return true;
            if (!release(first->frame)) {
                state_->fault.store(true, std::memory_order_release);
                return false;
            }
            first->occupied  = false;
            first->published = false;
        }
    }
    template <class Release>
    bool DrainReturned(Release&& release) noexcept {
        static_assert(std::is_nothrow_invocable_r_v<bool, Release&, Resource&>,
                      "release must return bool and be noexcept");
        if (!Owner(state_) || state_->closed || state_->fault.load(std::memory_order_acquire))
            return false;
        if (state_->closing) {
            for (const auto& e : state_->slots)
                if (e.occupied && (!e.published || !e.returned.load(std::memory_order_acquire)))
                    return false;
        }
        for (auto& e : state_->slots)
            if (e.occupied && e.published && e.returned.load(std::memory_order_acquire)) {
                if (!release(e.frame)) {
                    state_->fault.store(true, std::memory_order_release);
                    return false;
                }
                e.occupied  = false;
                e.published = false;
            }
        return true;
    }
    // Caller must serialize this entire operation against all owner-thread operations
    // using the decoder's external mutex. Never releases a frame.
    template <class Teardown>
    bool FinishIdleStop(Teardown&& teardown) noexcept {
        static_assert(std::is_nothrow_invocable_r_v<bool, Teardown&>,
                      "teardown must return bool and be noexcept");
        if (state_->closed || state_->fault.load(std::memory_order_acquire))
            return false;
        for (const auto& e : state_->slots)
            if (e.occupied)
                return false;
        state_->closing = true;
        if (!teardown()) {
            state_->fault.store(true, std::memory_order_release);
            return false;
        }
        state_->context.reset();
        state_->closed = true;
        return true;
    }
    template <class Release, class Teardown>
    bool FinishStop(Release&& release, Teardown&& teardown) noexcept {
        static_assert(std::is_nothrow_invocable_r_v<bool, Teardown&>,
                      "teardown must return bool and be noexcept");
        if (!Owner(state_) || !state_->closing || state_->closed || !DrainOrdered(release))
            return false;
        for (const auto& e : state_->slots)
            if (e.occupied)
                return false;
        if (!teardown()) {
            state_->fault.store(true, std::memory_order_release);
            return false;
        }
        state_->context.reset();
        state_->closed = true;
        return true;
    }

private:
    std::shared_ptr<State> state_;
};
}  // namespace cosmo::native_frame_candidate
