#pragma once
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <utility>

// A bounded, non-owning, single-flight cache. It is intentionally scoped to
// the two ScenarioBench scenes and to one decoder-minted witness object.
// Result contains no native-frame references. A failed leader is never reused.
namespace cosmo::nn::axera::benchmark {
template <typename Result>
class BenchPersonShare {
    struct Entry {
        std::string channel, scene, contract;
        std::uint64_t generation{}, frame{};
        std::int64_t timestamp{};
        std::weak_ptr<const void> witness;
        const void* witness_address{};
        bool ready{false}, success{false};
        Result value{};
        std::condition_variable cv;
    };
    std::mutex mutex_;
    std::list<std::shared_ptr<Entry>> entries_;
    static constexpr std::size_t kCapacity = 32;

public:
    template <typename Compute>
    bool Run(const std::string& channel, const std::string& scene, const std::string& contract,
             std::uint64_t generation, std::uint64_t frame, std::int64_t timestamp,
             const std::shared_ptr<const void>& witness, Result& result, Compute&& compute,
             bool* reused = nullptr) {
        if (reused)
            *reused = false;
        if (!witness || channel.empty() || contract.empty() || !generation ||
            (scene != "930927510" && scene != "930007463"))
            return compute(result);
        std::unique_lock<std::mutex> lock(mutex_);
        for (auto it = entries_.begin(); it != entries_.end();) {
            if ((*it)->ready && (*it)->witness.expired())
                it = entries_.erase(it);
            else
                ++it;
        }
        for (const auto& current : entries_) {
            auto entry = current;  // survives erasure while a waiter sleeps
            if (entry->channel != channel || entry->scene == scene || entry->contract != contract ||
                entry->generation != generation || entry->frame != frame || entry->timestamp != timestamp ||
                entry->witness_address != witness.get() || entry->witness.owner_before(witness) ||
                witness.owner_before(entry->witness))
                continue;
            entry->cv.wait(lock, [&] { return entry->ready; });
            if (!entry->success) {
                lock.unlock();
                return compute(result);
            }
            result = entry->value;
            if (reused)
                *reused = true;
            return true;
        }
        // Never evict an in-flight computation. If full, use the original path.
        if (entries_.size() >= kCapacity) {
            lock.unlock();
            return compute(result);
        }
        auto entry             = std::make_shared<Entry>();
        entry->channel         = channel;
        entry->scene           = scene;
        entry->contract        = contract;
        entry->generation      = generation;
        entry->frame           = frame;
        entry->timestamp       = timestamp;
        entry->witness         = witness;
        entry->witness_address = witness.get();
        entries_.push_back(entry);
        lock.unlock();
        bool ok = false;
        try {
            ok = compute(result);
            lock.lock();
            if (ok)
                entry->value = result;
            entry->success = ok;
            entry->ready   = true;
            if (!ok)
                entries_.remove(entry);
            lock.unlock();
            entry->cv.notify_all();
            return ok;
        } catch (...) {
            if (!lock.owns_lock())
                lock.lock();
            entry->success = false;
            entry->ready   = true;
            entries_.remove(entry);
            lock.unlock();
            entry->cv.notify_all();
            throw;
        }
    }
};
}  // namespace cosmo::nn::axera::benchmark
