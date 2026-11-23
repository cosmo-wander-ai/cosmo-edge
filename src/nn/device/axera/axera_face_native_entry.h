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
#include "nn/device/axera/axera_face_model_binding.h"
#include "nn/device/axera/axera_raw_yuv_executor.h"

namespace cosmo::nn::axera {
struct FaceFrameKey {
    std::string channel_id;
    std::uint64_t stream_generation{0};
    std::uint64_t frame_index{0};
    std::int64_t timestamp{0};
};
enum class FaceEntryError {
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
struct FaceEntryResult {
    FaceEntryError error{FaceEntryError::Closed};
    std::vector<AiDetectRstEl> targets;
};

// Internal entry created from an admitted model capability and a detector
// registration. A frame is executable only with its decoder-minted witness,
// matching queue/stream identity and the runner's explicit device-I/O checks.
class FaceNativeEntry {
    struct Identity {};

public:
    class Ticket {
        friend class FaceNativeEntry;
        Ticket(std::weak_ptr<const Identity> id, std::uint64_t generation)
            : id_(std::move(id)), generation_(generation) {}
        std::weak_ptr<const Identity> id_;
        std::uint64_t generation_{0};
    };
    FaceNativeEntry(std::shared_ptr<const face_binding::Capability> capability,
                    std::shared_ptr<const void> detector_identity, std::uint64_t task_generation,
                    std::string atomic_code, std::string scene_id);
    Ticket GetTicket() const;
    void Invalidate();
    FaceEntryResult Execute(const Ticket& ticket, const FaceFrameKey& key, media::NativeVideoBufferPtr source,
                            float threshold);
#ifdef COSMO_AXERA_FACE_TEST_SEAM
    // Only compiled in isolated tests, never in the production target.
    using TestProof = std::function<bool(const FaceFrameKey&, const media::NativeVideoBuffer&)>;
    using TestRun   = std::function<RawYuvResult(const std::shared_ptr<const face_binding::Capability>&,
                                                 media::NativeVideoBufferPtr, float)>;
    void SetTestSeam(TestProof proof, TestRun run) {
        proof_ = std::move(proof);
        run_   = std::move(run);
    }
#endif
private:
    std::shared_ptr<const face_binding::Capability> capability_;
    std::shared_ptr<const void> detector_identity_;
    std::shared_ptr<const Identity> identity_;
    const std::uint64_t generation_;
    const std::string atomic_code_;
    const std::string scene_id_;
    const std::thread::id owner_thread_;
    std::mutex mutex_;
    bool active_{true};
#ifdef COSMO_AXERA_FACE_TEST_SEAM
    TestProof proof_;
    TestRun run_;
#endif
};
}  // namespace cosmo::nn::axera
#endif
