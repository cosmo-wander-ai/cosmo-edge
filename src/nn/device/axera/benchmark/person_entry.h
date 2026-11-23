#pragma once
#ifdef COSMO_NN_USE_AXERA_BACKEND
#include <atomic>
#include <cstdlib>
#include <limits>
#include <mutex>
#include <thread>
#include <utility>

#include "nn/device/axera/axera_person_native_entry.h"
#include "nn/device/axera/axera_route_identity.h"
#include "nn/device/axera/benchmark/BenchPersonShare.h"
#include "nn/device/axera/benchmark/BenchRgbPreprocess.h"
#include "nn/device/axera/benchmark/person_executor.h"
#include "util/Log.h"
#include "util/UuidUtil.h"
namespace cosmo::nn::axera::benchmark {
class PersonEntry {
    struct Identity {};

public:
    class Ticket {
        friend class PersonEntry;
        Ticket(std::weak_ptr<const Identity> id, std::uint64_t g) : id_(std::move(id)), generation_(g) {}
        std::weak_ptr<const Identity> id_;
        std::uint64_t generation_;
    };
    PersonEntry(std::unique_ptr<PersonExecutor> executor, std::shared_ptr<const void> identity,
                std::uint64_t generation, std::string atomic, std::string scene)
        : executor_(std::move(executor)),
          detector_(std::move(identity)),
          id_(std::make_shared<Identity>()),
          generation_(generation),
          atomic_(std::move(atomic)),
          scene_(std::move(scene)),
          owner_(std::this_thread::get_id()) {}
    Ticket GetTicket() const {
        return {id_, generation_};
    }
    const std::string& SceneId() const noexcept {
        return scene_;
    }
    void Invalidate() {
        std::lock_guard<std::mutex> g(mutex_);
        active_ = false;
    }
    PersonEntryResult Execute(const Ticket& ticket, const PersonFrameKey& key,
                              media::NativeVideoBufferPtr source, float threshold) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto fail = [](PersonEntryError e) { return PersonEntryResult{e, {}}; };
        if (!active_ || !executor_ || !detector_ || !generation_ || atomic_ != "9275710" ||
            (scene_ != "930927510" && scene_ != "930007463") || owner_ != std::this_thread::get_id())
            return fail(PersonEntryError::Closed);
        if (ticket.id_.lock() != id_ || ticket.generation_ != generation_)
            return fail(PersonEntryError::InvalidTicket);
        if (!source || !source->Valid() ||
            source->memory_kind != media::NativeVideoMemoryKind::AxeraPhysical ||
            source->format != media::NativeVideoBufferFormat::NV12 ||
            source->color_space != media::NativeVideoColorSpace::Bt709 ||
            source->color_range != media::NativeVideoColorRange::Limited || source->crop_x ||
            source->crop_y || !source->frame_witness || !source->frame_witness->Matches(*source) ||
            source->frame_witness->Generation() != key.stream_generation ||
            source->frame_witness->FrameIndex() != key.frame_index ||
            source->frame_witness->SourceTimestamp() != key.timestamp || key.channel_id.empty() ||
            source->width <= 0 || source->height <= 0 ||
            source->planes[0].bytes > std::numeric_limits<AX_U32>::max() ||
            source->planes[1].bytes > std::numeric_limits<AX_U32>::max())
            return fail(PersonEntryError::UnverifiedProvenance);
        if (!std::isfinite(threshold) || threshold < .25f || threshold > 1.f)
            return fail(PersonEntryError::InvalidThreshold);
        const char* history_path = std::getenv("COSMO_AXERA_BENCH_HISTORY61_MODEL");
        const bool use_history   = history_path && *history_path;
        auto* history            = use_history ? executor_->Historical(history_path) : nullptr;
        if ((use_history && !history) || (!use_history && executor_->HistoricalWasLoaded()))
            return fail(PersonEntryError::ExecutionFailed);
        std::vector<::benchmark_ax::Detection> boxes;
        PersonEntryError execution_error = PersonEntryError::ExecutionFailed;
        auto compute                     = [&](std::vector<::benchmark_ax::Detection>& computed) {
            if (use_history)
                return history->Run(source, computed);
            std::vector<std::uint8_t> rgb(640 * 640 * 3);
            auto& state = detail::NativeState();
            bench_ax::Nv12View v;
            bench_ax::Mapping m;
            {
                std::lock_guard<std::mutex> guard(state.mutex);
                if (state.poisoned)
                    return false;
                auto* y = AX_SYS_Mmap(source->planes[0].physical_address, source->planes[0].bytes);
                if (!y || y == reinterpret_cast<void*>(-1))
                    return false;
                auto* uv = AX_SYS_Mmap(source->planes[1].physical_address, source->planes[1].bytes);
                if (!uv || uv == reinterpret_cast<void*>(-1)) {
                    if (AX_SYS_Munmap(y, source->planes[0].bytes) != 0)
                        state.poisoned = true;
                    return false;
                }
                v = {static_cast<const std::uint8_t*>(y),
                     static_cast<const std::uint8_t*>(uv),
                     source->planes[0].bytes,
                     source->planes[1].bytes,
                     static_cast<int>(source->width),
                     static_cast<int>(source->height),
                     static_cast<int>(source->planes[0].stride),
                     static_cast<int>(source->planes[1].stride)};
                cached_y_.resize(v.y_bytes);
                cached_uv_.resize(v.uv_bytes);
                std::memcpy(cached_y_.data(), v.y, v.y_bytes);
                std::memcpy(cached_uv_.data(), v.uv, v.uv_bytes);
                v.y           = cached_y_.data();
                v.uv          = cached_uv_.data();
                const auto u  = AX_SYS_Munmap(uv, source->planes[1].bytes),
                           yy = AX_SYS_Munmap(y, source->planes[0].bytes);
                if (u || yy) {
                    state.poisoned = true;
                    return false;
                }
            }
            // Pixels now refer only to this PersonEntry's private vectors. Its mutex
            // remains held, so CPU conversion needs no global AX resource lock.
            if (!bench_ax::Convert(v, {0, 0, v.width, v.height}, 640, 640, bench_ax::Resize::Letterbox,
                                                       rgb.data(), rgb.size(), m)) {
                execution_error = PersonEntryError::InvalidFrame;
                return false;
            }
            return executor_->Run(source, rgb, computed);
        };
        const char* enabled = std::getenv("COSMO_AXERA_BENCH_PERSON_SHARE");
        bool reused = false, ok = false;
        if (enabled && std::strcmp(enabled, "1") == 0) {
            // VerifiedPersonModelBytes pins these model bytes. The remaining
            // contract is fixed by this entry and immutable native witness.
            static BenchPersonShare<std::vector<::benchmark_ax::Detection>> cache;
            static const std::string contract =
                cosmo::nn::axera::route_identity::Digest("benchmark.person_share_sha256") +
                "|nv12-bt709-limited|rgb640-"
                "letterbox|person80-conf.25-nms.7-top1000-v1";
            static const std::string history_contract =
                cosmo::nn::axera::route_identity::Digest("benchmark.person_history_sha256") +
                "|ivps-nv12-640x360|person80-"
                "conf.25-nms.7-top1000-v1";
            ok = cache.Run(key.channel_id, scene_, use_history ? history_contract : contract,
                           key.stream_generation, key.frame_index, key.timestamp, source->frame_witness,
                           boxes, compute, &reused);
            if (reused && !(use_history ? history->CanReuse() : executor_->CanReuse()))
                return fail(PersonEntryError::ExecutionFailed);
            if (std::getenv("COSMO_AXERA_BENCH_SHARE_PROFILE")) {
                static std::atomic<unsigned long long> calls{0}, hits{0};
                if (reused)
                    ++hits;
                const auto n = ++calls;
                if (n % 32 == 0)
                    LOG_INFO("AX_BENCH_PERSON_SHARE calls={} reused={}", n, hits.load());
            }
        } else
            ok = compute(boxes);
        if (!ok)
            return fail(execution_error);
        std::vector<AiDetectRstEl> out;
        for (const auto& b : boxes) {
            if (b.score < threshold || b.x2 <= b.x1 || b.y2 <= b.y1)
                continue;
            AiDetectRstEl t;
            t.box.x      = int(b.x1);
            t.box.y      = int(b.y1);
            t.box.width  = int(b.x2) - t.box.x + 1;
            t.box.height = int(b.y2) - t.box.y + 1;
            t.box &= util::Box(0, 0, source->width, source->height);
            if (t.box.width <= 0 || t.box.height <= 0)
                continue;
            t.hwRatio                = float(t.box.height) / t.box.width;
            t.confidence.confidence  = b.score;
            t.confidence.label       = "Pedestrian";
            t.confidence.atomic_code = atomic_;
            t.confidence.source      = AiAnnotationSource::Detection;
            t.classId                = 1;
            t.targetId               = util::GenerateUUID();
            out.push_back(std::move(t));
        }
        return {PersonEntryError::None, std::move(out)};
    }

private:
    std::unique_ptr<PersonExecutor> executor_;
    std::shared_ptr<const void> detector_;
    std::shared_ptr<const Identity> id_;
    std::uint64_t generation_;
    std::string atomic_, scene_;
    std::thread::id owner_;
    std::mutex mutex_;
    bool active_{true};
    std::vector<std::uint8_t> cached_y_, cached_uv_;
};
}  // namespace cosmo::nn::axera::benchmark
#endif
