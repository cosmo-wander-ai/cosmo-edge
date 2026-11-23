#pragma once
#include <chrono>
#include <cstdio>

#include "nn/device/axera/benchmark/BenchEngineWorker.h"
#include "nn/device/axera/benchmark/BenchHelmetExecutor.h"

namespace cosmo::bench_ax {
// Keep the SDK model's final owner and destructor on the creation worker.
// If scheduling teardown fails, deliberately retain the owner.
inline std::shared_ptr<VerifiedHelmetModel> LoadCentralHelmet(const void* bytes, std::size_t size) {
    auto* owner = BenchmarkEngineWorker().Invoke(true, [&] {
        auto model = LoadHelmetModel(bytes, size);
        return model ? new std::shared_ptr<VerifiedHelmetModel>(std::move(model)) : nullptr;
    });
    if (!owner)
        return {};
    return std::shared_ptr<VerifiedHelmetModel>(owner->get(), [owner](VerifiedHelmetModel*) noexcept {
        try {
            BenchmarkEngineWorker().Invoke(true, [owner] { delete owner; });
        } catch (...) { /* Preserve the worker-owned model if teardown is unavailable. */
        }
    });
}

// Caller performs source validation, ROI snapshot and CPU resize before it
// enters the central queue. Only the central worker borrows the model handle.
inline HelmetResult RunCentralHelmet(std::unique_ptr<helmet_candidate::PendingResources> pending,
                                     const std::vector<std::array<std::int32_t, 4>>& rois) {
    using namespace helmet_candidate;
    HelmetResult result;
    using Clock = std::chrono::steady_clock;
    struct Profile {
        std::uint64_t groups{}, rois{}, snapshot_ns{}, snapshot_wait_ns{}, snapshot_hold_ns{}, resize_ns{},
            write_ns{}, queue_ns{}, worker_ns{}, npu_ns{}, guard_pre_callback_ns{}, guard_callback_ns{},
            guard_post_callback_ns{};
    };
    static thread_local Profile profile;
    const char* trace_value = std::getenv("COSMO_AXERA_BENCH_LOCK_PROFILE");
    const bool trace        = trace_value && std::strcmp(trace_value, "1") == 0;
    auto elapsed            = [](Clock::time_point start) {
        return static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count());
    };
    if (!pending || rois.empty() || rois.size() > 64 || !pending->frame || !pending->frame->Valid() ||
        !pending->frame->frame_witness || !pending->frame->frame_witness->Matches(*pending->frame) ||
        !pending->frame_lease || !pending->model || !pending->io_owner || !pending->mapping_owner ||
        !pending->sdk || !pending->task_permit || !pending->model_permit)
        return result;
    auto model        = std::dynamic_pointer_cast<VerifiedHelmetModel>(pending->model);
    auto sdk          = std::dynamic_pointer_cast<HelmetSdk>(pending->sdk);
    auto task         = std::dynamic_pointer_cast<ManagedPermit<TaskTag>>(pending->task_permit);
    auto model_permit = std::dynamic_pointer_cast<ManagedPermit<ModelTag>>(pending->model_permit);
    if (!model || !sdk || !task || !model_permit || !task->permit.TryBegin() ||
        !model_permit->permit.TryBegin())
        return result;
    const auto& f = *pending->frame;
    constexpr AX_U32 sizes[]{2211840, 1105920};
    if (f.width != 1920 || f.height != 1080 || f.crop_x || f.crop_y ||
        f.format != media::NativeVideoBufferFormat::NV12 ||
        f.memory_kind != media::NativeVideoMemoryKind::AxeraPhysical ||
        f.color_space != media::NativeVideoColorSpace::Bt709 ||
        f.color_range != media::NativeVideoColorRange::Limited || f.planes[0].stride != 2048 ||
        f.planes[1].stride != 2048 || f.planes[0].bytes != sizes[0] || f.planes[1].bytes != sizes[1])
        return result;
    for (const auto& r : rois)
        if (r[0] < 0 || r[1] < 0 || r[2] <= r[0] || r[3] <= r[1] || r[2] > 1920 || r[3] > 1080)
            return result;

    // The request owns the source lease, witness, mappings and both permits.
    // Keep it anchored while the CPU reads the mapped source without the
    // process-wide model-operation lock, as in the original NV12 ROI route.
    const auto snapshot_start = trace ? Clock::now() : Clock::time_point{};
    auto anchor               = std::make_unique<std::unique_ptr<PendingResources>>(std::move(pending));
    auto& p                   = **anchor;
    std::vector<std::vector<std::uint8_t>> inputs(rois.size());
    std::vector<std::uint8_t> scratch;
    std::uint64_t snapshot_wait_ns = 0, snapshot_hold_ns = 0, resize_ns = 0;
    {
        auto& state           = nn::axera::detail::NativeState();
        const auto lock_start = trace ? Clock::now() : Clock::time_point{};
        std::unique_lock<std::mutex> lock(state.mutex);
        auto locked_at = trace ? Clock::now() : Clock::time_point{};
        if (trace)
            snapshot_wait_ns = static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(locked_at - lock_start).count());
        if (state.poisoned) {
            result.result = GuardResult::Poisoned;
            return result;
        }
        bool known = false;
        try {
            auto proof = VerifyHelmetPoolBlock(p.frame, *sdk);
            if (!proof || !proof->Matches(p.frame)) {
                known         = true;
                result.result = GuardResult::KnownFailure;
            } else {
                for (int n = 0; n < 2; ++n) {
                    p.mapped_planes[n] = sdk->Map(f.planes[n].physical_address, sizes[n]);
                    if (!p.mapped_planes[n] || p.mapped_planes[n] == reinterpret_cast<void*>(-1))
                        throw std::runtime_error("map failed");
                    p.mapped_owned[n] = true;
                }
                Nv12View view{static_cast<const std::uint8_t*>(p.mapped_planes[0]),
                              static_cast<const std::uint8_t*>(p.mapped_planes[1]),
                              sizes[0],
                              sizes[1],
                              1920,
                              1080,
                              2048,
                              2048};
                if (trace)
                    snapshot_hold_ns += elapsed(locked_at);
                lock.unlock();
#ifdef COSMO_AXERA_BENCH_TEST_ROI_PAUSE
                BenchTestRoiPause();
#endif
                const auto resize_start = trace ? Clock::now() : Clock::time_point{};
                bool converted          = true;
                for (std::size_t i = 0; i < rois.size(); ++i) {
                    inputs[i].resize(150528);
                    const auto& r = rois[i];
                    const Rect roi{r[0], r[1], r[2] - r[0], r[3] - r[1]};
                    Mapping mapping;
                    if (!(model->IsLegacy()
                              ? LegacyHelmetConvert(view, roi, inputs[i].data(), inputs[i].size(), scratch)
                              : Convert(view, roi, 224, 224, Resize::Stretch, inputs[i].data(),
                                        inputs[i].size(), mapping))) {
                        converted = false;
                        break;
                    }
                }
                if (trace)
                    resize_ns = elapsed(resize_start);
                const auto relock_start = trace ? Clock::now() : Clock::time_point{};
                lock.lock();
                locked_at = trace ? Clock::now() : Clock::time_point{};
                if (trace)
                    snapshot_wait_ns += static_cast<std::uint64_t>(
                        std::chrono::duration_cast<std::chrono::nanoseconds>(locked_at - relock_start)
                            .count());
                if (state.poisoned || !proof->Matches(p.frame)) {
                    state.poisoned = true;
                    (void)anchor.release();
                    result.result = GuardResult::CompletionUnknown;
                    return result;
                }
                for (int n = 1; n >= 0; --n) {
                    if (sdk->Unmap(p.mapped_planes[n], sizes[n]) != 0)
                        throw std::runtime_error("unmap failed");
                    p.mapped_planes[n] = nullptr;
                    p.mapped_owned[n]  = false;
                }
                known = true;
                if (!converted)
                    result.result = GuardResult::KnownFailure;
            }
        } catch (...) {
            if (!lock.owns_lock()) {
                try {
                    lock.lock();
                } catch (...) {
                    (void)anchor.release();
                    result.result = GuardResult::CompletionUnknown;
                    return result;
                }
            }
            state.poisoned = true;
            (void)anchor.release();
            result.result = GuardResult::CompletionUnknown;
            return result;
        }
        if (!known) {
            state.poisoned = true;
            (void)anchor.release();
            result.result = GuardResult::CompletionUnknown;
            return result;
        }
        if (result.result == GuardResult::KnownFailure)
            return result;
        if (trace)
            snapshot_hold_ns += elapsed(locked_at);
    }
    const auto snapshot_ns = trace ? elapsed(snapshot_start) : 0;
    auto list              = model->CentralList();
    auto io_owner          = std::dynamic_pointer_cast<HelmetIoOwner>(p.io_owner);
    if (!list || !io_owner) {
        result.result = GuardResult::KnownFailure;
        return result;
    }
    auto frame_lock = list->FrameLock();
    try {
        if (!list->Ensure(inputs.size())) {
            (void)anchor.release();
            result.result = GuardResult::CompletionUnknown;
            return result;
        }
    } catch (...) {
        (void)anchor.release();
        result.result = GuardResult::CompletionUnknown;
        return result;
    }
    io_owner->central_list = list;
    const auto write_start = trace ? Clock::now() : Clock::time_point{};
    for (std::size_t i = 0; i < inputs.size(); ++i) {
        auto& slot = list->At(i);
        std::memcpy(slot.input.host, inputs[i].data(), 150528);
        std::memset(slot.output.host, 0, 8);
    }
    const auto write_ns = trace ? elapsed(write_start) : 0;
    auto owned          = std::move(*anchor);
    anchor.reset();
    std::uint64_t queue_ns = 0, npu_ns = 0, guard_pre_callback_ns = 0, guard_callback_ns = 0,
                  guard_post_callback_ns = 0;
    const auto worker_start              = trace ? Clock::now() : Clock::time_point{};
    try {
        result = BenchmarkEngineWorker().Invoke(
            true, [owned = std::move(owned), count = inputs.size(), sdk, task, model_permit, list, trace,
                   worker_start, &queue_ns, &npu_ns, &guard_pre_callback_ns, &guard_callback_ns,
                   &guard_post_callback_ns, elapsed]() mutable {
                if (trace)
                    queue_ns = elapsed(worker_start);
                HelmetResult out;
                owned->owner_thread = std::this_thread::get_id();
                owned->handle       = owned->model->HandleForOwnerThread();
                if (!owned->handle || !task->permit.StillCurrent() || !model_permit->permit.StillCurrent()) {
                    out.result = GuardResult::KnownFailure;
                    return out;
                }
                auto worker_model = std::dynamic_pointer_cast<VerifiedHelmetModel>(owned->model);
                auto io_owner     = std::dynamic_pointer_cast<HelmetIoOwner>(owned->io_owner);
                if (!worker_model || !io_owner) {
                    out.result = GuardResult::KnownFailure;
                    return out;
                }
                const auto guard_start = trace ? Clock::now() : Clock::time_point{};
                Clock::time_point guard_callback_end{};
                out.result = PendingResourceGuard::Run(
                    std::move(owned),
                    [&](PendingResources& q) {
                        const auto callback_start = trace ? Clock::now() : Clock::time_point{};
                        if (trace)
                            guard_pre_callback_ns = elapsed(guard_start);
                        AX_ENGINE_IO_INFO_T* info = nullptr;
                        if (sdk->Info(q.handle, &info) != 0)
                            return DeviceOperation::CompletionUnknown;
                        if (!HelmetMetadata(info))
                            return DeviceOperation::KnownFailure;
                        constexpr AX_U32 bytes[]{150528, 8};
                        q.io.pInputs     = q.inputs.data();
                        q.io.nInputSize  = 1;
                        q.io.pOutputs    = q.outputs.data();
                        q.io.nOutputSize = 1;
                        for (std::size_t i = 0; i < count; ++i) {
                            if (!task->permit.StillCurrent() || !model_permit->permit.StillCurrent() ||
                                !q.frame->frame_witness->Matches(*q.frame))
                                return DeviceOperation::KnownFailure;
                            auto& slot               = list->At(i);
                            q.inputs[0].phyAddr      = slot.input.physical;
                            q.inputs[0].pVirAddr     = slot.input.host;
                            q.inputs[0].nSize        = bytes[0];
                            q.inputs[0].pStride      = io_owner->in_stride.data();
                            q.inputs[0].nStrideSize  = 4;
                            q.outputs[0].phyAddr     = slot.output.physical;
                            q.outputs[0].pVirAddr    = slot.output.host;
                            q.outputs[0].nSize       = bytes[1];
                            q.outputs[0].pStride     = io_owner->out_stride.data();
                            q.outputs[0].nStrideSize = 2;
                            const auto npu_start     = trace ? Clock::now() : Clock::time_point{};
                            const auto rc            = sdk->Run(q.handle, &q.io);
                            if (trace)
                                npu_ns += elapsed(npu_start);
                            if (rc != 0)
                                return DeviceOperation::CompletionUnknown;
                        }
                        if (trace) {
                            guard_callback_ns  = elapsed(callback_start);
                            guard_callback_end = Clock::now();
                        }
                        return DeviceOperation::Complete;
                    },
                    [&](PendingResources&) { return true; });
                if (trace && guard_callback_end != Clock::time_point{})
                    guard_post_callback_ns = elapsed(guard_callback_end);
                if (out.result == GuardResult::Complete) {
                    out.task_permit  = task;
                    out.model_permit = model_permit;
                } else
                    out.scores.clear();
                return out;
            });
    } catch (...) {
        result.result = GuardResult::CompletionUnknown;
    }
    const auto worker_ns = trace ? elapsed(worker_start) : 0;
    if (result.result == GuardResult::Complete) {
        for (std::size_t i = 0; i < inputs.size(); ++i) {
            std::array<float, 2> scores{};
            std::memcpy(scores.data(), list->At(i).output.host, 8);
            if (!std::all_of(scores.begin(), scores.end(),
                             [](float x) { return std::isfinite(x) && x >= 0.f && x <= 1.f; })) {
                result.result = GuardResult::KnownFailure;
                result.scores.clear();
                return result;
            }
            result.scores.push_back(scores);
        }
    }
    if (trace && result.result == GuardResult::Complete) {
        ++profile.groups;
        profile.rois += rois.size();
        profile.snapshot_ns += snapshot_ns;
        profile.resize_ns += resize_ns;
        profile.snapshot_wait_ns += snapshot_wait_ns;
        profile.snapshot_hold_ns += snapshot_hold_ns;
        profile.write_ns += write_ns;
        profile.queue_ns += queue_ns;
        profile.worker_ns += worker_ns;
        profile.npu_ns += npu_ns;
        profile.guard_pre_callback_ns += guard_pre_callback_ns;
        profile.guard_callback_ns += guard_callback_ns;
        profile.guard_post_callback_ns += guard_post_callback_ns;
        if (profile.groups == 1 || profile.groups % 64 == 0)
            std::fprintf(stderr,
                         "AX_BENCH_CENTRAL_HELMET_PROFILE thread=%zu groups=%llu rois=%llu snapshot_ns=%llu "
                         "snapshot_wait_ns=%llu snapshot_hold_ns=%llu resize_ns=%llu write_ns=%llu "
                         "queue_ns=%llu worker_ns=%llu npu_ns=%llu guard_pre_callback_ns=%llu "
                         "guard_callback_ns=%llu guard_post_callback_ns=%llu\n",
                         std::hash<std::thread::id>{}(std::this_thread::get_id()),
                         static_cast<unsigned long long>(profile.groups),
                         static_cast<unsigned long long>(profile.rois),
                         static_cast<unsigned long long>(profile.snapshot_ns),
                         static_cast<unsigned long long>(profile.snapshot_wait_ns),
                         static_cast<unsigned long long>(profile.snapshot_hold_ns),
                         static_cast<unsigned long long>(profile.resize_ns),
                         static_cast<unsigned long long>(profile.write_ns),
                         static_cast<unsigned long long>(profile.queue_ns),
                         static_cast<unsigned long long>(profile.worker_ns),
                         static_cast<unsigned long long>(profile.npu_ns),
                         static_cast<unsigned long long>(profile.guard_pre_callback_ns),
                         static_cast<unsigned long long>(profile.guard_callback_ns),
                         static_cast<unsigned long long>(profile.guard_post_callback_ns));
    }
    return result;
}
}  // namespace cosmo::bench_ax
