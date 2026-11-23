#pragma once
#if defined(COSMO_NN_USE_AXERA_BACKEND) && defined(COSMO_MEDIA_USE_AXERA_BACKEND)
#include <ax_engine_api.h>
#include <ax_ivps_api.h>
#include <ax_sys_api.h>
#include <cryptopp/sha.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <thread>
#include <unordered_map>
#include <vector>

#include "media/NativeVideoBuffer.h"
#include "nn/device/axera/axera_face_model_sdk_loader.h"
#include "nn/device/axera/axera_native_execution_state.h"
#include "nn/device/axera/benchmark/BenchEngineWorker.h"
#include "nn/device/axera/benchmark/BenchLegacyPersonNms.h"
#include "nn/device/axera/benchmark/BenchTurnScheduler.h"
#include "nn/device/axera/benchmark/postprocess.h"
#include "util/Log.h"
namespace cosmo::nn::axera::benchmark {
// Historical, benchmark-only model variant. It is NOT the current dc6c model.
class BenchHistoryNv12 final {
    struct Block {
        AX_U64 phy{};
        void* host{};
        AX_U32 size{};
    };
    struct Reusable {
        Block in{}, out{};
        std::shared_ptr<face_binding::SdkLoadedModel> model;
        std::thread::id owner;
    };
    struct Pending {
        media::NativeVideoBufferPtr source;
        std::shared_ptr<face_binding::SdkLoadedModel> model;
        AX_VIDEO_FRAME_T input{}, scaled{};
        AX_IVPS_ASPECT_RATIO_T aspect{};
        Block in{}, out{};
        AX_ENGINE_IO_BUFFER_T i{}, o{};
        AX_ENGINE_IO_T io{};
        std::shared_ptr<Reusable> reusable;
        std::shared_ptr<Pending> quarantine;
    };
    std::shared_ptr<face_binding::SdkLoadedModel> model_;
    std::thread::id owner_;
    bool reuse_enabled_{false};
    bool noncached_output_{false};
    std::uint64_t worker_model_id_{0};
    struct WorkerModels {
        std::unordered_map<std::uint64_t, std::shared_ptr<face_binding::SdkLoadedModel>> models;
        std::uint64_t next{1};
    };
    // Only the central worker accesses this registry. A poisoned model must
    // survive process shutdown rather than be destroyed on an arbitrary thread.
    static WorkerModels& Models() {
        static auto* models = new WorkerModels;
        return *models;
    }
    // Declared after model_ so clean destruction frees buffers before the
    // owner-thread model reference can become last.
    std::shared_ptr<Reusable> reusable_;
    static void DeleteReusable(Reusable* p) noexcept {
        if (!p)
            return;
        auto& state = detail::NativeState();
        try {
            {
                std::lock_guard<std::mutex> lock(state.mutex);
                // A poisoned domain or a wrong owner cannot release SDK memory
                // or the model; intentionally retain the whole allocation.
                if (state.poisoned || p->owner != std::this_thread::get_id()) {
                    state.poisoned = true;
                    return;
                }
                try {
                    for (auto* b : {&p->out, &p->in}) {
                        if (!b->phy && !b->host)
                            continue;
                        if (!b->phy || !b->host || AX_SYS_MemFree(b->phy, b->host) != 0) {
                            state.poisoned = true;
                            return;
                        }
                        *b = {};
                    }
                } catch (...) {
                    state.poisoned = true;
                    return;
                }
            }
            // SdkLoadedModel final deletion itself takes NativeState.mutex.
            delete p;
        } catch (...) { /* mutex failure: retain all resources without SDK calls */
        }
    }
    static bool Dense(const AX_ENGINE_IOMETA_T& m, std::size_t bytes) {
        std::size_t count = 1;
        for (std::size_t j = m.nShapeSize; j > 0; --j) {
            const auto n = m.pShape[j - 1];
            if (n <= 0 || (m.pStride && (m.pStride[j - 1] <= 0 || std::size_t(m.pStride[j - 1]) != count)) ||
                count > std::numeric_limits<std::size_t>::max() / std::size_t(n))
                return false;
            count *= std::size_t(n);
        }
        return count <= std::numeric_limits<std::size_t>::max() / bytes && count * bytes == m.nSize;
    }
    static bool Metadata(AX_ENGINE_HANDLE handle) {
        AX_ENGINE_IO_INFO_T* meta = nullptr;
        if (AX_ENGINE_GetIOInfo(handle, &meta) != 0 || !meta || meta->nInputSize != 1 ||
            meta->nOutputSize != 1 || !meta->pInputs || !meta->pOutputs)
            return false;
        const auto& i = meta->pInputs[0];
        const auto& o = meta->pOutputs[0];
        const int is[]{1, 540, 640, 1}, os[]{1, 84, 8400};
        return i.eDataType == AX_ENGINE_DT_UINT8 && i.eLayout == AX_ENGINE_TENSOR_LAYOUT_NHWC &&
               i.pExtraMeta && i.pExtraMeta->eColorSpace == AX_ENGINE_CS_NV12 && i.nShapeSize == 4 &&
               i.pShape && std::equal(std::begin(is), std::end(is), i.pShape) && i.nSize == 345600 &&
               o.eDataType == AX_ENGINE_DT_FLOAT32 && o.nShapeSize == 3 && o.pShape &&
               std::equal(std::begin(os), std::end(os), o.pShape) && o.nSize == 84 * 8400 * 4 &&
               Dense(i, 1) && Dense(o, sizeof(float));
    }

public:
    ~BenchHistoryNv12() {
        if (!worker_model_id_)
            return;
        reusable_.reset();
        const auto id    = worker_model_id_;
        worker_model_id_ = 0;
        try {
            cosmo::bench_ax::BenchmarkEngineWorker().Invoke(false, [id] { Models().models.erase(id); });
        } catch (...) { /* retain an inaccessible model if the worker cannot accept cleanup */
        }
    }
    static std::unique_ptr<BenchHistoryNv12> Load(const char* path) {
        if (!path || !*path)
            return {};
        std::ifstream in(path, std::ios::binary);
        if (!in)
            return {};
        std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)), {});
        if (bytes.empty() || bytes.size() > std::numeric_limits<AX_U32>::max())
            return {};
        std::array<std::uint8_t, 32> digest{};
        CryptoPP::SHA256().CalculateDigest(digest.data(), bytes.data(), bytes.size());
        static constexpr std::array<std::uint8_t, 32> expected{
            {0x61, 0xee, 0x6c, 0xc6, 0xca, 0xe3, 0x30, 0xb8, 0x49, 0x32, 0xf7, 0x35, 0x27, 0x41, 0xc2, 0xf7,
             0x24, 0xb6, 0xb7, 0xdf, 0xa0, 0x5e, 0x25, 0xf0, 0x82, 0xbd, 0x84, 0x7a, 0x6e, 0x79, 0x06, 0xf3}};
        if (digest != expected)
            return {};
        auto owned  = std::make_shared<const std::vector<std::uint8_t>>(std::move(bytes));
        auto out    = std::unique_ptr<BenchHistoryNv12>(new BenchHistoryNv12);
        out->owner_ = std::this_thread::get_id();
        if (cosmo::bench_ax::BenchmarkEngineWorkerEnabled()) {
            // Construct and validate the handle on its eventual execution and
            // destruction thread; never hand its borrowed handle to the caller.
            try {
                out->worker_model_id_ =
                    cosmo::bench_ax::BenchmarkEngineWorker().Invoke(false, [owned]() -> std::uint64_t {
                        face_binding::SdkLoader loader;
                        auto model =
                            std::dynamic_pointer_cast<face_binding::SdkLoadedModel>(loader.Load(owned));
                        if (!model)
                            return 0;
                        auto handle = model->HandleForOwnerThread();
                        auto& state = detail::NativeState();
                        {
                            std::lock_guard<std::mutex> lock(state.mutex);
                            if (state.poisoned || !handle || !Metadata(handle))
                                return 0;
                        }
                        auto& registry = Models();
                        if (registry.next == 0)
                            return 0;
                        const auto id = registry.next++;
                        registry.models.emplace(id, std::move(model));
                        return id;
                    });
            } catch (...) {
                return {};
            }
            if (!out->worker_model_id_)
                return {};
        } else {
            face_binding::SdkLoader loader;
            auto model = std::dynamic_pointer_cast<face_binding::SdkLoadedModel>(loader.Load(owned));
            if (!model)
                return {};
            auto handle = model->HandleForOwnerThread();
            auto& state = detail::NativeState();
            {
                std::lock_guard<std::mutex> lock(state.mutex);
                if (state.poisoned || !handle || !Metadata(handle))
                    return {};
            }
            out->model_ = std::move(model);
        }
        const char* reuse      = std::getenv("COSMO_AXERA_BENCH_HISTORY61_REUSE_BUFFERS");
        out->reuse_enabled_    = reuse && std::strcmp(reuse, "1") == 0;
        const char* legacy_out = std::getenv("COSMO_AXERA_BENCH_LEGACY_OUTPUT_CMM");
        out->noncached_output_ = legacy_out && std::strcmp(legacy_out, "1") == 0;
        if (out->noncached_output_)
            LOG_INFO("AX_BENCH_LEGACY_OUTPUT_CMM enabled");
        return out;
    }
    bool CanReuse() const {
        return owner_ == std::this_thread::get_id() &&
               (worker_model_id_ || (model_ && model_->HandleForOwnerThread() != nullptr));
    }
    bool Run(media::NativeVideoBufferPtr source, std::vector<::benchmark_ax::Detection>& result) {
        struct Profile {
            uint64_t count{}, lock_ns{}, initial_hold_ns{}, postvpp_hold_ns{}, postworker_wait_ns{},
                postworker_hold_ns{}, final_wait_ns{}, final_hold_ns{}, vpp_ns{}, ivps_wait_ns{},
                ivps_prelock_ns{}, ivps_postlock_ns{}, turn_ns{}, turn_hold_ns{}, turn_relock_ns{}, npu_ns{},
                invalidate_ns{}, worker_ns{}, worker_queue_ns{}, worker_lock_ns{};
        };
        static thread_local Profile prof;
        const char* trace_opt = std::getenv("COSMO_AXERA_BENCH_LOCK_PROFILE");
        const bool trace      = trace_opt && std::strcmp(trace_opt, "1") == 0;
        using Clock           = std::chrono::steady_clock;
        auto ns               = [](Clock::time_point t) {
            return static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - t).count());
        };
        if (!source || !source->Valid() || !source->frame_witness ||
            !source->frame_witness->Matches(*source) || source->width != 1920 || source->height != 1080 ||
            source->crop_x || source->crop_y ||
            source->memory_kind != media::NativeVideoMemoryKind::AxeraPhysical ||
            source->format != media::NativeVideoBufferFormat::NV12 ||
            source->color_space != media::NativeVideoColorSpace::Bt709 ||
            source->color_range != media::NativeVideoColorRange::Limited ||
            owner_ != std::this_thread::get_id() || (!model_ && !worker_model_id_))
            return false;
        const auto* frame = static_cast<const AX_VIDEO_FRAME_T*>(source->backend_frame);
        if (!frame || frame->u32Width != 1920 || frame->u32Height != 1080 ||
            frame->u32FrameSize != source->bytes || frame->enImgFormat != AX_FORMAT_YUV420_SEMIPLANAR ||
            frame->stCompressInfo.enCompressMode != AX_COMPRESS_MODE_NONE ||
            frame->u64PhyAddr[0] != source->planes[0].physical_address ||
            frame->u64PhyAddr[1] != source->planes[1].physical_address ||
            frame->u32PicStride[0] != source->planes[0].stride ||
            frame->u32PicStride[1] != source->planes[1].stride || frame->s16CropX || frame->s16CropY ||
            frame->s16CropWidth || frame->s16CropHeight)
            return false;
        auto handle = worker_model_id_ ? nullptr : model_->HandleForOwnerThread();
        if (!worker_model_id_ && !handle)
            return false;
        if (reuse_enabled_ && !reusable_) {
            try {
                reusable_ = std::shared_ptr<Reusable>(new Reusable{{}, {}, model_, owner_}, DeleteReusable);
            } catch (...) {
                return false;
            }
        }
        auto p                          = std::make_shared<Pending>();
        p->source                       = std::move(source);
        p->model                        = model_;
        p->input                        = *frame;
        p->reusable                     = reusable_;
        const char* legacy_env          = std::getenv("COSMO_AXERA_BENCH_LEGACY_PERSON_NMS");
        const bool legacy               = legacy_env && std::strcmp(legacy_env, "1") == 0;
        const char* borrowed_output_env = std::getenv("COSMO_AXERA_BENCH_BORROW_OUTPUT");
        const bool borrowed_output      = !legacy && !noncached_output_ && borrowed_output_env &&
                                     std::strcmp(borrowed_output_env, "1") == 0;
        bool borrowed_result_ok = false;
        std::vector<float> tensor;
        if (!legacy && !borrowed_output)
            tensor.resize(84 * 8400);
        std::array<float, 32 * 6> rows{};
        std::array<float, 32> keep{};
        int legacy_status = -1;
        auto& state       = detail::NativeState();
        {
            const auto lock_start = trace ? Clock::now() : Clock::time_point{};
            std::unique_lock<std::mutex> lock(state.mutex);
            if (trace)
                prof.lock_ns += ns(lock_start);
            if (state.poisoned)
                return false;
            const auto initial_acquired  = trace ? Clock::now() : Clock::time_point{};
            static bool ivps_initialized = false;
            if (!ivps_initialized) {
                if (AX_IVPS_Init() != 0)
                    return false;
                ivps_initialized = true;
            }
            p->quarantine = p;  // retain source, handle and every partial SDK resource on uncertainty
            auto unknown  = [&]() {
                state.poisoned = true;
                return false;
            };
            if (p->reusable && p->reusable->in.phy && p->reusable->out.phy) {
                p->in  = p->reusable->in;
                p->out = p->reusable->out;
            } else {
                p->in.size  = 345600;
                p->out.size = 84 * 8400 * 4;
                auto rc     = AX_SYS_MemAlloc(&p->in.phy, &p->in.host, p->in.size, 128,
                                              reinterpret_cast<const AX_S8*>("bench-history61-in"));
                if (rc || !p->in.phy || !p->in.host) {
                    if (p->in.phy || p->in.host)
                        return unknown();
                    p->quarantine.reset();
                    return false;
                }
                if (p->in.phy > std::numeric_limits<AX_U64>::max() - p->in.size ||
                    reinterpret_cast<std::uintptr_t>(p->in.host) >
                        std::numeric_limits<std::uintptr_t>::max() - p->in.size)
                    return unknown();
                rc = noncached_output_
                         ? AX_SYS_MemAlloc(&p->out.phy, &p->out.host, p->out.size, 128,
                                           reinterpret_cast<const AX_S8*>("bench-history61-out"))
                         : AX_SYS_MemAllocCached(&p->out.phy, &p->out.host, p->out.size, 128,
                                                 reinterpret_cast<const AX_S8*>("bench-history61-out"));
                if (rc || !p->out.phy || !p->out.host) {
                    if (p->out.phy || p->out.host)
                        return unknown();
                    if (AX_SYS_MemFree(p->in.phy, p->in.host) != 0)
                        return unknown();
                    p->in = {};
                    p->quarantine.reset();
                    return false;
                }
                if (p->reusable) {
                    p->reusable->in  = p->in;
                    p->reusable->out = p->out;
                }
            }
            auto& f               = p->scaled;
            f.u32Width            = 640;
            f.u32Height           = 360;
            f.u32FrameSize        = 345600;
            f.u32PicStride[0]     = 640;
            f.u32PicStride[1]     = 640;
            f.enImgFormat         = AX_FORMAT_YUV420_SEMIPLANAR;
            f.stColorGamut        = AX_COLOR_GAMUT_BT709;
            f.u64PhyAddr[0]       = p->in.phy;
            f.u64PhyAddr[1]       = p->in.phy + 230400;
            f.u64VirAddr[0]       = reinterpret_cast<std::uintptr_t>(p->in.host);
            f.u64VirAddr[1]       = f.u64VirAddr[0] + 230400;
            p->aspect.eMode       = AX_IVPS_ASPECT_RATIO_STRETCH;
            AX_S32 vpp_rc         = -1;
            const char* split_opt = std::getenv("COSMO_AXERA_BENCH_IVPS_SEPARATE");
            const bool split      = split_opt && std::strcmp(split_opt, "1") == 0;
            if (split) {
                // All owners are already pinned in p->quarantine. Serialize
                // this benchmark's IVPS calls separately from engine calls.
                static auto* ivps_mutex = new std::mutex;
                if (trace)
                    prof.initial_hold_ns += ns(initial_acquired);
                lock.unlock();
                for (;;) {
                    const auto domain_start = trace ? Clock::now() : Clock::time_point{};
                    std::unique_lock<std::mutex> ivps(*ivps_mutex);
                    if (trace)
                        prof.ivps_wait_ns += ns(domain_start);
                    if (!lock.try_lock()) {
                        ivps.unlock();
                        const auto prelock_start = trace ? Clock::now() : Clock::time_point{};
                        lock.lock();
                        if (trace)
                            prof.ivps_prelock_ns += ns(prelock_start);
                        if (state.poisoned)
                            return unknown();
                        lock.unlock();
                        continue;
                    }
                    if (state.poisoned)
                        return unknown();
                    lock.unlock();
                    const auto vpp_start = trace ? Clock::now() : Clock::time_point{};
                    try {
                        vpp_rc = AX_IVPS_CropResizeVpp(&p->input, &f, &p->aspect);
                    } catch (...) {
                        vpp_rc = -1;
                    }
                    if (trace)
                        prof.vpp_ns += ns(vpp_start);
                    ivps.unlock();
                    const auto postlock_start = trace ? Clock::now() : Clock::time_point{};
                    lock.lock();
                    if (trace)
                        prof.ivps_postlock_ns += ns(postlock_start);
                    break;
                }
            } else {
                const auto vpp_start = trace ? Clock::now() : Clock::time_point{};
                vpp_rc               = AX_IVPS_CropResizeVpp(&p->input, &f, &p->aspect);
                if (trace)
                    prof.vpp_ns += ns(vpp_start);
            }
            const auto postvpp_acquired = trace ? Clock::now() : Clock::time_point{};
            if (vpp_rc != 0 || state.poisoned)
                return unknown();
            if (split) {
                static std::atomic<uint64_t> calls{0};
                const auto n = ++calls;
                if (n == 1 || n % 256 == 0)
                    LOG_INFO("AX_BENCH_IVPS_SPLIT calls={}", n);
            }
            std::optional<cosmo::bench_ax::BenchTurnScheduler::Turn> turn;
            auto turn_acquired = Clock::time_point{};
            if (!worker_model_id_ && cosmo::bench_ax::BenchmarkTurnsEnabled()) {
                lock.unlock();
                const auto turn_start = trace ? Clock::now() : Clock::time_point{};
                try {
                    turn.emplace(cosmo::bench_ax::BenchmarkTurns().Acquire(false));
                } catch (...) {
                    lock.lock();
                    return unknown();
                }
                if (trace)
                    prof.turn_ns += ns(turn_start);
                turn_acquired = trace ? Clock::now() : Clock::time_point{};
                lock.lock();
                if (state.poisoned)
                    return unknown();
                if (trace)
                    prof.turn_relock_ns += ns(turn_acquired);
            }
            p->i.phyAddr             = p->in.phy;
            p->i.pVirAddr            = p->in.host;
            p->i.nSize               = p->in.size;
            p->o.phyAddr             = p->out.phy;
            p->o.pVirAddr            = p->out.host;
            p->o.nSize               = p->out.size;
            p->io.pInputs            = &p->i;
            p->io.nInputSize         = 1;
            p->io.pOutputs           = &p->o;
            p->io.nOutputSize        = 1;
            AX_S32 npu_rc            = -1;
            auto postworker_acquired = Clock::time_point{};
            if (worker_model_id_) {
                const auto id               = worker_model_id_;
                std::uint64_t actual_npu_ns = 0, queue_ns = 0, worker_lock_ns = 0;
                if (trace)
                    prof.postvpp_hold_ns += ns(postvpp_acquired);
                lock.unlock();
                const auto worker_start = trace ? Clock::now() : Clock::time_point{};
                try {
                    npu_rc = cosmo::bench_ax::BenchmarkEngineWorker().Invoke(
                        false,
                        [id, p, trace, worker_start, &actual_npu_ns, &queue_ns, &worker_lock_ns]() -> AX_S32 {
                            const auto began = trace ? Clock::now() : Clock::time_point{};
                            if (trace)
                                queue_ns = static_cast<std::uint64_t>(
                                    std::chrono::duration_cast<std::chrono::nanoseconds>(began - worker_start)
                                        .count());
                            auto& registry = Models();
                            auto it        = registry.models.find(id);
                            if (it == registry.models.end())
                                return -1;
                            auto* handle = it->second->HandleForOwnerThread();
                            if (!handle)
                                return -1;
                            auto& state = detail::NativeState();
                            std::lock_guard<std::mutex> lock(state.mutex);
                            if (state.poisoned)
                                return -1;
                            const auto started = trace ? Clock::now() : Clock::time_point{};
                            if (trace)
                                worker_lock_ns = static_cast<std::uint64_t>(
                                    std::chrono::duration_cast<std::chrono::nanoseconds>(started - began)
                                        .count());
                            const auto rc = AX_ENGINE_RunSync(handle, &p->io);
                            if (trace)
                                actual_npu_ns = static_cast<std::uint64_t>(
                                    std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() -
                                                                                         started)
                                        .count());
                            if (rc != 0)
                                state.poisoned = true;
                            return rc;
                        });
                } catch (...) {
                    npu_rc = -1;
                }
                if (trace) {
                    prof.worker_ns += ns(worker_start);
                    prof.worker_queue_ns += queue_ns;
                    prof.worker_lock_ns += worker_lock_ns;
                    prof.npu_ns += actual_npu_ns;
                }
                const auto postworker_start = trace ? Clock::now() : Clock::time_point{};
                lock.lock();
                if (trace) {
                    prof.postworker_wait_ns += ns(postworker_start);
                    postworker_acquired = Clock::now();
                }
            } else {
                const auto npu_start = trace ? Clock::now() : Clock::time_point{};
                npu_rc               = AX_ENGINE_RunSync(handle, &p->io);
                if (trace)
                    prof.npu_ns += ns(npu_start);
            }
            if (npu_rc != 0)
                return unknown();
            const auto invalidate_start = trace ? Clock::now() : Clock::time_point{};
            const auto invalidate_rc =
                noncached_output_ ? 0 : AX_SYS_MinvalidateCache(p->out.phy, p->out.host, p->out.size);
            if (trace)
                prof.invalidate_ns += ns(invalidate_start);
            if (invalidate_rc != 0)
                return unknown();
            if (trace && turn)
                prof.turn_hold_ns += ns(turn_acquired);
            turn.reset();
            auto final_acquired = Clock::time_point{};
            if (legacy) {
                // Historical CPU NMS reads this call's exclusive output while the
                // global SDK lock is open. Pending owns all buffers and the frame.
                if (trace && worker_model_id_)
                    prof.postworker_hold_ns += ns(postworker_acquired);
                lock.unlock();
                try {
                    legacy_status = person_postprocess32(static_cast<const float*>(p->out.host), rows.data(),
                                                         keep.data());
                } catch (...) {
                    legacy_status = -1;
                }
                const auto final_start = trace ? Clock::now() : Clock::time_point{};
                lock.lock();
                if (trace)
                    prof.final_wait_ns += ns(final_start);
                final_acquired = trace ? Clock::now() : Clock::time_point{};
                if (state.poisoned)
                    return false;
                if (legacy_status != 0) {
                    // Explicitly documented semantic fallback until historical
                    // NMS32/NMS128 models are wired into this private candidate.
                    try {
                        tensor.resize(84 * 8400);
                    } catch (...) {
                        return unknown();
                    }
                    std::memcpy(tensor.data(), p->out.host, p->out.size);
                }
                static std::atomic<unsigned> calls{0}, fallbacks{0};
                const auto n = ++calls;
                const auto f = legacy_status == 0 ? fallbacks.load() : ++fallbacks;
                if (n <= 3 || n % 500 == 0 || legacy_status != 0)
                    LOG_INFO("AX_BENCH_LEGACY_PERSON_NMS calls:{} fallback:{} status:{}", n, f,
                             legacy_status);
            } else {
                // RunSync is complete. Pending retains the output while this
                // owner thread reads it outside the global SDK lock.
                lock.unlock();
                if (borrowed_output) {
                    static const bool announced = [] {
                        LOG_INFO("{}", "AX_BENCH_FULL80_BORROWED_CACHED_OUTPUT active");
                        return true;
                    }();
                    (void)announced;
                    try {
                        const auto* output = static_cast<const float*>(p->out.host);
                        result = ::benchmark_ax::person(output, 4 * 8400, output + 4 * 8400, 80 * 8400,
                                                        p->source->width, p->source->height);
                        borrowed_result_ok = true;
                    } catch (...) {
                        result.clear();
                    }
                } else {
                    std::memcpy(tensor.data(), p->out.host, p->out.size);
                }
                lock.lock();
                if (state.poisoned) {
                    if (borrowed_output)
                        result.clear();
                    return false;
                }
            }
            if (!p->reusable) {
                bool freed = true;
                if (AX_SYS_MemFree(p->out.phy, p->out.host) != 0)
                    freed = false;
                else
                    p->out = {};
                if (AX_SYS_MemFree(p->in.phy, p->in.host) != 0)
                    freed = false;
                else
                    p->in = {};
                if (!freed) {
                    if (borrowed_output)
                        result.clear();
                    return unknown();
                }
            }
            p->quarantine.reset();
            if (trace && legacy)
                prof.final_hold_ns += ns(final_acquired);
        }
        if (trace && ++prof.count % 32 == 0)
            LOG_INFO(
                "AX_BENCH_DET_LOCK_PROFILE count={} lock_ns={} initial_hold_ns={} postvpp_hold_ns={} "
                "postworker_wait_ns={} postworker_hold_ns={} final_wait_ns={} final_hold_ns={} vpp_ns={} "
                "ivps_wait_ns={} ivps_prelock_ns={} ivps_postlock_ns={} turn_ns={} turn_hold_ns={} "
                "turn_relock_ns={} npu_ns={} invalidate_ns={} worker_ns={} worker_queue_ns={} "
                "worker_lock_ns={}",
                prof.count, prof.lock_ns, prof.initial_hold_ns, prof.postvpp_hold_ns, prof.postworker_wait_ns,
                prof.postworker_hold_ns, prof.final_wait_ns, prof.final_hold_ns, prof.vpp_ns,
                prof.ivps_wait_ns, prof.ivps_prelock_ns, prof.ivps_postlock_ns, prof.turn_ns,
                prof.turn_hold_ns, prof.turn_relock_ns, prof.npu_ns, prof.invalidate_ns, prof.worker_ns,
                prof.worker_queue_ns, prof.worker_lock_ns);
        if (borrowed_output && !borrowed_result_ok)
            return false;
        try {
            if (legacy && legacy_status == 0) {
                // Reproduce the historical coordinate normalization check,
                // then map the 640-square letterbox into original coordinates.
                bool normalized = true;
                for (int i = 0; i < 20 && normalized; ++i)
                    if (keep[i] > .5f)
                        for (int j = 0; j < 4; ++j)
                            if (rows[i * 6 + j] > 1.f)
                                normalized = false;
                const float scale = std::min(640.f / p->source->width, 640.f / p->source->height);
                const int rw      = std::max(1, int(p->source->width * scale)),
                          rh      = std::max(1, int(p->source->height * scale));
                const float sx = float(rw) / p->source->width, sy = float(rh) / p->source->height;
                const float px = std::floor((640 - rw) / 2.f), py = std::floor((640 - rh) / 2.f);
                result.clear();
                result.reserve(32);
                for (std::size_t i = 0; i < 32; ++i)
                    if (keep[i] > .5f) {
                        const float* r = &rows[i * 6];
                        const float cx = r[0] * (normalized ? 640.f : 1.f),
                                    cy = r[1] * (normalized ? 640.f : 1.f);
                        const float w  = r[2] * (normalized ? 640.f : 1.f),
                                    h  = r[3] * (normalized ? 640.f : 1.f);
                        result.push_back({std::clamp((cx - w * .5f - px) / sx, 0.f, float(p->source->width)),
                                          std::clamp((cy - h * .5f - py) / sy, 0.f, float(p->source->height)),
                                          std::clamp((cx + w * .5f - px) / sx, 0.f, float(p->source->width)),
                                          std::clamp((cy + h * .5f - py) / sy, 0.f, float(p->source->height)),
                                          r[4], i, 0});
                    }
            } else if (!borrowed_output)
                result = ::benchmark_ax::person(tensor.data(), 4 * 8400, tensor.data() + 4 * 8400, 80 * 8400,
                                                p->source->width, p->source->height);
        } catch (...) {
            result.clear();
            return false;
        }
        {
            std::lock_guard<std::mutex> lock(state.mutex);
            if (state.poisoned) {
                result.clear();
                return false;
            }
        }
        return true;
    }
};
}  // namespace cosmo::nn::axera::benchmark
#endif
