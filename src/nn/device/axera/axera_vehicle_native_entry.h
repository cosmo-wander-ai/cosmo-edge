#pragma once
#ifdef COSMO_NN_USE_AXERA_BACKEND
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "infer/AiCommon.h"
#include "media/NativeVideoBuffer.h"
#include "nn/device/axera/axera_raw_yuv_executor.h"
#include "nn/device/axera/axera_vehicle_model_binding.h"

namespace cosmo::nn::axera {
struct VehicleFrameKey {
    std::string channel_id;
    std::uint64_t stream_generation{0};
    std::uint64_t frame_index{0};
    std::int64_t timestamp{0};
};
enum class VehicleEntryError {
    None,
    Closed,
    InvalidTicket,
    InvalidFrame,
    UnverifiedProvenance,
    InvalidThreshold,
    ExecutionFailed,
    OutputCapacityExceeded,
    InvalidOutput
};
struct VehicleEntryResult {
    VehicleEntryError error{VehicleEntryError::Closed};
    std::vector<AiDetectRstEl> targets;
};

// Internal, detector-owned entry. It cannot authorize an unbound model or
// replace the decoder-minted witness, queue identity and leased native frame.
class VehicleNativeEntry {
    struct Identity {};

public:
    class Ticket {
        friend class VehicleNativeEntry;
        Ticket(std::weak_ptr<const Identity> id, std::uint64_t generation)
            : id_(std::move(id)), generation_(generation) {}
        std::weak_ptr<const Identity> id_;
        std::uint64_t generation_{0};
    };
    VehicleNativeEntry(std::shared_ptr<const vehicle_binding::Capability> capability,
                       std::shared_ptr<const void> detector_identity, std::uint64_t task_generation,
                       std::string atomic_code, std::string scene_id);
    Ticket GetTicket() const;
    void Invalidate();
    VehicleEntryResult Execute(const Ticket& ticket, const VehicleFrameKey& key,
                               media::NativeVideoBufferPtr source,
                               const std::array<vehicle_binding::ClassPolicy, 3> policies);
#ifdef COSMO_AXERA_VEHICLE_TEST_SEAM
    using TestProof = std::function<bool(const VehicleFrameKey&, const media::NativeVideoBuffer&)>;
    using TestRun   = std::function<RawYuvVehicleResult(
        const std::shared_ptr<const vehicle_binding::Capability>&, media::NativeVideoBufferPtr)>;
    void SetTestSeam(TestProof proof, TestRun run) {
        proof_ = std::move(proof);
        run_   = std::move(run);
    }
#endif
private:
    std::shared_ptr<const vehicle_binding::Capability> capability_;
    std::shared_ptr<const void> detector_identity_;
    std::shared_ptr<const Identity> identity_;
    const std::uint64_t generation_;
    const std::string atomic_code_;
    const std::string scene_id_;
    const std::thread::id owner_thread_;
    std::mutex mutex_;
    bool active_{true};
#ifdef COSMO_AXERA_VEHICLE_TEST_SEAM
    TestProof proof_;
    TestRun run_;
#endif
};
}  // namespace cosmo::nn::axera
#endif
