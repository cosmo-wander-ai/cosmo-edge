#pragma once
// Isolated resource-lifetime building block; it does not authorize a graph,
// validate model metadata, map memory, invoke the SDK or publish results.
#include <array>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>

#include "ax_engine_api.h"
#include "media/NativeVideoBuffer.h"
#include "nn/device/axera/axera_native_execution_state.h"
#include "util/HelmetLifecycleGate.h"

namespace cosmo::helmet_candidate {
// In the future these interfaces must be implemented by the actual trusted
// loader, allocator, mapper, SDK and task/model lifecycle. Fakes prove only
// lifetime mechanics, not that production ownership implementations are valid.
struct ModelOwner {
    virtual ~ModelOwner()                                    = default;
    virtual AX_ENGINE_HANDLE HandleForOwnerThread() noexcept = 0;
};
struct IoOwner {
    virtual ~IoOwner() = default;
};
struct MappingOwner {
    virtual ~MappingOwner() = default;
};
struct SdkOwner {
    virtual ~SdkOwner() = default;
};
struct Block {
    AX_U64 physical{};
    void* host{};
    AX_U32 bytes{};
};
struct PendingResources final {
    media::NativeVideoBufferPtr frame;
    std::shared_ptr<FrameLeaseOwner> frame_lease;
    std::shared_ptr<ModelOwner> model;
    std::shared_ptr<IoOwner> io_owner;
    std::shared_ptr<MappingOwner> mapping_owner;
    std::shared_ptr<SdkOwner> sdk;
    std::shared_ptr<PermitOwner> task_permit, model_permit;
    AX_ENGINE_HANDLE handle{};
    std::array<Block, 3> io_blocks{};  // ROI, valid, 5 FP32 scores
    std::array<void*, 2> mapped_planes{};
    std::array<bool, 2> mapped_owned{};
    std::array<AX_S32, 2> y_stride{{2048, 1}}, uv_stride{{2048, 1}}, scores_stride{{20, 4}};
    std::array<AX_S32, 1> roi_stride{{4}}, valid_stride{{1}};
    std::array<AX_ENGINE_IO_BUFFER_T, 3> inputs{};
    std::array<AX_ENGINE_IO_BUFFER_T, 2> outputs{};
    AX_ENGINE_IO_T io{};
    std::thread::id owner_thread{};

private:
    friend class PendingResourceGuard;
    std::shared_ptr<PendingResources> keep_alive_;
};
// CompletionUnknown must be returned after any ambiguous device operation.
enum class DeviceOperation { Complete, KnownFailure, CompletionUnknown };
enum class GuardResult { Complete, KnownFailure, InvalidResources, Poisoned, CompletionUnknown };
class PendingResourceGuard final {
    static bool Plausible(const PendingResources& p) noexcept {
        if (!p.frame || !p.frame->Valid() || !p.frame->frame_witness ||
            !p.frame->frame_witness->Matches(*p.frame) || !p.model || !p.io_owner || !p.mapping_owner ||
            !p.sdk || !p.frame_lease || !p.task_permit || !p.model_permit || !p.handle ||
            p.owner_thread != std::this_thread::get_id())
            return false;
        return p.model->HandleForOwnerThread() == p.handle;
    }

public:
    // Before this call p must hold initialized owner objects but no SDK IO
    // allocation or mapping. Operation performs those inside the self-retained
    // p and records every partial acquisition before another SDK call. Operation
    // and cleanup execute under the real process-shared NativeState mutex. They
    // MUST only use resources already owned by p and must not acquire any
    // model/task/queue lock. Cleanup must explicitly unmap/free IO; false or an
    // exception means ownership is unknown, so every owner is retained forever.
    // No result publication occurs here. In particular, lifecycle authority and
    // the SDK operation must be atomically ordered by the actual caller.
    template <class Operation, class Cleanup>
    static GuardResult Run(std::unique_ptr<PendingResources> input, Operation&& op, Cleanup&& cleanup) {
        if (!input || !Plausible(*input))
            return GuardResult::InvalidResources;
        auto p       = std::shared_ptr<PendingResources>(std::move(input));
        auto& global = nn::axera::detail::NativeState();
        std::unique_lock<std::mutex> lock(global.mutex);
        if (global.poisoned)
            return GuardResult::Poisoned;
        p->keep_alive_     = p;  // established before any operation with uncertain ownership
        GuardResult result = GuardResult::CompletionUnknown;
        {
            struct Guard {
                nn::axera::detail::NativeExecutionState& state;
                std::shared_ptr<PendingResources>& p;
                bool known{false};
                ~Guard() {
                    if (known)
                        p->keep_alive_.reset();
                    else
                        state.poisoned = true;
                }
            } guard{global, p};
            try {
                auto status = std::forward<Operation>(op)(*p);
                if (status != DeviceOperation::CompletionUnknown && std::forward<Cleanup>(cleanup)(*p)) {
                    guard.known = true;
                    result      = status == DeviceOperation::Complete ? GuardResult::Complete
                                                                      : GuardResult::KnownFailure;
                }
            } catch (...) { /* Unknown completion; guard quarantines all resources. */
            }
        }
        // Last local owner drops only after the global lock is released.
        lock.unlock();
        return result;
    }
};
}  // namespace cosmo::helmet_candidate
