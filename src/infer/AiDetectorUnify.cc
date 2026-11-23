// AiDetectorUnify — Unified detection engine wrapper.

#include "infer/AiDetectorUnify.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <exception>
#include <iterator>
#include <utility>

#include "infer/DetectorBatchResults.h"
#include "nn/core/inference_pipeline_metrics.h"
#include "nn/device/axera/axera_route_identity.h"
#include "util/Log.h"
#include "util/UuidUtil.h"
#ifdef COSMO_NN_USE_AXERA_BACKEND
#include "infer/NativePersonIdentity.h"
#include "nn/device/axera/E24NativeRunner.h"
#include "nn/device/axera/axera_face_model_sdk_loader.h"
#include "nn/node/yolo_e2e_decode_node.h"
#include "nn/utils/net_utils.h"
#include "nn/utils/op.h"
#endif

namespace cosmo {
namespace {
    using MetricsClock = std::chrono::steady_clock;

    uint64_t ElapsedNanoseconds(MetricsClock::time_point started_at) {
        return static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(MetricsClock::now() - started_at).count());
    }
}  // namespace

AiDetectorUnify::AiDetectorUnify(const std::string& atomic_code, const std::string& json_path,
                                 const std::string& model_path)
    : atomic_code_(atomic_code), cfg_path_(json_path), model_path_(model_path) {}

AiDetectorUnify::~AiDetectorUnify() {
    LOG_INFO("{}", "AiDetectorUnify Delete");
}

#ifdef COSMO_NN_USE_AXERA_BACKEND
std::shared_ptr<const NativeFaceRegistration> AiDetectorUnify::PrepareNativeFace(
    std::shared_ptr<nn::axera::face_binding::Binding> binding,
    std::shared_ptr<const nn::axera::face_binding::Capability> capability,
    std::shared_ptr<AlgDataQueue<AlgDataPtr>> queue, std::shared_ptr<AlgDataQueueDistributor> output,
    std::uint64_t task_generation, std::string scene_id) {
    if (detector_ || native_entry_ || person_entry_ || fire_entry_ || vehicle_entry_ || !binding ||
        !capability || !binding->Owns(capability) || !queue || !output || !task_generation ||
        atomic_code_.empty() || scene_id.empty() || scene_id != capability->GetContract().scene_id)
        return nullptr;
    const auto lifecycle = queue->Lifecycle();
    if (!lifecycle.running || !lifecycle.generation)
        return nullptr;
    auto execution = capability->AcquireForExecution();
    if (!execution)
        return nullptr;
    auto model = std::dynamic_pointer_cast<nn::axera::face_binding::SdkLoadedModel>(execution.Model());
    if (!model || !model->HandleForOwnerThread())
        return nullptr;
    try {
        std::vector<std::string> labels{"face"};
        auto identity = std::make_shared<int>(0);
        auto entry    = std::make_shared<nn::axera::FaceNativeEntry>(capability, identity, task_generation,
                                                                     atomic_code_, std::move(scene_id));
        auto registration = std::shared_ptr<const NativeFaceRegistration>(
            new NativeFaceRegistration(entry, identity, std::move(queue), std::move(output)));
        if (!registration->queue_lifecycle_.running || !registration->queue_lifecycle_.generation)
            return nullptr;
        native_binding_      = std::move(binding);
        native_capability_   = std::move(capability);
        native_identity_     = std::move(identity);
        native_entry_        = std::move(entry);
        native_registration_ = registration;
        labels_              = std::move(labels);
        max_batch_size_      = 1;
        return registration;
    } catch (...) {
        return nullptr;
    }
}

std::shared_ptr<const NativeFaceRegistration> AiDetectorUnify::PrepareNativeE24(
    std::shared_ptr<const std::vector<std::uint8_t>> bytes, std::shared_ptr<AlgDataQueue<AlgDataPtr>> queue,
    std::shared_ptr<AlgDataQueueDistributor> output, std::uint64_t generation, const std::string& scene) {
    if (detector_ || native_registration_ || !queue || !output || !generation ||
        !nn::axera::route_identity::MatchesPersonAttributesRoute(atomic_code_, scene))
        return {};
    auto identity     = std::make_shared<int>(0);
    auto registration = std::shared_ptr<const NativeFaceRegistration>(
        new NativeFaceRegistration(NativeFaceRegistration::E24{}, identity, queue, output));
    if (!registration->queue_lifecycle_.running || !registration->queue_lifecycle_.generation)
        return {};
    auto runner = std::make_unique<nn::axera::E24NativeRunner>();
    if (!runner->Load(std::move(bytes)))
        return {};
    e24_inputs_              = std::make_unique<nn::axera::E24NativeInputPool>(1);
    e24_runner_              = std::move(runner);
    labels_                  = {"person"};
    joint_person_attributes_ = true;
    native_identity_         = std::move(identity);
    native_registration_     = registration;
    return registration;
}

util::ErrorEnum AiDetectorUnify::DetectNativeE24(const media::NativeVideoBufferPtr& source, float threshold,
                                                 std::vector<AiDetectRstEl>& results) {
    std::shared_ptr<nn::axera::E24NativeInput> input;
    const auto ivps_start = MetricsClock::now();
    nn::axera::E24IvpsTiming ivps_timing;
    const auto prepared = e24_inputs_->Prepare(source, input, &ivps_timing);
    const auto ivps_ns  = ElapsedNanoseconds(ivps_start);
    if (prepared != nn::axera::E24NativePrepareResult::Ready) {
        LOG_WARN("AX E24 native frame preparation failed, status:{}", static_cast<int>(prepared));
        return util::ErrorEnum::AI_FORWARD_FAILED;
    }
    nn::axera::E24NativeRunner::Output rows;
    int sdk_error        = 0;
    const auto npu_start = MetricsClock::now();
    const auto ok        = e24_runner_->Forward(input, rows, &sdk_error);
    const auto npu_ns    = ElapsedNanoseconds(npu_start);
    nn::GetInferencePipelineMetrics().RecordGraphForward(npu_ns, 1, ok);
    if (!ok) {
        LOG_ERRO("AX E24 native inference failed, AX error:{}", sdk_error);
        return util::ErrorEnum::AI_FORWARD_FAILED;
    }
    static std::atomic<std::uint64_t> forwards{0};
    const auto count = forwards.fetch_add(1, std::memory_order_relaxed) + 1;
    if (count == 1 || count % 256 == 0)
        LOG_INFO("AX E24 native forward count:{} IVPS ns:{} NPU ns:{} IVPS wait ns:{} IVPS resize ns:{}",
                 count, ivps_ns, npu_ns, ivps_timing.wait_ns, ivps_timing.resize_ns);

    const auto parse_start = MetricsClock::now();
    nn::BlobDesc desc;
    desc.dims = {1, 300, 10};
    nn::BlobHandle raw;
    raw.base = rows.data();
    std::vector<std::shared_ptr<nn::Blob>> incoming{std::make_shared<nn::Blob>(desc, raw)};
    nn::axera::E24NativeRunner::Output decoded_rows;
    nn::BlobHandle packed;
    packed.base = decoded_rows.data();
    std::vector<std::shared_ptr<nn::Blob>> decoded{std::make_shared<nn::Blob>(desc, packed)};
    nn::YoloPost post("yolo26_person_attributes_postprocess");
    post.nms_detection_conf = 0.25f;
    post.top_k              = 300;
    nn::YoloE2EDecodeNode decoder;
    decoder.LoadParam(&post);
    const nn::Size image{source->width, source->height};
    const nn::Size input_size{static_cast<int>(nn::axera::kE24ImageWidth),
                              static_cast<int>(nn::axera::kE24ImageHeight)};
    std::vector<std::vector<nn::ObjectInfoV1>> outputs;
    const bool parsed = decoder.Forward(incoming, decoded) &&
                        nn::NetUtils::ParsePersonAttributesOutput(decoded, {image}, input_size, threshold,
                                                                  "person", outputs) &&
                        outputs.size() == 1;
    nn::GetInferencePipelineMetrics().RecordResultParse(ElapsedNanoseconds(parse_start), 1, parsed);
    if (!parsed)
        return util::ErrorEnum::AI_PARSE_OUTPUT_FAILED;
    return ConvertResults(outputs[0], image.width, image.height, results);
}

std::shared_ptr<const NativeFaceRegistration> AiDetectorUnify::PrepareBenchmarkPerson(
    std::unique_ptr<nn::axera::benchmark::PersonExecutor> executor,
    std::shared_ptr<AlgDataQueue<AlgDataPtr>> queue, std::shared_ptr<AlgDataQueueDistributor> output,
    std::uint64_t generation, std::string scene) {
    if (detector_ || native_entry_ || person_entry_ || fire_entry_ || vehicle_entry_ || bench_entry_ ||
        e24_runner_ || !executor || !queue || !output || !generation || atomic_code_ != "9275710" ||
        (scene != "930927510" && scene != "930007463"))
        return {};
    const auto lifecycle = queue->Lifecycle();
    if (!lifecycle.running || !lifecycle.generation)
        return {};
    auto identity     = std::make_shared<int>(0);
    auto entry        = std::make_shared<nn::axera::benchmark::PersonEntry>(std::move(executor), identity,
                                                                            generation, atomic_code_, scene);
    auto registration = std::shared_ptr<const NativeFaceRegistration>(
        new NativeFaceRegistration(entry, identity, queue, output));
    if (!registration->queue_lifecycle_.running || !registration->queue_lifecycle_.generation)
        return {};
    bench_entry_         = std::move(entry);
    native_identity_     = std::move(identity);
    native_registration_ = registration;
    labels_              = {"Pedestrian"};
    max_batch_size_      = 1;
    return registration;
}

std::shared_ptr<const NativeFaceRegistration> AiDetectorUnify::PrepareNativePerson(
    std::shared_ptr<nn::axera::person_binding::Binding> binding,
    std::shared_ptr<const nn::axera::person_binding::Capability> capability,
    std::shared_ptr<AlgDataQueue<AlgDataPtr>> queue, std::shared_ptr<AlgDataQueueDistributor> output,
    std::uint64_t task_generation, std::string scene_id, bool trusted_person_template) {
    if (detector_ || native_entry_ || person_entry_ || fire_entry_ || vehicle_entry_ || !binding ||
        !capability || !binding->Owns(capability) || !queue || !output || !task_generation ||
        scene_id.empty() || scene_id != capability->GetContract().scene_id)
        return nullptr;
    const auto& contract = capability->GetContract();
    const char* label =
        native_person_identity::Select(atomic_code_, contract.semantic_contract, contract.model_sha256);
    if (!label)
        return nullptr;
    const auto lifecycle = queue->Lifecycle();
    if (!lifecycle.running || !lifecycle.generation)
        return nullptr;
    auto execution = capability->AcquireForExecution();
    if (!execution)
        return nullptr;
    auto model = std::dynamic_pointer_cast<nn::axera::face_binding::SdkLoadedModel>(execution.Model());
    if (!model || !model->HandleForOwnerThread())
        return nullptr;
    try {
        std::vector<std::string> labels{label};
        auto identity = std::make_shared<int>(0);
        auto entry    = std::make_shared<nn::axera::PersonNativeEntry>(capability, identity, task_generation,
                                                                       atomic_code_, std::move(scene_id),
                                                                       trusted_person_template);
        auto registration = std::shared_ptr<const NativeFaceRegistration>(
            new NativeFaceRegistration(entry, identity, std::move(queue), std::move(output)));
        if (!registration->queue_lifecycle_.running || !registration->queue_lifecycle_.generation)
            return nullptr;
        person_binding_      = std::move(binding);
        person_capability_   = std::move(capability);
        person_entry_        = std::move(entry);
        native_identity_     = std::move(identity);
        native_registration_ = registration;
        labels_              = std::move(labels);
        max_batch_size_      = 1;
        return registration;
    } catch (...) {
        return nullptr;
    }
}

std::shared_ptr<const NativeFaceRegistration> AiDetectorUnify::PrepareNativeFire(
    std::shared_ptr<nn::axera::fire_binding::Binding> binding,
    std::shared_ptr<const nn::axera::fire_binding::Capability> capability,
    std::shared_ptr<AlgDataQueue<AlgDataPtr>> queue, std::shared_ptr<AlgDataQueueDistributor> output,
    std::uint64_t task_generation, std::string scene_id) {
    if (detector_ || native_entry_ || person_entry_ || fire_entry_ || vehicle_entry_ || !binding ||
        !capability || !binding->Owns(capability) || !queue || !output || !task_generation ||
        atomic_code_ != capability->GetContract().atomic_code || scene_id.empty() ||
        scene_id != capability->GetContract().scene_id)
        return nullptr;
    const auto lifecycle = queue->Lifecycle();
    if (!lifecycle.running || !lifecycle.generation)
        return nullptr;
    auto execution = capability->AcquireForExecution();
    if (!execution)
        return nullptr;
    auto model = std::dynamic_pointer_cast<nn::axera::face_binding::SdkLoadedModel>(execution.Model());
    if (!model || !model->HandleForOwnerThread())
        return nullptr;
    try {
        std::vector<std::string> labels{"fire", "smog"};
        auto identity = std::make_shared<int>(0);
        auto entry    = std::make_shared<nn::axera::FireNativeEntry>(capability, identity, task_generation,
                                                                     atomic_code_, std::move(scene_id));
        auto registration = std::shared_ptr<const NativeFaceRegistration>(
            new NativeFaceRegistration(entry, identity, std::move(queue), std::move(output)));
        if (!registration->queue_lifecycle_.running || !registration->queue_lifecycle_.generation)
            return nullptr;
        fire_binding_        = std::move(binding);
        fire_capability_     = std::move(capability);
        fire_entry_          = std::move(entry);
        native_identity_     = std::move(identity);
        native_registration_ = registration;
        labels_              = std::move(labels);
        max_batch_size_      = 1;
        return registration;
    } catch (...) {
        return nullptr;
    }
}

std::shared_ptr<const NativeFaceRegistration> AiDetectorUnify::PrepareNativeVehicle(
    std::shared_ptr<nn::axera::vehicle_binding::Binding> binding,
    std::shared_ptr<const nn::axera::vehicle_binding::Capability> capability,
    std::shared_ptr<AlgDataQueue<AlgDataPtr>> queue, std::shared_ptr<AlgDataQueueDistributor> output,
    std::uint64_t task_generation, std::string scene_id) {
    if (detector_ || native_entry_ || person_entry_ || fire_entry_ || vehicle_entry_ || !binding ||
        !capability || !binding->Owns(capability) || !queue || !output || !task_generation ||
        atomic_code_ != capability->GetContract().atomic_code || scene_id.empty() ||
        scene_id != capability->GetContract().scene_id)
        return nullptr;
    const auto lifecycle = queue->Lifecycle();
    if (!lifecycle.running || !lifecycle.generation)
        return nullptr;
    auto execution = capability->AcquireForExecution();
    if (!execution)
        return nullptr;
    auto model = std::dynamic_pointer_cast<nn::axera::face_binding::SdkLoadedModel>(execution.Model());
    if (!model || !model->HandleForOwnerThread())
        return nullptr;
    try {
        std::vector<std::string> labels{"Person", "Nonvehicle", "Vehicle"};
        auto identity = std::make_shared<int>(0);
        auto entry    = std::make_shared<nn::axera::VehicleNativeEntry>(capability, identity, task_generation,
                                                                        atomic_code_, std::move(scene_id));
        auto registration = std::shared_ptr<const NativeFaceRegistration>(
            new NativeFaceRegistration(entry, identity, std::move(queue), std::move(output)));
        if (!registration->queue_lifecycle_.running || !registration->queue_lifecycle_.generation)
            return nullptr;
        vehicle_binding_     = std::move(binding);
        vehicle_capability_  = std::move(capability);
        vehicle_entry_       = std::move(entry);
        native_identity_     = std::move(identity);
        native_registration_ = registration;
        labels_              = std::move(labels);
        max_batch_size_      = 1;
        return registration;
    } catch (...) {
        return nullptr;
    }
}

util::ErrorEnum AiDetectorUnify::DetectNativeFace(
    const std::shared_ptr<const NativeFaceEnvelope>& envelope, const VideoFramePtr& metadata,
    const media::NativeVideoBufferPtr& source, const std::string& channel_id, float threshold,
    const RegistrationAuthority::Lease& lease, const AlgDataQueue<AlgDataPtr>::LifecycleLease& queue_lease,
    std::vector<AiDetectRstEl>& results,
    const std::array<nn::axera::fire_binding::ClassPolicy, 2>* fire_policies,
    const std::array<nn::axera::vehicle_binding::ClassPolicy, 3>* vehicle_policies) {
    const bool bench           = static_cast<bool>(bench_entry_);
    const bool e24             = e24_runner_ && e24_inputs_;
    const bool face            = native_binding_ && native_capability_ && native_entry_;
    const bool person          = person_binding_ && person_capability_ && person_entry_;
    const bool fire            = fire_binding_ && fire_capability_ && fire_entry_;
    const bool vehicle         = vehicle_binding_ && vehicle_capability_ && vehicle_entry_;
    const bool partial_vehicle = (vehicle_binding_ || vehicle_capability_ || vehicle_entry_) && !vehicle;
    const bool partial_face    = (native_binding_ || native_capability_ || native_entry_) && !face;
    const bool partial_person  = (person_binding_ || person_capability_ || person_entry_) && !person;
    const bool partial_fire    = (fire_binding_ || fire_capability_ || fire_entry_) && !fire;
    if (!envelope || partial_face || partial_person || partial_fire || partial_vehicle ||
        static_cast<int>(face) + static_cast<int>(person) + static_cast<int>(fire) +
                static_cast<int>(vehicle) + static_cast<int>(bench) + static_cast<int>(e24) !=
            1 ||
        !native_identity_ || !native_registration_ || envelope->registration_ != native_registration_ ||
        !envelope->authority_ || !metadata || !metadata->Active() || !metadata->IsMetadataOnly() || !source ||
        source != envelope->source_ || channel_id != envelope->key_.channel_id ||
        metadata->GetFrameIndex() != envelope->key_.frame_index ||
        metadata->GetTimestamp() != envelope->key_.timestamp ||
        metadata->GetStreamIndex() != envelope->stream_index_ ||
        metadata->GetWidth() != static_cast<size_t>(source->width) ||
        metadata->GetHeight() != static_cast<size_t>(source->height))
        return util::ErrorEnum::InvalidImage;
    if (!lease.Matches(envelope->authority_.get(), envelope->generation_) ||
        (face && !native_binding_->Owns(native_capability_)) ||
        (person && !person_binding_->Owns(person_capability_)) ||
        (fire && !fire_binding_->Owns(fire_capability_)) ||
        (vehicle && !vehicle_binding_->Owns(vehicle_capability_)))
        return util::ErrorEnum::AI_FORWARD_FAILED;
    const auto& registration = envelope->registration_;
    auto queue               = registration->queue_.lock();
    if (!queue || registration->detector_identity_ != native_identity_ || registration->e24_ != e24 ||
        (face &&
         (registration->entry_.lock() != native_entry_ || !registration->ticket_ ||
          registration->person_ticket_ || registration->fire_ticket_ || registration->vehicle_ticket_)) ||
        (person && (registration->person_entry_.lock() != person_entry_ || !registration->person_ticket_ ||
                    registration->ticket_ || registration->fire_ticket_ || registration->vehicle_ticket_)) ||
        (fire && (registration->fire_entry_.lock() != fire_entry_ || !registration->fire_ticket_ ||
                  registration->ticket_ || registration->person_ticket_ || registration->vehicle_ticket_)) ||
        (bench && (registration->bench_entry_.lock() != bench_entry_ || !registration->bench_ticket_ ||
                   registration->ticket_ || registration->person_ticket_ || registration->fire_ticket_ ||
                   registration->vehicle_ticket_)) ||
        (vehicle &&
         (registration->vehicle_entry_.lock() != vehicle_entry_ || !registration->vehicle_ticket_ ||
          registration->ticket_ || registration->person_ticket_ || registration->fire_ticket_)))
        return util::ErrorEnum::AI_FORWARD_FAILED;
    if (!queue_lease.Matches(queue.get(), registration->queue_lifecycle_))
        return util::ErrorEnum::AI_FORWARD_FAILED;
    if (e24)
        return DetectNativeE24(source, threshold, results);
    if (fire) {
        if (!fire_policies)
            return util::ErrorEnum::AI_FORWARD_FAILED;
        const auto& k = envelope->key_;
        const nn::axera::FireFrameKey key{k.channel_id, k.stream_generation, k.frame_index, k.timestamp};
        auto result = fire_entry_->Execute(*registration->fire_ticket_, key, source, *fire_policies);
        if (result.error == nn::axera::FireEntryError::OutputCapacityExceeded)
            return util::ErrorEnum::AI_FIRE_OUTPUT_OVERFLOW;
        if (result.error != nn::axera::FireEntryError::None)
            return util::ErrorEnum::AI_FORWARD_FAILED;
        results = std::move(result.targets);
        return util::ErrorEnum::Success;
    }
    if (vehicle) {
        if (!vehicle_policies)
            return util::ErrorEnum::AI_FORWARD_FAILED;
        const auto& k = envelope->key_;
        const nn::axera::VehicleFrameKey key{k.channel_id, k.stream_generation, k.frame_index, k.timestamp};
        auto result = vehicle_entry_->Execute(*registration->vehicle_ticket_, key, source, *vehicle_policies);
        if (result.error == nn::axera::VehicleEntryError::OutputCapacityExceeded)
            return util::ErrorEnum::AI_VEHICLE_OUTPUT_OVERFLOW;
        if (result.error != nn::axera::VehicleEntryError::None)
            return util::ErrorEnum::AI_FORWARD_FAILED;
        results = std::move(result.targets);
        return util::ErrorEnum::Success;
    }
    if (bench) {
        const auto& k = envelope->key_;
        const nn::axera::PersonFrameKey key{k.channel_id, k.stream_generation, k.frame_index, k.timestamp};
        auto result = bench_entry_->Execute(*registration->bench_ticket_, key, source, threshold);
        if (result.error != nn::axera::PersonEntryError::None)
            return util::ErrorEnum::AI_FORWARD_FAILED;
        results = std::move(result.targets);
        return util::ErrorEnum::Success;
    }
    if (person) {
        const auto& k = envelope->key_;
        const nn::axera::PersonFrameKey key{k.channel_id, k.stream_generation, k.frame_index, k.timestamp};
        auto result = person_entry_->Execute(*registration->person_ticket_, key, source, threshold);
        if (result.error == nn::axera::PersonEntryError::OutputCapacityExceeded)
            return util::ErrorEnum::AI_PERSON_OUTPUT_OVERFLOW;
        if (result.error == nn::axera::PersonEntryError::HsOutputCapacityExceeded)
            return util::ErrorEnum::AI_HS_OUTPUT_OVERFLOW;
        if (result.error == nn::axera::PersonEntryError::MouseOutputCapacityExceeded)
            return util::ErrorEnum::AI_MOUSE_OUTPUT_OVERFLOW;
        if (result.error == nn::axera::PersonEntryError::RubbishOutputCapacityExceeded)
            return util::ErrorEnum::AI_RUBBISH_OUTPUT_OVERFLOW;
        if (result.error != nn::axera::PersonEntryError::None)
            return util::ErrorEnum::AI_FORWARD_FAILED;
        results = std::move(result.targets);
        return util::ErrorEnum::Success;
    }
    auto result = native_entry_->Execute(*registration->ticket_, envelope->key_, source, threshold);
    if (result.error == nn::axera::FaceEntryError::OutputCapacityExceeded)
        return util::ErrorEnum::AI_FACE_OUTPUT_OVERFLOW;
    if (result.error != nn::axera::FaceEntryError::None)
        return util::ErrorEnum::AI_FORWARD_FAILED;
    results = std::move(result.targets);
    return util::ErrorEnum::Success;
}

void AiDetectorUnify::CloseNativeFace() {
    e24_runner_.reset();
    e24_inputs_.reset();
    if (bench_entry_)
        bench_entry_->Invalidate();
    bench_entry_.reset();
    if (native_entry_)
        native_entry_->Invalidate();
    if (native_binding_)
        native_binding_->Revoke();
    if (fire_entry_)
        fire_entry_->Invalidate();
    if (fire_binding_)
        fire_binding_->Revoke();
    if (vehicle_entry_)
        vehicle_entry_->Invalidate();
    if (vehicle_binding_)
        vehicle_binding_->Revoke();
    if (person_entry_)
        person_entry_->Invalidate();
    if (person_binding_)
        person_binding_->Revoke();
    native_registration_.reset();
    native_entry_.reset();
    native_identity_.reset();
    native_capability_.reset();
    native_binding_.reset();
    vehicle_entry_.reset();
    vehicle_capability_.reset();
    vehicle_binding_.reset();
    fire_entry_.reset();
    fire_capability_.reset();
    fire_binding_.reset();
    person_entry_.reset();
    person_capability_.reset();
    person_binding_.reset();
}
#endif

util::ErrorEnum AiDetectorUnify::Init() {
    return Init(std::nullopt);
}

util::ErrorEnum AiDetectorUnify::Init(std::optional<InferenceInputSize> input_size) {
    if (detector_
#ifdef COSMO_NN_USE_AXERA_BACKEND
        || native_entry_ || person_entry_ || fire_entry_ || vehicle_entry_ || bench_entry_ || e24_runner_
#endif
    ) {
        LOG_WARN("{}", "Init SDK Detector Failed. Already Init");
        return util::ErrorEnum::Created;
    }

    try {
        cosmo::nn::DefaultComponent::Options options;
        options.input_size = input_size;
        detector_ =
            std::make_unique<cosmo::nn::DefaultComponent>(options, cfg_path_, model_path_, GetDeviceType());
    } catch (const std::exception& e) {
        LOG_ERRO("Init SDK Detector Failed. CfgPath:{} ModelPath:{}, {}", cfg_path_, model_path_, e.what());
        detector_.reset();
        return util::ErrorEnum::Failed;
    }

    if (!detector_) {
        LOG_ERRO("Init SDK Detector Failed. CfgPath:{} ModelPath:{}", cfg_path_, model_path_);
        return util::ErrorEnum::Failed;
    }
    LOG_INFO("Init SDK Detector Ok. CfgPath:{} ModelPath:{}", cfg_path_, model_path_);

    max_batch_size_ = static_cast<size_t>(detector_->GetMaxBatchSize());
    LOG_DEBUG("{} Detector Max Batch Size {}", model_path_, max_batch_size_);

    labels_                  = detector_->GetSelectedClassnames();
    joint_person_attributes_ = detector_->GetModelType() == "yolo26_person_attributes";

    return util::ErrorEnum::Success;
}

util::ErrorEnum AiDetectorUnify::Detect(const std::vector<VideoFramePtr>& images,
                                        std::vector<AiConfidence> conf_thres,
                                        std::vector<std::vector<AiDetectRstEl>>& results) {
    return Detect(images, {}, std::move(conf_thres), results);
}

util::ErrorEnum AiDetectorUnify::Detect(const std::vector<VideoFramePtr>& images,
                                        const std::vector<media::NativeVideoBufferPtr>& native_buffers,
                                        std::vector<AiConfidence> conf_thres,
                                        std::vector<std::vector<AiDetectRstEl>>& results) {
    if (!detector_) {
        LOG_WARN("{}", "SDK Detector Not Init");
        return util::ErrorEnum::NotInit;
    }

    std::for_each(conf_thres.begin(), conf_thres.end(),
                  [this](const auto& conf) { detector_->SetThreshold(conf.label, conf.confidence); });

    try {
        size_t image_num = images.size();
        std::vector<std::vector<AiDetectRstEl>> completeResults;
        std::vector<VideoFramePtr> inputs;
        std::vector<media::NativeVideoBufferPtr> native_inputs;
        for (size_t i = 0; i < image_num; i++) {
            inputs.push_back(images[i]);
            native_inputs.push_back(i < native_buffers.size() ? native_buffers[i] : nullptr);
            size_t input_size = inputs.size();
            if (input_size == max_batch_size_ || (i + 1) == image_num) {
                std::vector<std::vector<AiDetectRstEl>> outputs;
                auto ret = Forward(inputs, native_inputs, outputs);
                if (util::ErrorEnum::Success != ret) {
                    LOG_ERRO("Forward Failed. Ret:{}", ret);
                    return ret;
                }
                if (!infer_detail::AppendCompleteDetectorBatch(input_size, std::move(outputs),
                                                               completeResults)) {
                    LOG_ERRO("Detector output count does not match input batch:{}", input_size);
                    return util::ErrorEnum::AI_PARSE_OUTPUT_FAILED;
                }
                inputs.clear();
                native_inputs.clear();
            }
        }
        // Do not expose earlier partial batches if any later batch fails.
        results.insert(results.end(), std::make_move_iterator(completeResults.begin()),
                       std::make_move_iterator(completeResults.end()));
    } catch (const std::exception& e) {
        LOG_ERRO("Detect Exception. CfgPath:{} ModelPath:{} images:{} confThres:{} maxBatch:{}, {}",
                 cfg_path_, model_path_, images.size(), conf_thres.size(), max_batch_size_, e.what());
        return util::ErrorEnum::AI_FORWARD_FAILED;
    } catch (...) {
        LOG_ERRO("Detect non-std exception. CfgPath:{} ModelPath:{} images:{} confThres:{} maxBatch:{}",
                 cfg_path_, model_path_, images.size(), conf_thres.size(), max_batch_size_);
        return util::ErrorEnum::AI_FORWARD_FAILED;
    }
    return util::ErrorEnum::Success;
}

util::ErrorEnum AiDetectorUnify::Forward(const std::vector<VideoFramePtr>& images,
                                         const std::vector<media::NativeVideoBufferPtr>& native_buffers,
                                         std::vector<std::vector<AiDetectRstEl>>& results) {
    std::vector<std::shared_ptr<cosmo::nn::Blob>> image_blobs{};
    const auto blob_convert_started = MetricsClock::now();
    auto ret                        = ConvertImagesToBlobs(images, native_buffers, image_blobs);
    cosmo::nn::GetInferencePipelineMetrics().RecordBlobConvert(ElapsedNanoseconds(blob_convert_started),
                                                               static_cast<uint64_t>(image_blobs.size()));
    if (util::ErrorEnum::Success != ret) {
        LOG_ERRO("ConvertImagesToBlobs Failed. Ret:{}", ret);
        return ret;
    }
    if (image_blobs.size() != images.size()) {
        LOG_ERRO("Detector input conversion count:{} does not match input batch:{}", image_blobs.size(),
                 images.size());
        return util::ErrorEnum::InvalidImage;
    }
    const auto graph_forward_started = MetricsClock::now();
    try {
        auto status = detector_->Forward({image_blobs});
        if (!bool(status)) {
            cosmo::nn::GetInferencePipelineMetrics().RecordGraphForward(
                ElapsedNanoseconds(graph_forward_started), static_cast<uint64_t>(image_blobs.size()), false);
            LOG_ERRO("Forward Failed.({})", status.description());
            return util::ErrorEnum::AI_FORWARD_FAILED;
        }
    } catch (const std::exception& e) {
        cosmo::nn::GetInferencePipelineMetrics().RecordGraphForward(
            ElapsedNanoseconds(graph_forward_started), static_cast<uint64_t>(image_blobs.size()), false);
        LOG_ERRO("Forward Exception. CfgPath:{} ModelPath:{} inputBatch:{}, {}", cfg_path_, model_path_,
                 image_blobs.size(), e.what());
        return util::ErrorEnum::AI_FORWARD_FAILED;
    } catch (...) {
        cosmo::nn::GetInferencePipelineMetrics().RecordGraphForward(
            ElapsedNanoseconds(graph_forward_started), static_cast<uint64_t>(image_blobs.size()), false);
        LOG_ERRO("Forward non-std exception. CfgPath:{} ModelPath:{} inputBatch:{}", cfg_path_, model_path_,
                 image_blobs.size());
        return util::ErrorEnum::AI_FORWARD_FAILED;
    }
    cosmo::nn::GetInferencePipelineMetrics().RecordGraphForward(
        ElapsedNanoseconds(graph_forward_started), static_cast<uint64_t>(image_blobs.size()), true);

    std::vector<std::vector<cosmo::nn::ObjectInfoV1>> outputs;
    const auto result_parse_started = MetricsClock::now();
    try {
        auto status = detector_->ParseOutput<cosmo::nn::ObjectInfoV1>(outputs);
        if (!bool(status)) {
            cosmo::nn::GetInferencePipelineMetrics().RecordResultParse(
                ElapsedNanoseconds(result_parse_started), static_cast<uint64_t>(image_blobs.size()), false);
            LOG_ERRO("ParseOutput Failed.({})", status.description());
            return util::ErrorEnum::AI_PARSE_OUTPUT_FAILED;
        }
    } catch (const std::exception& e) {
        cosmo::nn::GetInferencePipelineMetrics().RecordResultParse(
            ElapsedNanoseconds(result_parse_started), static_cast<uint64_t>(image_blobs.size()), false);
        LOG_ERRO("ParseOutput Exception. CfgPath:{} ModelPath:{}, {}", cfg_path_, model_path_, e.what());
        return util::ErrorEnum::AI_PARSE_OUTPUT_FAILED;
    } catch (...) {
        cosmo::nn::GetInferencePipelineMetrics().RecordResultParse(
            ElapsedNanoseconds(result_parse_started), static_cast<uint64_t>(image_blobs.size()), false);
        LOG_ERRO("ParseOutput non-std exception. CfgPath:{} ModelPath:{}", cfg_path_, model_path_);
        return util::ErrorEnum::AI_PARSE_OUTPUT_FAILED;
    }
    if (outputs.size() != image_blobs.size()) {
        cosmo::nn::GetInferencePipelineMetrics().RecordResultParse(
            ElapsedNanoseconds(result_parse_started), static_cast<uint64_t>(image_blobs.size()), false);
        LOG_ERRO("ParseOutput size:{} does not match inputBatch:{}. CfgPath:{} ModelPath:{}", outputs.size(),
                 image_blobs.size(), cfg_path_, model_path_);
        return util::ErrorEnum::AI_PARSE_OUTPUT_FAILED;
    }
    cosmo::nn::GetInferencePipelineMetrics().RecordResultParse(
        ElapsedNanoseconds(result_parse_started), static_cast<uint64_t>(image_blobs.size()), true);
    for (size_t i = 0; i < outputs.size(); i++) {
        const auto& dims = image_blobs.at(i)->GetBlobDesc().dims;
        std::vector<AiDetectRstEl> result;
        const auto status = ConvertResults(outputs[i], dims.at(2), dims.at(1), result);
        if (status != util::ErrorEnum::Success)
            return status;
        results.push_back(std::move(result));
    }
    return util::ErrorEnum::Success;
}

util::ErrorEnum AiDetectorUnify::ConvertResults(const std::vector<nn::ObjectInfoV1>& objects, int width,
                                                int height, std::vector<AiDetectRstEl>& result) const {
    for (const auto& obj : objects) {
        AiDetectRstEl el;
        if (obj.oriented_corners) {
            const double max_x = width - 1.0;
            const double max_y = height - 1.0;
            if (max_x < 0 || max_y < 0 || obj.x2 < 0 || obj.y2 < 0 || obj.x1 > max_x || obj.y1 > max_y ||
                !std::isfinite(obj.x1) || !std::isfinite(obj.y1) || !std::isfinite(obj.x2) ||
                !std::isfinite(obj.y2))
                continue;
            // Bound floating-point coordinates before converting to integer pixels.
            el.box.x      = static_cast<int>(std::floor(std::clamp<double>(obj.x1, 0, max_x)));
            el.box.y      = static_cast<int>(std::floor(std::clamp<double>(obj.y1, 0, max_y)));
            el.box.width  = static_cast<int>(std::ceil(std::clamp<double>(obj.x2, 0, max_x))) - el.box.x + 1;
            el.box.height = static_cast<int>(std::ceil(std::clamp<double>(obj.y2, 0, max_y))) - el.box.y + 1;
        } else {
            el.box.x      = static_cast<int>(obj.x1);
            el.box.y      = static_cast<int>(obj.y1);
            el.box.width  = static_cast<int>(obj.x2) - static_cast<int>(obj.x1) + 1;
            el.box.height = static_cast<int>(obj.y2) - static_cast<int>(obj.y1) + 1;
            el.box &= util::Box(0, 0, width - 1, height - 1);
        }
        el.oriented_corners = obj.oriented_corners;
        if (el.box.width <= 0 || el.box.height <= 0) {
            continue;
        }
        el.hwRatio = static_cast<float>(el.box.height) / static_cast<float>(el.box.width);
        if (obj.infos.empty()) {
            continue;
        }
        el.confidence.confidence  = obj.infos.at(0).confidence;
        el.confidence.label       = obj.infos.at(0).class_name;
        el.confidence.atomic_code = atomic_code_;
        el.confidence.source      = AiAnnotationSource::Detection;
        el.classId                = obj.infos.at(0).class_id + 1;
        el.targetId               = util::GenerateUUID();
        if (joint_person_attributes_) {
            if (obj.infos.size() != 5 || obj.infos.front().class_id != 0 ||
                obj.infos.front().class_name != "person") {
                LOG_ERRO("Unexpected person and attribute output from {}", model_path_);
                return util::ErrorEnum::AI_PARSE_OUTPUT_FAILED;
            }
            el.hasJointPersonAttributes = true;
            for (auto it = std::next(obj.infos.begin()); it != obj.infos.end(); ++it) {
                AiConfidence score{it->class_name, atomic_code_, it->confidence,
                                   AiAnnotationSource::Classification, util::GenerateUUID()};
                el.attrRst.push_back({score.label, score.label, score.atomic_code, score.confidence,
                                      score.source, score.measurementId});
                el.classifyRst.push_back(std::move(score));
            }
        }
        result.push_back(std::move(el));
    }
    return util::ErrorEnum::Success;
}

util::ErrorEnum AiDetectorUnify::GetMaxBatchSize(size_t& value) {
#ifdef COSMO_NN_USE_AXERA_BACKEND
    if (native_entry_ || person_entry_ || fire_entry_ || vehicle_entry_ || bench_entry_ || e24_runner_) {
        value = 1;
        return util::ErrorEnum::Success;
    }
#endif
    if (!detector_) {
        LOG_WARN("{}", "SDK Detector Not Init");
        return util::ErrorEnum::NotInit;
    }
    value = static_cast<size_t>(detector_->GetMaxBatchSize());
    return util::ErrorEnum::Success;
}
}  // namespace cosmo
