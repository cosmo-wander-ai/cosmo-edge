// AiDetector.cc — Core detection logic: init, inference, filtering, and thread lifecycle.
// Parameter management is in AiDetectorParam.cc.
// Target area marking and history is in AiDetectorTarget.cc.
// Channel/task lifecycle management is in AiDetectorTask.cc.

#include "flow/detect/AiDetector.h"

#include "flow/detect/BenchDetectorQueueName.h"
#include "flow/detect/BenchSharedPair.h"
#include "flow/detect/NativeFireSelector.h"
#include "flow/detect/NativeHsSelector.h"
#include "flow/detect/NativeMouseSelector.h"
#include "flow/detect/NativeRubbishSelector.h"
#include "flow/detect/NativeVehicleSelector.h"
#include "flow/detect/PersonTemplateContract.h"
#include "nn/device/axera/axera_route_identity.h"
#ifdef COSMO_NN_USE_AXERA_BACKEND
#include "flow/detect/NativeFaceInputGuard.h"
#if defined(COSMO_MEDIA_USE_AXERA_BACKEND)
#include "ax_sys_api.h"
#include "media/AxeraVideoFrameCopy.h"
#ifdef COSMO_AXERA_SCENARIOBENCH_CANDIDATE
#include "nn/device/axera/axera_native_execution_state.h"
#include "nn/device/axera/benchmark/BenchBorrowedEventFrame.h"
#include "nn/device/axera/benchmark/BenchLazyEventFrame.h"
#endif
#include <limits>
#endif
#endif
#ifdef COSMO_AXERA_FACE_ISOLATED_DEVICE_TEST
#include "nn/device/axera/axera_face_diagnostics.h"
#endif

#include <algorithm>
#include <cctype>
#include <cmath>
#include <sstream>
#include <unordered_map>

#include "service/detail/ServiceRegistry.h"
#include "service/model/IModelPathMapping.h"
#include "service/system/IConfigReadService.h"
#include "service/system/IHardwareQuery.h"
#include "util/EnvUtil.h"
#include "util/Log.h"
#include "util/SafeParse.h"
#include "util/TimeUtil.h"
#include "util/UuidUtil.h"
#include "util/dto/ActionCodes.h"

static constexpr const char* kTag = "AI-DETECTER ";
namespace cosmo {

namespace {
#if defined(COSMO_NN_USE_AXERA_BACKEND) && defined(COSMO_MEDIA_USE_AXERA_BACKEND)
    // The detector holds the VDEC lease here. Snapshot only target-bearing
    // frames before releasing it; downstream event processing is asynchronous.
    VideoFramePtr SnapshotNativeEventFrame(const media::NativeVideoBufferPtr& native,
                                           const VideoFramePtr& metadata, bool benchmark_cached_uv) {
        if (!native || !metadata || !native->Valid() ||
            native->memory_kind != media::NativeVideoMemoryKind::AxeraPhysical ||
            native->format != media::NativeVideoBufferFormat::NV12 || native->crop_x || native->crop_y ||
            !native->frame_witness || !native->frame_witness->Matches(*native) ||
            native->frame_index != metadata->GetFrameIndex() ||
            native->planes[0].bytes > std::numeric_limits<AX_U32>::max() ||
            native->planes[1].bytes > std::numeric_limits<AX_U32>::max())
            return nullptr;
        struct Mapping {
            AX_VOID* ptr;
            AX_U32 bytes;
            ~Mapping() {
                if (ptr)
                    AX_SYS_Munmap(ptr, bytes);
            }
        };
        Mapping y{
            AX_SYS_Mmap(native->planes[0].physical_address, static_cast<AX_U32>(native->planes[0].bytes)),
            static_cast<AX_U32>(native->planes[0].bytes)};
        Mapping uv{
            AX_SYS_Mmap(native->planes[1].physical_address, static_cast<AX_U32>(native->planes[1].bytes)),
            static_cast<AX_U32>(native->planes[1].bytes)};
        if (!y.ptr || !uv.ptr)
            return nullptr;
        auto frame =
            std::make_shared<media::VideoFrame>(native->width, native->height, media::PixelFormat::PIXEL_I420,
                                                metadata->GetFrameIndex(), metadata->GetTimestamp());
        const uint8_t* uv_source = static_cast<const uint8_t*>(uv.ptr);
        static thread_local std::vector<uint8_t> cached_uv;
        if (benchmark_cached_uv) {
            cached_uv.resize(uv.bytes);
            std::memcpy(cached_uv.data(), uv_source, uv.bytes);
            uv_source = cached_uv.data();
        }
        if (!frame->Active() || !media::CopyAxeraSemiplanarToI420(
                                    static_cast<const uint8_t*>(y.ptr), y.bytes, native->planes[0].stride,
                                    uv_source, uv.bytes, native->planes[1].stride, native->width,
                                    native->height, false, frame->GetData(), frame->GetSize()))
            return nullptr;
        frame->SetStreamIndex(metadata->GetStreamIndex());
        return frame;
    }

    VideoFramePtr DeferE24EventFrame(const media::NativeVideoBufferPtr& native,
                                     const VideoFramePtr& metadata) {
        // E24 uses one model owner per channel. At most one unmaterialized
        // event frame on that worker may retain a decoder lease.
        static thread_local std::weak_ptr<media::VideoFrame> pending;
        if (const auto current = pending.lock(); current && current->IsDeferred())
            return {};
        auto result = std::make_shared<media::VideoFrame>(
            metadata->GetWidth(), metadata->GetHeight(), media::PixelFormat::PIXEL_I420,
            metadata->GetFrameIndex(), metadata->GetTimestamp(), [native, metadata] {
                auto image = SnapshotNativeEventFrame(native, metadata, true);
                static std::atomic<std::uint64_t> materializations{0};
                const auto count = materializations.fetch_add(1, std::memory_order_relaxed) + 1;
                if (count == 1 || count % 256 == 0 || !image)
                    LOG_INFO("AX E24 event frame materialized count:{} success:{}", count, bool(image));
                return image;
            });
        if (!result->Active())
            return {};
        result->SetStreamIndex(metadata->GetStreamIndex());
        pending = result;
        static std::atomic<std::uint64_t> deferred{0};
        const auto count = deferred.fetch_add(1, std::memory_order_relaxed) + 1;
        if (count == 1 || count % 256 == 0)
            LOG_INFO("AX E24 event frame deferred count:{}", count);
        return result;
    }
#ifdef COSMO_AXERA_SCENARIOBENCH_CANDIDATE
    VideoFramePtr TryBorrowedNativeEvent(const media::NativeVideoBufferPtr& native,
                                         const VideoFramePtr& metadata, const std::string& channel) {
        namespace bench = nn::axera::benchmark;
        struct Mapper final : bench::SnapshotMap {
            const std::uint8_t* Map(std::uint64_t address, std::size_t bytes) noexcept override {
                if (bytes > std::numeric_limits<AX_U32>::max())
                    return nullptr;
                try {
                    return static_cast<const std::uint8_t*>(AX_SYS_Mmap(address, static_cast<AX_U32>(bytes)));
                } catch (...) {
                    return nullptr;
                }
            }
            bool Unmap(const std::uint8_t* ptr, std::size_t bytes) noexcept override {
                if (bytes > std::numeric_limits<AX_U32>::max())
                    return false;
                try {
                    return AX_SYS_Munmap(const_cast<std::uint8_t*>(ptr), static_cast<AX_U32>(bytes)) == 0;
                } catch (...) {
                    return false;
                }
            }
        };
        static std::atomic<std::uint64_t> calls{0}, hits{0}, materialized{0};
        try {
            static auto mapper = std::make_shared<Mapper>();
            auto finished      = [](bench::SnapshotError error) {
                const auto n = ++materialized;
                if (n <= 3 || n % 500 == 0 || error != bench::SnapshotError::None)
                    LOG_INFO("AX_BENCH_BORROWED_EVENT_MATERIALIZED count:{} status:{}", n,
                                  static_cast<int>(error));
                if (error == bench::SnapshotError::UnmapFailed) {
                    try {
                        auto& state = nn::axera::detail::NativeState();
                        std::lock_guard<std::mutex> lock(state.mutex);
                        state.poisoned = true;
                    } catch (...) {
                    }
                }
            };
            auto frame   = bench::MakeBorrowedEventFrame(native, metadata, channel, mapper, finished);
            const auto n = ++calls;
            const auto h = frame ? ++hits : hits.load();
            if (n <= 3 || n % 500 == 0)
                LOG_INFO("AX_BENCH_BORROWED_EVENT calls:{} hits:{}", n, h);
            return frame;
        } catch (...) {
            return {};
        }
    }
    struct LazyEventAttempt {
        VideoFramePtr frame;
        bool fatal{false};
    };
    LazyEventAttempt TryLazyNativeEvent(const media::NativeVideoBufferPtr& native,
                                        const VideoFramePtr& metadata, const std::string& channel) {
        namespace bench = nn::axera::benchmark;
        struct Mapper final : bench::SnapshotMap {
            const std::uint8_t* Map(std::uint64_t address, std::size_t bytes) noexcept override {
                if (bytes > std::numeric_limits<AX_U32>::max())
                    return nullptr;
                try {
                    return static_cast<const std::uint8_t*>(AX_SYS_Mmap(address, static_cast<AX_U32>(bytes)));
                } catch (...) {
                    return nullptr;
                }
            }
            bool Unmap(const std::uint8_t* ptr, std::size_t bytes) noexcept override {
                if (bytes > std::numeric_limits<AX_U32>::max())
                    return false;
                try {
                    return AX_SYS_Munmap(const_cast<std::uint8_t*>(ptr), static_cast<AX_U32>(bytes)) == 0;
                } catch (...) {
                    return false;
                }
            }
        } mapper;
        try {
            static auto budget = std::make_shared<bench::SnapshotBudget>(8);
            auto result        = bench::MakeLazyEventFrame(native, metadata, channel, mapper, budget);
            static std::atomic<std::uint64_t> calls{0}, hits{0};
            const auto n = ++calls;
            const auto h = result.frame ? ++hits : hits.load();
            if (n <= 3 || n % 500 == 0 || result.error == bench::SnapshotError::UnmapFailed)
                LOG_INFO("AX_BENCH_LAZY_EVENT calls:{} hits:{} status:{}", n, h,
                         static_cast<int>(result.error));
            if (result.error == bench::SnapshotError::UnmapFailed) {
                try {
                    auto& state = nn::axera::detail::NativeState();
                    std::lock_guard<std::mutex> lock(state.mutex);
                    state.poisoned = true;
                } catch (...) {
                }  // still fail closed if the execution lock fails
                return {{}, true};  // unresolved quarantine retains the source
            }
            return {std::move(result.frame), false};
        } catch (...) {
            return {{}, false};
        }
    }
#endif
#endif
    constexpr const char* kEnvMaxReuse     = "COSMO_AI_DETECTOR_MAX_REUSE";
    constexpr const char* kEnvFpsBudget    = "COSMO_AI_DETECTOR_FPS_BUDGET";
    constexpr const char* kEnvReuseProfile = "COSMO_AI_DETECTOR_REUSE_PROFILE";
    constexpr size_t kMaxRuntimeReuseLimit = 64;

    std::string SanitizeEnvSuffix(const std::string& raw) {
        std::string suffix;
        suffix.reserve(raw.size());
        for (unsigned char ch : raw) {
            suffix.push_back(std::isalnum(ch) ? static_cast<char>(std::toupper(ch)) : '_');
        }
        return suffix;
    }

    std::string LookupPlacementEnv(const char* base_key, const std::string& alg_code) {
        const auto suffix = SanitizeEnvSuffix(alg_code);
        if (!suffix.empty()) {
            const auto alg_key = std::string(base_key) + "_" + suffix;
            const auto value   = util::GetEnvOrDefault(alg_key.c_str(), "");
            if (!value.empty()) {
                return value;
            }
        }
        return util::GetEnvOrDefault(base_key, "");
    }

    size_t LookupMaxReuseCount(const std::string& alg_code) {
        const auto raw_value = LookupPlacementEnv(kEnvMaxReuse, alg_code);
        if (raw_value.empty()) {
            return kMaxReuseHardLimit;
        }

        size_t parsed = 0;
        if (!ai_detector_fps::ParsePositiveSize(raw_value, parsed)) {
            LOG_WARN("{}Invalid {} value:{}, fallback:{}", kTag, kEnvMaxReuse, raw_value, kMaxReuseHardLimit);
            return kMaxReuseHardLimit;
        }
        return std::clamp(parsed, static_cast<size_t>(1), kMaxRuntimeReuseLimit);
    }

    ai_detector_fps::ReuseProfile BackendDefaultReuseProfile() {
#ifdef COSMO_NN_USE_RKNN_BACKEND
        return ai_detector_fps::RknnDefaultReuseProfile();
#else
        return ai_detector_fps::DefaultReuseProfile();
#endif
    }

    // Per-algCode fps budget overrides for instance placement. Unlisted algCodes fall back to
    // kDefaultInstanceFpsBudget. Populate from per-model stress-test results.
    float LookupInstanceFpsBudget(const std::string& alg_code) {
        const auto raw_value = LookupPlacementEnv(kEnvFpsBudget, alg_code);
        if (!raw_value.empty()) {
            const float parsed = util::ParseFloat(raw_value, -1.0f);
            if (ai_detector_fps::HasConfiguredFps(parsed)) {
                return parsed;
            }
            LOG_WARN("{}Invalid {} value:{}, fallback:{}", kTag, kEnvFpsBudget, raw_value,
                     kDefaultInstanceFpsBudget);
        }

        static const std::unordered_map<std::string, float> kAlgFpsBudget = {
            // {"45626", 36.0f},
        };
        const auto it = kAlgFpsBudget.find(alg_code);
        return (it != kAlgFpsBudget.end()) ? it->second : kDefaultInstanceFpsBudget;
    }

    ai_detector_fps::ReuseProfile LookupReuseProfile(const std::string& alg_code) {
        const auto raw_value = LookupPlacementEnv(kEnvReuseProfile, alg_code);
        if (raw_value.empty()) {
            return BackendDefaultReuseProfile();
        }

        auto profile = ai_detector_fps::ParseReuseProfile(raw_value);
        if (profile.empty()) {
            LOG_WARN("{}Invalid {} value:{}, fallback to default reuse profile", kTag, kEnvReuseProfile,
                     raw_value);
            return BackendDefaultReuseProfile();
        }
        return profile;
    }

    std::string ReuseProfileToString(const ai_detector_fps::ReuseProfile& profile) {
        if (profile.empty()) {
            return "formula";
        }

        std::ostringstream oss;
        for (size_t i = 0; i < profile.size(); ++i) {
            if (i > 0) {
                oss << ",";
            }
            oss << profile[i].max_fps << ":" << profile[i].reuse_count;
        }
        return oss.str();
    }
    std::string DetectorQueueName(const std::string& atomic_code) {
#if defined(COSMO_NN_USE_AXERA_BACKEND) && defined(COSMO_AXERA_SCENARIOBENCH_CANDIDATE)
        if (atomic_code == "9275710") {
            return BenchDetectorQueueName(atomic_code, util::GenerateUUID());
        }
#endif
        return BenchDetectorQueueName(atomic_code, "");
    }
}  // namespace

AiDetector::~AiDetector() {
    is_detector_inst_initialized_ = false;
    LOG_INFO("{}[{} {}] Stop", kTag, name_, uuid);
    AiDetector::Stop();
    if (detector_) {
        if (running) {
            running = false;
            stop();
        }

        // Stop discards queued data even if this worker never started.
        // Pop deliberately refuses items from a stopped queue.
        data_queue->Stop();

        detector_.reset();
        detector_ = nullptr;

        channel_list_.clear();
        task_histories_.clear();
        overview_rec_insts_.clear();
        task_areas_.clear();
    }
    LOG_INFO("{}[{} {}] Delete", kTag, name_, uuid);
}

AiDetector::AiDetector(ActionNode& action)
    : AlgActionBase(AlgActionType::AlgActionAiDetect, action, "", "", DetectorQueueName(action.atomicCode)) {
    action_status = util::ErrorEnum::ActionReady;
    data_queue->SetMaxSize(30);
    alg_code_   = action.atomicCode;
    input_size_ = action.input_size;
    name_       = action.atomicCode;
    uuid        = util::GenerateUUID();

    // Admit the first frame immediately. AiSdkInit replaces this with the
    // loaded model's bounded batch before the next queue drain.
    batch_count_     = 1;
    max_reuse_count_ = LookupMaxReuseCount(alg_code_);
#ifdef COSMO_NN_USE_AXERA_BACKEND
    // A native person-attribute registration owns one channel's decoder and output queue.
    if (nn::axera::route_identity::IsPersonAttributesModel(alg_code_) &&
        NativeFaceSelection() == NativeSelection::Selected)
        max_reuse_count_ = 1;
#endif
    instance_fps_budget_ = LookupInstanceFpsBudget(alg_code_);
    reuse_profile_       = LookupReuseProfile(alg_code_);
    data_queue->SetMaxSize(48);

    LOG_INFO("{}[{} {}] Init MaxReuse:{} BatchCount:{} FpsBudget:{} ReuseProfile:{}", kTag, name_, uuid,
             max_reuse_count_, batch_count_, instance_fps_budget_, ReuseProfileToString(reuse_profile_));
}

#ifdef COSMO_NN_USE_AXERA_BACKEND
std::shared_ptr<const NativeFaceRegistration> AiDetector::NativeFaceRegistrationForQueue() {
    std::lock_guard<std::mutex> lock(native_registration_mutex_);
    return native_registration_;
}

struct AiDetector::NativePrepared final : NativeFaceWorkerInit::Prepared {
    AiDetector& owner;
    std::shared_ptr<AiDetectorUnify> detector;
    std::shared_ptr<const NativeFaceRegistration> registration;
    std::shared_ptr<AlgDataQueue<AlgDataPtr>> queue;
    std::string scene;
    float threshold;
    std::uint64_t capacity;
    std::optional<std::array<nn::axera::fire_binding::ClassPolicy, 2>> fire_policies;
    std::optional<std::array<nn::axera::vehicle_binding::ClassPolicy, 3>> vehicle_policies;
    bool person{false};
    std::optional<float> person_min;
    bool committed{false};
    NativePrepared(
        AiDetector& o, std::shared_ptr<AiDetectorUnify> d, std::shared_ptr<const NativeFaceRegistration> r,
        std::shared_ptr<AlgDataQueue<AlgDataPtr>> q, std::string s, float t, std::uint64_t c,
        std::optional<std::array<nn::axera::fire_binding::ClassPolicy, 2>> policies   = std::nullopt,
        bool is_person                                                                = false,
        std::optional<std::array<nn::axera::vehicle_binding::ClassPolicy, 3>> vehicle = std::nullopt,
        std::optional<float> dynamic_person_min                                       = std::nullopt)
        : owner(o),
          detector(std::move(d)),
          registration(std::move(r)),
          queue(std::move(q)),
          scene(std::move(s)),
          threshold(t),
          capacity(c),
          fire_policies(std::move(policies)),
          vehicle_policies(std::move(vehicle)),
          person(is_person),
          person_min(dynamic_person_min) {}
    ~NativePrepared() override {
        // A cancelled/timed-out command destroys SDK state on this worker.
        if (!committed && detector)
            detector->CloseNativeFace();
    }
    bool Commit() override {
        const auto lifecycle = queue->Lifecycle();
        if ((fire_policies && vehicle_policies) || (person && (fire_policies || vehicle_policies)) ||
            !owner.running || owner.detector_ || owner.data_queue != queue || !lifecycle.running ||
            !lifecycle.generation || lifecycle.generation != registration->queue_lifecycle_.generation)
            return false;
        auto labels = detector->GetLabels();
        if (labels.empty())
            return false;
        std::lock_guard<std::mutex> lock(owner.native_registration_mutex_);
        owner.detector_                     = std::move(detector);
        owner.labels_                       = std::move(labels);
        owner.batch_count_                  = 1;
        owner.is_detector_inst_initialized_ = true;
        owner.native_scene_id_              = std::move(scene);
        owner.native_scene_threshold_       = threshold;
        owner.native_person_min_threshold_  = person_min;
        owner.native_fire_policies_         = std::move(fire_policies);
        owner.native_vehicle_policies_      = std::move(vehicle_policies);
        owner.native_face_capacity_         = capacity;
        owner.native_person_registered_     = person;
        owner.native_registration_          = std::move(registration);
        committed                           = true;
        return true;
    }
};

std::unique_ptr<NativeFaceWorkerInit::Prepared> AiDetector::PrepareNativeFaceOnWorker(
    std::shared_ptr<nn::axera::face_binding::Binding> binding,
    std::shared_ptr<const nn::axera::face_binding::Capability> capability, std::uint64_t task_generation,
    std::string scene_id, float scene_threshold) {
    if (detector_ || !data_queue || !capability || !std::isfinite(scene_threshold) ||
        scene_threshold < capability->GetContract().graph_confidence || scene_threshold > 1.f)
        return nullptr;
    const auto capacity = capability->GetContract().result_capacity;
    auto queue          = data_queue;
    auto candidate      = std::make_shared<AiDetectorUnify>(alg_code_, "", "");
    auto registration   = candidate->PrepareNativeFace(std::move(binding), std::move(capability), queue,
                                                       distributor, task_generation, scene_id);
    if (!registration)
        return nullptr;
    return std::make_unique<NativePrepared>(*this, std::move(candidate), std::move(registration),
                                            std::move(queue), std::move(scene_id), scene_threshold, capacity);
}

std::unique_ptr<NativeFaceWorkerInit::Prepared> AiDetector::PrepareE24OnWorker(
    std::shared_ptr<const std::vector<std::uint8_t>> bytes, std::uint64_t generation, std::string scene) {
    if (detector_ || !data_queue || !bytes)
        return {};
    auto queue        = data_queue;
    auto candidate    = std::make_shared<AiDetectorUnify>(alg_code_, "", "");
    auto registration = candidate->PrepareNativeE24(std::move(bytes), queue, distributor, generation, scene);
    if (!registration)
        return {};
    return std::make_unique<NativePrepared>(*this, std::move(candidate), std::move(registration),
                                            std::move(queue), std::move(scene), .25f, 300);
}

std::unique_ptr<NativeFaceWorkerInit::Prepared> AiDetector::PrepareBenchmarkPersonOnWorker(
    std::unique_ptr<nn::axera::benchmark::PersonExecutor> executor, std::uint64_t generation,
    std::string scene) {
    if (detector_ || !data_queue || !executor)
        return {};
    auto queue     = data_queue;
    auto candidate = std::make_shared<AiDetectorUnify>(alg_code_, "", "");
    auto registration =
        candidate->PrepareBenchmarkPerson(std::move(executor), queue, distributor, generation, scene);
    if (!registration)
        return {};
    return std::make_unique<NativePrepared>(*this, std::move(candidate), std::move(registration),
                                            std::move(queue), std::move(scene), .25f, 1000, std::nullopt,
                                            true);
}
std::unique_ptr<NativeFaceWorkerInit::Prepared> AiDetector::PrepareNativePersonOnWorker(
    std::shared_ptr<nn::axera::person_binding::Binding> binding,
    std::shared_ptr<const nn::axera::person_binding::Capability> capability, std::uint64_t task_generation,
    std::string scene_id, float scene_threshold, bool person_template) {
    if (detector_ || !data_queue || !capability || !std::isfinite(scene_threshold) ||
        scene_threshold < capability->GetContract().graph_confidence || scene_threshold > 1.f)
        return nullptr;
    const auto capacity = capability->GetContract().result_capacity;
    const std::optional<float> person_min =
        person_template ? std::optional<float>(capability->GetContract().graph_confidence) : std::nullopt;
    auto queue     = data_queue;
    auto candidate = std::make_shared<AiDetectorUnify>(alg_code_, "", "");
    auto registration =
        candidate->PrepareNativePerson(std::move(binding), std::move(capability), queue, distributor,
                                       task_generation, scene_id, person_template);
    if (!registration)
        return nullptr;
    return std::make_unique<NativePrepared>(*this, std::move(candidate), std::move(registration),
                                            std::move(queue), std::move(scene_id), scene_threshold, capacity,
                                            std::nullopt, true, std::nullopt, person_min);
}
std::unique_ptr<NativeFaceWorkerInit::Prepared> AiDetector::PrepareNativeFireOnWorker(
    std::shared_ptr<nn::axera::fire_binding::Binding> binding,
    std::shared_ptr<const nn::axera::fire_binding::Capability> capability, std::uint64_t task_generation,
    std::string scene_id) {
    if (detector_ || !data_queue || !capability)
        return nullptr;
    const auto capacity = capability->GetContract().result_capacity;
    const auto policies = capability->GetContract().classes;
    auto queue          = data_queue;
    auto candidate      = std::make_shared<AiDetectorUnify>(alg_code_, "", "");
    auto registration   = candidate->PrepareNativeFire(std::move(binding), std::move(capability), queue,
                                                       distributor, task_generation, scene_id);
    if (!registration)
        return nullptr;
    return std::make_unique<NativePrepared>(*this, std::move(candidate), std::move(registration),
                                            std::move(queue), std::move(scene_id), 0.f, capacity, policies);
}
std::unique_ptr<NativeFaceWorkerInit::Prepared> AiDetector::PrepareNativeVehicleOnWorker(
    std::shared_ptr<nn::axera::vehicle_binding::Binding> binding,
    std::shared_ptr<const nn::axera::vehicle_binding::Capability> capability, std::uint64_t task_generation,
    std::string scene_id) {
    if (detector_ || !data_queue || !capability)
        return nullptr;
    const auto capacity = capability->GetContract().result_capacity;
    const auto policies = capability->GetContract().classes;
    auto queue          = data_queue;
    auto candidate      = std::make_shared<AiDetectorUnify>(alg_code_, "", "");
    auto registration   = candidate->PrepareNativeVehicle(std::move(binding), std::move(capability), queue,
                                                          distributor, task_generation, scene_id);
    if (!registration)
        return nullptr;
    return std::make_unique<NativePrepared>(*this, std::move(candidate), std::move(registration),
                                            std::move(queue), std::move(scene_id), 0.f, capacity,
                                            std::nullopt, false, policies);
}
#endif

bool AiDetector::AiSdkInit() {
    if (detector_) {
        LOG_INFO("{}[{} {}] Sdk Have Init", kTag, name_, uuid);
        return true;
    }

#ifdef COSMO_NN_USE_AXERA_BACKEND
    // A strict selection can only be initialized by the worker's admitted
    // native registration. Never fall back to the ordinary graph if that
    // registration is missing, invalid, or has already been revoked.
    if (NativeFaceSelection() != NativeSelection::NotSelected)
        return false;
#endif

    constexpr int kMaxRetryBeforeDelay  = 3;
    constexpr int64_t kRetryIntervalMs  = 30 * 1000;
    constexpr int64_t kRequiredGpuMemMB = 512;

    if (init_retry_count_ >= kMaxRetryBeforeDelay) {
        auto now = util::GetMilliseconds();
        if ((now - last_init_fail_time_ms_) < kRetryIntervalMs) {
            return false;
        }
        LOG_INFO("{}[{} {}] Init retry after {}s cooldown (retryCount:{})", kTag, name_, uuid,
                 kRetryIntervalMs / 1000, init_retry_count_);
    }

#ifndef COSMO_NN_USE_HOST_BACKEND
    auto availMB =
        service::ServiceRegistry::Instance().Get<service::IHardwareQuery>().GetAvailableGpuMemoryMB();
    if (availMB >= 0 && availMB < kRequiredGpuMemMB) {
        LOG_WARN("{}[{} {}] Insufficient GPU memory, skipping model load. Available:{}MB Required:{}MB", kTag,
                 name_, uuid, availMB, kRequiredGpuMemMB);
        init_retry_count_++;
        last_init_fail_time_ms_ = util::GetMilliseconds();
        return false;
    }
#else
    static_cast<void>(kRequiredGpuMemMB);
#endif

    std::string cfgPath   = "";
    std::string modelPath = "";
    auto cfgRet = service::ServiceRegistry::Instance().Get<service::IModelPathMapping>().GetModelCfg(
        alg_code_, cfgPath, modelPath);
    if (!cfgRet) {
        LOG_WARN("{}Get Model Configure Failed. AlgCode:{} code:{}", kTag, alg_code_, cfgRet);
        return false;
    }
    LOG_INFO("{}cfgPath:{}, modelPath:{}", kTag, cfgPath, modelPath);
    detector_ = std::make_shared<AiDetectorUnify>(alg_code_, cfgPath, modelPath);
    auto ret  = detector_->Init(input_size_);
    if (util::ErrorEnum::Success != ret) {
        detector_.reset();
        detector_     = nullptr;
        action_status = ret;
        init_retry_count_++;
        last_init_fail_time_ms_ = util::GetMilliseconds();
        LOG_WARN("{}[{} {}] {} Sdk Init Failed Get Ret {} (retryCount:{})", kTag, name_, uuid, alg_code_, ret,
                 init_retry_count_);
        return false;
    }
    init_retry_count_       = 0;
    action_status           = util::ErrorEnum::AI_INST_CREATED;
    labels_                 = detector_->GetLabels();
    size_t model_max_batch  = 1;
    const auto batch_status = detector_->GetMaxBatchSize(model_max_batch);
    if (batch_status != util::ErrorEnum::Success || model_max_batch == 0) {
        LOG_WARN("{}[{} {}] {} invalid model max batch:{}, keep single-frame drain", kTag, name_, uuid,
                 alg_code_, model_max_batch);
        model_max_batch = 1;
    }
    batch_count_ = ai_detector_fps::EffectiveDetectorDrainBatch(ai_detector_fps::kMaxDetectorDrainBatch,
                                                                model_max_batch);
    LOG_INFO("{}[{} {}] {} Init Sdk ModelMaxBatch:{} DrainBatch:{}", kTag, name_, uuid, alg_code_,
             model_max_batch, batch_count_);
    is_detector_inst_initialized_ = true;
    return true;
}

void AiDetector::QueueStatus(std::vector<AlgActionDataQueueStatus>& que_status, unsigned int duration_sec) {
    AlgActionDataQueueStatus status;
    auto durationInfo = duration_stat.ComputeStats();
    status.durationInfos.push_back(durationInfo);
    if (data_queue->Status(status.queueStatus, duration_sec)) {
        status.actionId = GetActionId();
        for (auto& channelNode : channel_list_) {
            status.channelIds.push_back(channelNode.channel);
            for (const auto& binding : channelNode.tasks) {
                status.taskIds.push_back(binding.task);
            }
        }
        status.actionStatus = action_status;
#ifdef COSMO_NN_USE_AXERA_BACKEND
        if (native_fire::ExposeFaceStatus(NativeFaceSelection() == NativeSelection::Selected, alg_code_)) {
            status.inferenceInstanceId     = uuid;
            status.faceOutputOverflowCount = native_face_overflow_count_.Snapshot();
            std::lock_guard<std::mutex> lock(native_registration_mutex_);
            if (native_registration_)
                status.faceOutputCapacity = native_face_capacity_;
        }
        {
            std::lock_guard<std::mutex> lock(native_registration_mutex_);
            if (native_registration_ && native_person_registered_ && alg_code_ == "1001003" &&
                native_face_capacity_) {
                status.inferenceInstanceId       = uuid;
                status.personOutputCapacity      = native_face_capacity_;
                status.personOutputOverflowCount = native_person_overflow_count_.Snapshot();
            }
        }
#endif
        que_status.push_back(status);
    }
    return;
}

#ifdef COSMO_NN_USE_AXERA_BACKEND
bool AiDetector::Start() {
    std::lock_guard<std::mutex> lock(native_lifecycle_mutex_);
    if (!running) {
        if (!native_worker_init_.Reset())
            return false;
        native_face_overflow_count_.Reset();
        native_person_overflow_count_.Reset();
    }
    return AlgActionBase::Start();
}
#endif

void AiDetector::Stop() {
#ifdef COSMO_NN_USE_AXERA_BACKEND
    std::lock_guard<std::mutex> lock(native_lifecycle_mutex_);
    native_worker_init_.Stop();
#endif
    if (running) {
        running = false;
        if (data_queue) {
            data_queue->Stop();
        }
        stop();
        action_status = util::ErrorEnum::ActionStop;
        LOG_INFO("{}[{} {}] Stop done, thread joined", kTag, name_, uuid);
    }
}

void AiDetector::HandFrames(std::vector<AlgDataPtr> alg_datas) {
    if (0 == alg_datas.size()) {
        action_status = util::ErrorEnum::FlowDataInvalid;
        return;
    }

#ifdef COSMO_NN_USE_AXERA_BACKEND
    const auto selected        = NativeFaceSelection();
    const auto input_selection = selected == NativeSelection::Selected ? native_face::InputSelection::Selected
                                 : selected == NativeSelection::NotSelected
                                     ? native_face::InputSelection::NotSelected
                                     : native_face::InputSelection::Invalid;
    const auto envelope_count =
        static_cast<std::size_t>(std::count_if(alg_datas.begin(), alg_datas.end(), [](const auto& data) {
            return data && data->chanDataDec.native_face_envelope;
        }));
    bool valid_native_frame   = false;
    bool registration_matches = false;
    if (input_selection == native_face::InputSelection::Selected && alg_datas.size() == 1 && alg_datas[0]) {
        const auto& data = alg_datas[0]->chanDataDec;
        valid_native_frame =
            data.frame && data.frame->Active() && data.frame->IsMetadataOnly() && data.native_buffer;
        const auto registration = NativeFaceRegistrationForQueue();
        registration_matches    = registration && data.native_face_envelope &&
                               data.native_face_envelope->registration_ == registration;
    }
    const auto route = native_face::DecideInputRoute(input_selection, alg_datas.size(), envelope_count,
                                                     valid_native_frame, registration_matches);
    if (route == native_face::InputRoute::Reject) {
        action_status = util::ErrorEnum::AI_FORWARD_FAILED;
        return;
    }
    const bool has_native_face = route == native_face::InputRoute::Native;
#ifdef COSMO_AXERA_HELMET_ROUTE_CANDIDATE
    std::shared_ptr<const NativeHelmetAdmission> helmet_admission;
    {
        std::lock_guard<std::mutex> lock(native_registration_mutex_);
#ifdef COSMO_AXERA_SCENARIOBENCH_CANDIDATE
        if (alg_code_ == "9275710" && BenchmarkCrossChannelEnabled()) {
            if (!alg_datas.empty() && alg_datas[0]) {
                helmet_admission = bench_helmet_admissions_.Find(alg_datas[0]->channelId);
            }
        } else
#endif
            helmet_admission = native_helmet_admission_;
    }
    media::NativeVideoBufferPtr helmet_frame;
    if (helmet_admission &&
        (!has_native_face ||
         (!native_person_registered_ && alg_code_ != "1000001" && alg_code_ != "2000001") || !alg_datas[0] ||
         helmet_admission->channel_ != alg_datas[0]->channelId)) {
        action_status = util::ErrorEnum::AI_FORWARD_FAILED;
        return;
    }
#endif
    if (has_native_face && !detector_) {
        action_status = util::ErrorEnum::AI_INST_NOTCREATED;
        return;
    }
#endif
    if (!detector_) {
        action_status = util::ErrorEnum::AI_INST_NOTCREATED;
        if (!AiSdkInit())
            return;
    }
    std::vector<VideoFramePtr> images;
    std::vector<media::NativeVideoBufferPtr> native_buffers;
    std::vector<AlgDataPtr> activeAlgDatas;
    for (auto algData : alg_datas) {
        if (algData && algData->chanDataDec.frame && algData->chanDataDec.frame->Active()) {
            // Different size instances can receive the same decoded frame wrapper.
            if (input_size_)
                algData = AlgDataCopy(algData);
            images.push_back(algData->chanDataDec.frame);
            native_buffers.push_back(algData->chanDataDec.native_buffer);
            activeAlgDatas.push_back(algData);
        }
    }

    if (images.empty()) {
        action_status = util::ErrorEnum::FlowDataInvalid;
        return;
    }

    CheckAndActivateConfidence();
    std::vector<AiConfidence> confThres;
    std::vector<std::vector<AiDetectRstEl>> detRsts;
#ifdef COSMO_NN_USE_AXERA_BACKEND
    std::unique_ptr<RegistrationAuthority::Lease> native_input_guard;
    std::unique_ptr<AlgDataQueue<AlgDataPtr>::LifecycleLease> native_queue_guard;
    std::unique_ptr<AlgDataQueueDistributor::NativeOutputTransaction> native_output_guard;
#ifdef COSMO_AXERA_FACE_ISOLATED_DEVICE_TEST
    nn::axera::diagnostics::FrameKey native_diagnostic_key{};
#endif
#endif

    if (active_confidence_modify_sign_ != active_confidence_active_sign_) {
        active_confidence_active_sign_ = active_confidence_modify_sign_;
        confThres                      = active_confidence_;
        LOG_INFO("{}[{} {}] Confidence Active. confThres size:{}", kTag, name_, uuid, confThres.size());
    }
#ifdef COSMO_NN_USE_AXERA_BACKEND
    if (has_native_face) {
        if (activeAlgDatas.size() != 1 || images.size() != 1 || native_buffers.size() != 1 ||
            !activeAlgDatas[0]->chanDataDec.native_face_envelope) {
            action_status = util::ErrorEnum::InvalidImage;
            return;
        }
        const auto& input_registration = activeAlgDatas[0]->chanDataDec.native_face_envelope->registration_;
        const bool native_e24          = input_registration && input_registration->e24_;
        float selected_threshold       = 0.f;
        if (native_e24) {
            // Attribute-specific scene thresholds are applied by the regular
            // downstream graph; the model's person floor remains 0.25.
            selected_threshold = .25f;
        } else if (native_vehicle_policies_) {
            if (!native_vehicle::MatchesPolicies(*native_vehicle_policies_, active_confidence_)) {
                action_status = util::ErrorEnum::AI_FORWARD_FAILED;
                return;
            }
        } else if (native_fire_policies_) {
            if (!native_fire::MatchesPolicies(*native_fire_policies_, active_confidence_)) {
                action_status = util::ErrorEnum::AI_FORWARD_FAILED;
                return;
            }
#if defined(COSMO_AXERA_HS_ROUTE_CANDIDATE) &&                                                               \
    (defined(COSMO_AXERA_FACE_ISOLATED_DEVICE_TEST) || defined(COSMO_AXERA_HS_ROUTE_PRODUCTION))
        } else if (alg_code_ == nn::axera::hs_contract::kAtomicCode) {
            if (!native_hs::MatchesPolicy(active_confidence_, native_scene_threshold_)) {
                action_status = util::ErrorEnum::AI_FORWARD_FAILED;
                return;
            }
#ifdef COSMO_AXERA_HS_ROUTE_PRODUCTION
            selected_threshold = active_confidence_.front().confidence;
#else
            selected_threshold = native_scene_threshold_;
#endif
#endif
#if defined(COSMO_AXERA_MOUSE_ROUTE_CANDIDATE) &&                                                            \
    (defined(COSMO_AXERA_FACE_ISOLATED_DEVICE_TEST) || defined(COSMO_AXERA_MOUSE_ROUTE_PRODUCTION))
        } else if (alg_code_ == nn::axera::mouse_contract::kAtomicCode) {
            if (!native_mouse::MatchesPolicy(active_confidence_, native_scene_threshold_)) {
                action_status = util::ErrorEnum::AI_FORWARD_FAILED;
                return;
            }
#ifdef COSMO_AXERA_MOUSE_ROUTE_PRODUCTION
            selected_threshold = active_confidence_.front().confidence;
#else
            selected_threshold = native_scene_threshold_;
#endif
#endif
#if defined(COSMO_AXERA_RUBBISH_ROUTE_CANDIDATE) &&                                                          \
    (defined(COSMO_AXERA_FACE_ISOLATED_DEVICE_TEST) || defined(COSMO_AXERA_RUBBISH_ROUTE_PRODUCTION))
        } else if (alg_code_ == nn::axera::rubbish_contract::kAtomicCode) {
            if (!native_rubbish::MatchesPolicy(active_confidence_, native_scene_threshold_)) {
                action_status = util::ErrorEnum::AI_FORWARD_FAILED;
                return;
            }
#ifdef COSMO_AXERA_RUBBISH_ROUTE_PRODUCTION
            selected_threshold = active_confidence_.front().confidence;
#else
            selected_threshold = native_scene_threshold_;
#endif
#endif
        } else {
            const auto face_threshold =
                std::find_if(active_confidence_.begin(), active_confidence_.end(), [this](const auto& conf) {
                    return conf.label == (alg_code_ == "9275710"
                                              ? "Pedestrian"
                                              : (alg_code_ == "1001003" ? "pedestrian" : "face"));
                });
#ifdef COSMO_AXERA_SCENARIOBENCH_CANDIDATE
            if (alg_code_ == "9275710") {
                if (face_threshold == active_confidence_.end() ||
                    !std::isfinite(face_threshold->confidence) || face_threshold->confidence < .25f ||
                    face_threshold->confidence > 1.f) {
                    action_status = util::ErrorEnum::AI_FORWARD_FAILED;
                    return;
                }
                // Detect at the admitted model floor; ConfidenceFilter applies
                // each branch's task threshold independently after detection.
                selected_threshold = .25f;
            } else
#endif
            {
                const bool dynamic_person =
                    alg_code_ == "1001003" && native_person_min_threshold_.has_value();
                if (face_threshold == active_confidence_.end() ||
                    (dynamic_person ? !person_template_contract::ValidThreshold(face_threshold->confidence,
                                                                                *native_person_min_threshold_)
                                    : face_threshold->confidence != native_scene_threshold_)) {
                    action_status = util::ErrorEnum::AI_FORWARD_FAILED;
                    return;
                }
                selected_threshold = face_threshold->confidence;
            }
        }
        std::vector<AiDetectRstEl> targets;
        auto envelope = activeAlgDatas[0]->chanDataDec.native_face_envelope;
#ifdef COSMO_AXERA_HELMET_ROUTE_CANDIDATE
        if (helmet_admission) {
            const auto bound   = helmet_admission->detector_registration_;
            const auto current = std::static_pointer_cast<const void>(envelope->registration_);
            if (!bound || bound.get() != current.get() || bound.owner_before(current) ||
                current.owner_before(bound)) {
                action_status = util::ErrorEnum::AI_FORWARD_FAILED;
                return;
            }
            helmet_frame = envelope->source_;
        }
#endif
#ifdef COSMO_AXERA_FACE_ISOLATED_DEVICE_TEST
        native_diagnostic_key = {envelope->key_.channel_id, envelope->key_.stream_generation,
                                 envelope->key_.frame_index, envelope->key_.timestamp};
#endif
        if (!envelope->authority_) {
            action_status = util::ErrorEnum::AI_FORWARD_FAILED;
            return;
        }
        native_input_guard = std::make_unique<RegistrationAuthority::Lease>(
            envelope->authority_->Acquire(envelope->generation_));
        if (!*native_input_guard) {
            action_status = util::ErrorEnum::AI_FORWARD_FAILED;
            return;
        }
        auto native_queue = envelope->registration_->queue_.lock();
        if (!native_queue || native_queue != data_queue) {
            action_status = util::ErrorEnum::AI_FORWARD_FAILED;
            return;
        }
        native_queue_guard = std::make_unique<AlgDataQueue<AlgDataPtr>::LifecycleLease>(
            native_queue->AcquireLifecycle(envelope->registration_->queue_lifecycle_));
        if (!*native_queue_guard) {
            action_status = util::ErrorEnum::AI_FORWARD_FAILED;
            return;
        }
        native_output_guard = distributor->AcquireNativeOutput(envelope->output_snapshot_);
        if (!native_output_guard) {
            action_status = util::ErrorEnum::AI_FORWARD_FAILED;
            return;
        }
#ifdef COSMO_AXERA_FACE_ISOLATED_DEVICE_TEST
        // This envelope has actually been popped from the detector queue;
        // emitting here also orders the event before the worker's runner.
        nn::axera::diagnostics::Trace(nn::axera::diagnostics::Event::Queued, native_diagnostic_key);
#endif
        action_status =
            detector_->DetectNativeFace(envelope, images[0], native_buffers[0], activeAlgDatas[0]->channelId,
                                        selected_threshold, *native_input_guard, *native_queue_guard, targets,
                                        native_fire_policies_ ? &*native_fire_policies_ : nullptr,
                                        native_vehicle_policies_ ? &*native_vehicle_policies_ : nullptr);
#if defined(COSMO_MEDIA_USE_AXERA_BACKEND)
        if (action_status == util::ErrorEnum::Success && !targets.empty()) {
            VideoFramePtr snapshot;
            if (native_e24)
                snapshot = DeferE24EventFrame(native_buffers[0], activeAlgDatas[0]->chanDataDec.frame);
#ifdef COSMO_AXERA_SCENARIOBENCH_CANDIDATE
            const char* borrowed_opt = std::getenv("COSMO_AXERA_BENCH_NATIVE_EVENT_PROVIDER");
            if (alg_code_ == "9275710" && borrowed_opt && std::strcmp(borrowed_opt, "1") == 0) {
                snapshot = TryBorrowedNativeEvent(native_buffers[0], activeAlgDatas[0]->chanDataDec.frame,
                                                  activeAlgDatas[0]->channelId);
                const char* verify = std::getenv("COSMO_AXERA_BENCH_NATIVE_EVENT_VERIFY");
                static std::atomic<unsigned> verify_count{0};
                if (snapshot && verify && std::strcmp(verify, "1") == 0 && ++verify_count <= 10) {
                    auto eager      = SnapshotNativeEventFrame(native_buffers[0],
                                                               activeAlgDatas[0]->chanDataDec.frame, true);
                    const auto* a   = eager ? eager->GetData() : nullptr;
                    const auto* b   = snapshot->GetData();
                    const bool same = a && b && eager->GetSize() == snapshot->GetSize() &&
                                      std::memcmp(a, b, eager->GetSize()) == 0;
                    LOG_INFO("AX_BENCH_BORROWED_EVENT_VERIFY frame:{} exact:{} bytes:{}", verify_count.load(),
                             same, same ? snapshot->GetSize() : 0);
                    if (!same)
                        snapshot = std::move(eager);
                }
            }
            const char* lazy_opt = std::getenv("COSMO_AXERA_BENCH_LAZY_EVENT_FRAME");
            if (!snapshot && alg_code_ == "9275710" && lazy_opt && std::strcmp(lazy_opt, "1") == 0) {
                const auto lazy = TryLazyNativeEvent(native_buffers[0], activeAlgDatas[0]->chanDataDec.frame,
                                                     activeAlgDatas[0]->channelId);
                if (lazy.fatal) {
                    action_status = util::ErrorEnum::AI_FORWARD_FAILED;
                    return;
                }
                snapshot           = lazy.frame;
                const char* verify = std::getenv("COSMO_AXERA_BENCH_LAZY_EVENT_VERIFY");
                static std::atomic<unsigned> verify_count{0};
                if (snapshot && verify && std::strcmp(verify, "1") == 0 && ++verify_count <= 10) {
                    auto eager      = SnapshotNativeEventFrame(native_buffers[0],
                                                               activeAlgDatas[0]->chanDataDec.frame, true);
                    const auto* a   = eager ? eager->GetData() : nullptr;
                    const auto* b   = snapshot->GetData();
                    const bool same = a && b && eager->GetSize() == snapshot->GetSize() &&
                                      std::memcmp(a, b, eager->GetSize()) == 0;
                    LOG_INFO("AX_BENCH_LAZY_EVENT_VERIFY frame:{} exact:{} bytes:{}", verify_count.load(),
                             same, same ? snapshot->GetSize() : 0);
                    if (!same)
                        snapshot = std::move(eager);
                }
            }
#endif
            if (!snapshot)
                snapshot = SnapshotNativeEventFrame(native_buffers[0], activeAlgDatas[0]->chanDataDec.frame,
                                                    native_e24 || alg_code_ == "9275710");
            if (snapshot)
                activeAlgDatas[0]->chanDataDec.frame = std::move(snapshot);
            else
                LOG_WARN("{} AX event frame snapshot failed; preserving metadata-only event", kTag);
        }
#endif
        // The result and metadata may be retained downstream. Drop all local
        // frame leases before publishing a successful native result.
        for (auto& data : alg_datas) {
            if (data) {
                data->chanDataDec.native_face_envelope.reset();
                data->chanDataDec.native_buffer.reset();
            }
        }
        envelope.reset();
        if (action_status == util::ErrorEnum::Success)
            detRsts.push_back(std::move(targets));
    } else
#endif
    {
        action_status = detector_->Detect(images, native_buffers, confThres, detRsts);
    }
    native_buffers.clear();
    if (util::ErrorEnum::Success != action_status) {
#ifdef COSMO_NN_USE_AXERA_BACKEND
        if (has_native_face && action_status == util::ErrorEnum::AI_FACE_OUTPUT_OVERFLOW) {
            const auto count = native_face_overflow_count_.Record(action_status);
            if (count == 1 || count % 100 == 0)
                LOG_ERRO("{}[{} {}] Face output capacity exceeded; frame rejected. Total:{}", kTag, name_,
                         uuid, count);
            return;
        }
        if (has_native_face && native_person_registered_ &&
            action_status == util::ErrorEnum::AI_PERSON_OUTPUT_OVERFLOW) {
            const auto count = native_person_overflow_count_.Record(action_status);
            if (count == 1 || count % 100 == 0)
                LOG_ERRO("{}[{} {}] Person output capacity exceeded; frame rejected. Total:{}", kTag, name_,
                         uuid, count);
            return;
        }
#endif
        LOG_ERRO("{}[{} {}] Detect Failed. Ret:{} images:{} confThres:{}", kTag, name_, uuid, action_status,
                 images.size(), confThres.size());
        return;
    }
    if (detRsts.size() > activeAlgDatas.size()) {
        LOG_WARN("{}[{} {}] Detect result size:{} larger than active data size:{}, extra results ignored",
                 kTag, name_, uuid, detRsts.size(), activeAlgDatas.size());
    }

    for (size_t i = 0; i < detRsts.size() && i < activeAlgDatas.size(); i++) {
        auto algData                        = activeAlgDatas[i];
        DataDetTrackClassifyPtr detTrackRst = std::make_shared<DataDetTrackClassify>();
        detTrackRst->targets                = detRsts[i];
        detTrackRst->streamIndex            = images[i]->GetStreamIndex();
        detTrackRst->frameIndex             = images[i]->GetFrameIndex();
        detTrackRst->timestamp              = images[i]->GetTimestamp();
        detTrackRst->picWidth               = images[i]->GetWidth();
        detTrackRst->picHeight              = images[i]->GetHeight();
        for (auto& target : detTrackRst->targets) {
            target.frameIndex  = images[i]->GetFrameIndex();
            target.streamIndex = images[i]->GetStreamIndex();
        }
        std::vector<std::string> privacyLabels;
        for (const auto& confidence : active_confidence_) {
            if (std::find(labels_.begin(), labels_.end(), confidence.label) != labels_.end()) {
                privacyLabels.push_back(confidence.label);
            }
        }
        CaptureAlarmPrivacySnapshot(*algData, uuid, privacyLabels, *detTrackRst);
        algData->legacyDetect = algData->chanDataDetect;

        algData->chanDataDetect.atomicCode  = alg_code_;
        algData->chanDataDetect.lables      = labels_;
        algData->chanDataDetect.atomicCodes = {alg_code_};
        algData->chanDataDetect.detRet      = detTrackRst;
#ifdef COSMO_AXERA_FACE_ISOLATED_DEVICE_TEST
        if (has_native_face)
            algData->native_diagnostic_key =
                std::make_shared<const nn::axera::diagnostics::FrameKey>(native_diagnostic_key);
#endif

        algData->dataType = AlgDataType::ChannelDataDetect;
#ifdef COSMO_AXERA_FACE_ISOLATED_DEVICE_TEST
        std::int64_t native_delivered_targets = 0;
#endif
        const auto filter = [this
#ifdef COSMO_AXERA_FACE_ISOLATED_DEVICE_TEST
                             ,
                             &native_delivered_targets
#endif
#if defined(COSMO_NN_USE_AXERA_BACKEND) && defined(COSMO_AXERA_HELMET_ROUTE_CANDIDATE)
                             ,
                             &helmet_admission, &helmet_frame
#endif
        ](AlgDataPtr frame, const std::string& taskId) {
            auto filtered = ConfidenceFilter(frame, taskId);
#if defined(COSMO_NN_USE_AXERA_BACKEND) && defined(COSMO_AXERA_HELMET_ROUTE_CANDIDATE)
            if (helmet_admission && (alg_code_ != "9275710" || taskId == helmet_admission->task_)) {
                // ConfidenceFilter can return the original wrapper for a
                // missing detection result. Such an alias is never mintable.
                if (!filtered || filtered.get() == frame.get() || filtered.use_count() != 1 ||
                    !filtered->chanDataDetect.detRet || !helmet_frame)
                    return AlgDataPtr{};
                auto ticket = helmet_admission->Mint(taskId, helmet_frame);
                if (!ticket)
                    return AlgDataPtr{};
                filtered->native_helmet_ticket = std::move(ticket);
            }
#endif
#ifdef COSMO_AXERA_FACE_ISOLATED_DEVICE_TEST
            if (filtered && filtered->chanDataDetect.detRet)
                native_delivered_targets =
                    static_cast<std::int64_t>(filtered->chanDataDetect.detRet->targets.size());
#endif
            return filtered;
        };
#ifdef COSMO_NN_USE_AXERA_BACKEND
        if (has_native_face) {
            const int delivered = distributor->DistributorNativeOutput(*native_output_guard,
                                                                       algData->channelId, algData, filter);
#ifdef COSMO_AXERA_FACE_ISOLATED_DEVICE_TEST
            nn::axera::diagnostics::Trace(delivered ? nn::axera::diagnostics::Event::Delivered
                                                    : nn::axera::diagnostics::Event::Rejected,
                                          native_diagnostic_key, delivered ? native_delivered_targets : 0);
#endif
            if (!delivered)
                action_status = util::ErrorEnum::AI_FORWARD_FAILED;
        } else
#endif
        {
            distributor->DistributorData(algData->channelId, algData, filter);
        }
    }
    return;
}

// When detector is shared by multiple tasks, use lowest confidence for detection
// then filter per-task confidence on distribution
AlgDataPtr AiDetector::ConfidenceFilter(AlgDataPtr data_ptr, const std::string& taskId) {
    AlgDataPtr algData = AlgDataCopy(data_ptr);
    if (!algData)
        return nullptr;

    if (!algData->chanDataDetect.detRet) {
        return data_ptr;
    }

    if (!algData->legacyDetect.atomicCodes.empty()) {
        algData->chanDataDetect.atomicCodes.insert(algData->chanDataDetect.atomicCodes.end(),
                                                   algData->legacyDetect.atomicCodes.begin(),
                                                   algData->legacyDetect.atomicCodes.end());
    }
    algData->chanDataDetect.detRet = std::make_shared<DataDetTrackClassify>();
    if (!algData->chanDataDetect.detRet)
        return algData;
    algData->chanDataDetect.detRet->streamIndex = data_ptr->chanDataDetect.detRet->streamIndex;
    algData->chanDataDetect.detRet->frameIndex  = data_ptr->chanDataDetect.detRet->frameIndex;
    algData->chanDataDetect.detRet->timestamp   = data_ptr->chanDataDetect.detRet->timestamp;
    algData->chanDataDetect.detRet->picWidth    = data_ptr->chanDataDetect.detRet->picWidth;
    algData->chanDataDetect.detRet->picHeight   = data_ptr->chanDataDetect.detRet->picHeight;

    algData->chanDataDetect.detRet->dataType = AlgDataType::ChannelDataDetect;

    bool findTask = false;
    for (auto& taskConfidence : active_task_confidence_) {
        if (taskId == taskConfidence.task_id) {
            for (auto& confidenceEl : taskConfidence.label_params) {
                findTask = true;
                for (auto& target : data_ptr->chanDataDetect.detRet->targets) {
                    if ((target.confidence.label == confidenceEl.label) &&
                        (target.confidence.confidence >= confidenceEl.confidence)) {
                        AiDetectRstEl validTarget = target;
                        algData->chanDataDetect.detRet->targets.push_back(validTarget);
                    }
                }
            }
        }
    }

#ifdef COSMO_AXERA_SCENARIOBENCH_CANDIDATE
    // A newly registered branch may precede its confidence snapshot.
    // Do not publish model-floor detections under an unknown task threshold.
    if (!findTask && alg_code_ == "9275710" && !BenchSibling(algData->channelId, taskId).empty())
        return nullptr;
#endif
    if (!((!algData->taskId.empty()) && (algData->taskId != taskId))) {
        if (algData->legacyDetect.detRet) {
            algData->chanDataDetect.detRet->targets.insert(algData->chanDataDetect.detRet->targets.end(),
                                                           algData->legacyDetect.detRet->targets.begin(),
                                                           algData->legacyDetect.detRet->targets.end());
        }
    }
    algData->taskId = taskId;
    PublishAlarmPrivacySnapshot(*algData);
    if (!findTask) {
        algData->chanDataDetect.detRet->targets = data_ptr->chanDataDetect.detRet->targets;
    }

    SignTargetAreas(algData, taskId);
    RecordHistory(algData, taskId);
    OverviewRecord(taskId, algData->chanDataDetect.detRet);
    return algData;
}

// SetProcQueSize — moved to AiDetectorTask.cc

void AiDetector::run() {
    int index = 0;
    while (running) {
#ifdef COSMO_NN_USE_AXERA_BACKEND
        native_worker_init_.PumpOnce();
#endif
        if ((data_queue->RestSize() >= batch_count_) || ((index >= 100) && (data_queue->RestSize() > 0))) {
            size_t detnum     = data_queue->RestSize();
            auto inputFpsCalc = input_fps_calc.FpsWithFrame();
            handle_frame_cnt  = inputFpsCalc.first;
            in_fps            = inputFpsCalc.second;

            std::vector<AlgDataPtr> alg_datas;
            for (size_t i = 0; i < detnum && i < batch_count_; i++) {
                auto data          = data_queue->Pop();
                AlgDataPtr algData = AlgDataCopy(data);
                if (algData) {
                    alg_datas.push_back(algData);
                }
            }
            if (!alg_datas.empty()) {
                if (service::ServiceRegistry::Instance().Get<service::IConfigReadService>().GetActionSwitch(
                        GetActionId())) {
                    duration_stat.BeginSample();
                    HandFrames(alg_datas);
                    duration_stat.EndSample();
                }
            }
            index = 0;
        } else {
            data_queue->WaitForData(10);
            index += 1;
        }
    }
    // Release model on thread exit to free VRAM (AiSdkInit will reload on next start)
    if (detector_) {
#ifdef COSMO_NN_USE_AXERA_BACKEND
        {
            std::lock_guard<std::mutex> lock(native_registration_mutex_);
            native_registration_.reset();
            native_face_capacity_.reset();
            native_person_registered_ = false;
            native_scene_id_.clear();
            native_scene_threshold_ = 0.f;
            native_person_min_threshold_.reset();
            native_fire_policies_.reset();
            native_vehicle_policies_.reset();
        }
        detector_->CloseNativeFace();
#endif
        LOG_INFO("{}[{} {}] Thread exiting, releasing YOLO model to free VRAM", kTag, name_, uuid);
        detector_.reset();
        detector_                     = nullptr;
        is_detector_inst_initialized_ = false;
    }
    LOG_INFO("[{} {}] THREAD [{}] Stop ", name_, uuid, Name());
}

// ChannelExist, TaskExist, TaskIsFull, TaskIsEmpty, ChannelCount, TaskCount,
// AddTask, RemoveTask — moved to AiDetectorTask.cc

}  // namespace cosmo
