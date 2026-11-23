// AiDetectorUnify — Unified detector model wrapper.

#pragma once

#include <memory>

#include "infer/AiCommon.h"
#include "infer/AiComponment.h"
#include "media/VideoFrame.h"
#include "nn/utils/default_component.h"
#ifdef COSMO_NN_USE_AXERA_BACKEND
#include "flow/detect/NativeFaceRoute.h"
#endif

namespace cosmo {
#ifdef COSMO_NN_USE_AXERA_BACKEND
namespace nn::axera {
    class E24NativeInputPool;
    class E24NativeRunner;
}  // namespace nn::axera
#endif
class AiDetectorUnify {
public:
    AiDetectorUnify(const std::string& atomic_code, const std::string& json_path,
                    const std::string& model_path);
    ~AiDetectorUnify();

    util::ErrorEnum Init();
    util::ErrorEnum Init(std::optional<InferenceInputSize> input_size);

    util::ErrorEnum Detect(const std::vector<VideoFramePtr>& images, std::vector<AiConfidence> conf_thres,
                           std::vector<std::vector<AiDetectRstEl>>& results);
    util::ErrorEnum Detect(const std::vector<VideoFramePtr>& images,
                           const std::vector<media::NativeVideoBufferPtr>& native_buffers,
                           std::vector<AiConfidence> conf_thres,
                           std::vector<std::vector<AiDetectRstEl>>& results);

    std::vector<std::string> GetLabels() {
        return labels_;
    }

    util::ErrorEnum GetMaxBatchSize(size_t& value);

#if defined(COSMO_NN_USE_AXERA_BACKEND) && defined(COSMO_AXERA_FACE_TEST_SEAM)
    // This test seam is absent from production builds.
    std::shared_ptr<const NativeFaceRegistration> TestPrepareNativeFace(
        std::shared_ptr<nn::axera::face_binding::Binding> binding,
        std::shared_ptr<const nn::axera::face_binding::Capability> capability,
        std::shared_ptr<AlgDataQueue<AlgDataPtr>> queue, std::shared_ptr<AlgDataQueueDistributor> output,
        std::uint64_t generation, std::string scene) {
        return PrepareNativeFace(std::move(binding), std::move(capability), std::move(queue),
                                 std::move(output), generation, std::move(scene));
    }
    util::ErrorEnum TestDetectNativeFace(const std::shared_ptr<const NativeFaceEnvelope>& envelope,
                                         const VideoFramePtr& frame,
                                         const media::NativeVideoBufferPtr& native,
                                         const std::string& channel, float threshold,
                                         const RegistrationAuthority::Lease& lease,
                                         const AlgDataQueue<AlgDataPtr>::LifecycleLease& queue_lease,
                                         std::vector<AiDetectRstEl>& out) {
        return DetectNativeFace(envelope, frame, native, channel, threshold, lease, queue_lease, out);
    }
    void TestCloseNativeFace() {
        CloseNativeFace();
    }
    void TestSetNativeFaceSeam(nn::axera::FaceNativeEntry::TestProof proof,
                               nn::axera::FaceNativeEntry::TestRun run) {
        native_entry_->SetTestSeam(std::move(proof), std::move(run));
    }
#ifdef COSMO_AXERA_FIRE_TEST_SEAM
    std::shared_ptr<const NativeFaceRegistration> TestPrepareNativeFire(
        std::shared_ptr<nn::axera::fire_binding::Binding> binding,
        std::shared_ptr<const nn::axera::fire_binding::Capability> capability,
        std::shared_ptr<AlgDataQueue<AlgDataPtr>> queue, std::shared_ptr<AlgDataQueueDistributor> output,
        std::uint64_t generation, std::string scene) {
        return PrepareNativeFire(std::move(binding), std::move(capability), std::move(queue),
                                 std::move(output), generation, std::move(scene));
    }
    void TestSetNativeFireSeam(nn::axera::FireNativeEntry::TestProof proof,
                               nn::axera::FireNativeEntry::TestRun run) {
        fire_entry_->SetTestSeam(std::move(proof), std::move(run));
    }
    util::ErrorEnum TestDetectNativeFire(const std::shared_ptr<const NativeFaceEnvelope>& envelope,
                                         const VideoFramePtr& frame,
                                         const media::NativeVideoBufferPtr& native,
                                         const std::string& channel,
                                         const std::array<nn::axera::fire_binding::ClassPolicy, 2>& policies,
                                         const RegistrationAuthority::Lease& lease,
                                         const AlgDataQueue<AlgDataPtr>::LifecycleLease& queue_lease,
                                         std::vector<AiDetectRstEl>& out) {
        return DetectNativeFace(envelope, frame, native, channel, 0.f, lease, queue_lease, out, &policies);
    }
#endif
#ifdef COSMO_AXERA_VEHICLE_TEST_SEAM
    std::shared_ptr<const NativeFaceRegistration> TestPrepareNativeVehicle(
        std::shared_ptr<nn::axera::vehicle_binding::Binding> binding,
        std::shared_ptr<const nn::axera::vehicle_binding::Capability> capability,
        std::shared_ptr<AlgDataQueue<AlgDataPtr>> queue, std::shared_ptr<AlgDataQueueDistributor> output,
        std::uint64_t generation, std::string scene) {
        return PrepareNativeVehicle(std::move(binding), std::move(capability), std::move(queue),
                                    std::move(output), generation, std::move(scene));
    }
    void TestSetNativeVehicleSeam(nn::axera::VehicleNativeEntry::TestProof proof,
                                  nn::axera::VehicleNativeEntry::TestRun run) {
        vehicle_entry_->SetTestSeam(std::move(proof), std::move(run));
    }
    util::ErrorEnum TestDetectNativeVehicle(
        const std::shared_ptr<const NativeFaceEnvelope>& envelope, const VideoFramePtr& frame,
        const media::NativeVideoBufferPtr& native, const std::string& channel,
        const std::array<nn::axera::vehicle_binding::ClassPolicy, 3>& policies,
        const RegistrationAuthority::Lease& lease,
        const AlgDataQueue<AlgDataPtr>::LifecycleLease& queue_lease, std::vector<AiDetectRstEl>& out) {
        return DetectNativeFace(envelope, frame, native, channel, 0.f, lease, queue_lease, out, nullptr,
                                &policies);
    }
#endif
#ifdef COSMO_AXERA_PERSON_TEST_SEAM
    std::shared_ptr<const NativeFaceRegistration> TestPrepareNativePerson(
        std::shared_ptr<nn::axera::person_binding::Binding> binding,
        std::shared_ptr<const nn::axera::person_binding::Capability> capability,
        std::shared_ptr<AlgDataQueue<AlgDataPtr>> queue, std::shared_ptr<AlgDataQueueDistributor> output,
        std::uint64_t generation, std::string scene) {
        return PrepareNativePerson(std::move(binding), std::move(capability), std::move(queue),
                                   std::move(output), generation, std::move(scene));
    }
    void TestSetNativePersonSeam(nn::axera::PersonNativeEntry::TestProof proof,
                                 nn::axera::PersonNativeEntry::TestRun run) {
        person_entry_->SetTestSeam(std::move(proof), std::move(run));
    }
#endif
#endif

private:
    friend class AiDetector;
#ifdef COSMO_NN_USE_AXERA_BACKEND
    // Internal capability installation only. No config flag or model path
    // creates this route; the Binding must own the actual loaded capability.
    std::shared_ptr<const NativeFaceRegistration> PrepareNativeFace(
        std::shared_ptr<nn::axera::face_binding::Binding> binding,
        std::shared_ptr<const nn::axera::face_binding::Capability> capability,
        std::shared_ptr<AlgDataQueue<AlgDataPtr>> queue, std::shared_ptr<AlgDataQueueDistributor> output,
        std::uint64_t task_generation, std::string scene_id);
    std::shared_ptr<const NativeFaceRegistration> PrepareNativeE24(
        std::shared_ptr<const std::vector<std::uint8_t>> bytes,
        std::shared_ptr<AlgDataQueue<AlgDataPtr>> queue, std::shared_ptr<AlgDataQueueDistributor> output,
        std::uint64_t generation, const std::string& scene);
    util::ErrorEnum DetectNativeE24(const media::NativeVideoBufferPtr& source, float threshold,
                                    std::vector<AiDetectRstEl>& results);
    std::shared_ptr<const NativeFaceRegistration> PrepareBenchmarkPerson(
        std::unique_ptr<nn::axera::benchmark::PersonExecutor> executor,
        std::shared_ptr<AlgDataQueue<AlgDataPtr>> queue, std::shared_ptr<AlgDataQueueDistributor> output,
        std::uint64_t task_generation, std::string scene_id);
    std::shared_ptr<const NativeFaceRegistration> PrepareNativePerson(
        std::shared_ptr<nn::axera::person_binding::Binding> binding,
        std::shared_ptr<const nn::axera::person_binding::Capability> capability,
        std::shared_ptr<AlgDataQueue<AlgDataPtr>> queue, std::shared_ptr<AlgDataQueueDistributor> output,
        std::uint64_t task_generation, std::string scene_id, bool trusted_person_template = false);
    std::shared_ptr<const NativeFaceRegistration> PrepareNativeFire(
        std::shared_ptr<nn::axera::fire_binding::Binding> binding,
        std::shared_ptr<const nn::axera::fire_binding::Capability> capability,
        std::shared_ptr<AlgDataQueue<AlgDataPtr>> queue, std::shared_ptr<AlgDataQueueDistributor> output,
        std::uint64_t task_generation, std::string scene_id);
    std::shared_ptr<const NativeFaceRegistration> PrepareNativeVehicle(
        std::shared_ptr<nn::axera::vehicle_binding::Binding> binding,
        std::shared_ptr<const nn::axera::vehicle_binding::Capability> capability,
        std::shared_ptr<AlgDataQueue<AlgDataPtr>> queue, std::shared_ptr<AlgDataQueueDistributor> output,
        std::uint64_t task_generation, std::string scene_id);
    util::ErrorEnum DetectNativeFace(
        const std::shared_ptr<const NativeFaceEnvelope>& envelope, const VideoFramePtr& metadata,
        const media::NativeVideoBufferPtr& source, const std::string& channel_id, float threshold,
        const RegistrationAuthority::Lease& lease,
        const AlgDataQueue<AlgDataPtr>::LifecycleLease& queue_lease, std::vector<AiDetectRstEl>& results,
        const std::array<nn::axera::fire_binding::ClassPolicy, 2>* fire_policies       = nullptr,
        const std::array<nn::axera::vehicle_binding::ClassPolicy, 3>* vehicle_policies = nullptr);
    void CloseNativeFace();
    std::unique_ptr<nn::axera::E24NativeInputPool> e24_inputs_;
    std::unique_ptr<nn::axera::E24NativeRunner> e24_runner_;
    std::shared_ptr<nn::axera::face_binding::Binding> native_binding_;
    std::shared_ptr<const nn::axera::face_binding::Capability> native_capability_;
    std::shared_ptr<const void> native_identity_;
    std::shared_ptr<nn::axera::FaceNativeEntry> native_entry_;
    std::shared_ptr<nn::axera::person_binding::Binding> person_binding_;
    std::shared_ptr<const nn::axera::person_binding::Capability> person_capability_;
    std::shared_ptr<nn::axera::PersonNativeEntry> person_entry_;
    std::shared_ptr<nn::axera::benchmark::PersonEntry> bench_entry_;
    std::shared_ptr<nn::axera::fire_binding::Binding> fire_binding_;
    std::shared_ptr<const nn::axera::fire_binding::Capability> fire_capability_;
    std::shared_ptr<nn::axera::FireNativeEntry> fire_entry_;
    std::shared_ptr<nn::axera::vehicle_binding::Binding> vehicle_binding_;
    std::shared_ptr<const nn::axera::vehicle_binding::Capability> vehicle_capability_;
    std::shared_ptr<nn::axera::VehicleNativeEntry> vehicle_entry_;
    std::shared_ptr<const NativeFaceRegistration> native_registration_;
#endif
    util::ErrorEnum Forward(const std::vector<VideoFramePtr>& images,
                            const std::vector<media::NativeVideoBufferPtr>& native_buffers,
                            std::vector<std::vector<AiDetectRstEl>>& results);

    util::ErrorEnum ConvertResults(const std::vector<nn::ObjectInfoV1>& objects, int width, int height,
                                   std::vector<AiDetectRstEl>& result) const;

    size_t max_batch_size_        = 1;
    bool joint_person_attributes_ = false;
    std::string atomic_code_;
    std::string cfg_path_;
    std::string model_path_;
    std::vector<std::string> labels_;
    std::unique_ptr<cosmo::nn::DefaultComponent> detector_;
};

using AiDetectorUnifyPtr = std::shared_ptr<AiDetectorUnify>;
}  // namespace cosmo
