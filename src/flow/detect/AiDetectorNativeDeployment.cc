#ifdef COSMO_NN_USE_AXERA_BACKEND
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "cryptopp/sha.h"
#include "flow/detect/AiDetector.h"
#include "flow/detect/AxeraTrustedDirectories.h"
#include "flow/detect/NativeFaceActionGuard.h"
#include "flow/detect/NativeFireSelector.h"
#include "flow/detect/NativeHsSelector.h"
#include "flow/detect/NativeModelSelector.h"
#include "flow/detect/NativeMouseSelector.h"
#include "flow/detect/NativeRequiredConfiguration.h"
#include "flow/detect/NativeRubbishSelector.h"
#include "flow/detect/NativeVehicleSelector.h"
#include "flow/detect/PersonTemplateContract.h"
#include "flow/detect/PersonTemplateGraph.h"
#include "nn/device/axera/axera_face_deployment.h"
#include "nn/device/axera/axera_face_model_sdk_loader.h"
#include "nn/device/axera/axera_fire_deployment.h"
#include "nn/device/axera/axera_person_deployment.h"
#include "nn/device/axera/axera_person_manifest_name.h"
#include "nn/device/axera/axera_route_identity.h"
#include "nn/device/axera/axera_vehicle_deployment.h"
#include "service/detail/ServiceRegistry.h"
#include "service/system/IAppInfoService.h"
#include "util/EnvUtil.h"
#include "util/Log.h"
#include "util/PathUtil.h"

namespace cosmo {
namespace {
    using axera_trusted_directories::Fd;
    using axera_trusted_directories::OpenDirectory;
    using axera_trusted_directories::OpenTrustedAbsoluteDirectory;
    using axera_trusted_directories::Roots;
    using axera_trusted_directories::TrustedDirectory;

    constexpr off_t kNativeModelSizeLimit = 32 * 1024 * 1024;

    std::shared_ptr<const std::vector<std::uint8_t>> ReadVerifiedE24Model() {
        const auto& directory       = nn::axera::route_identity::Lookup("person_attributes.model_directory");
        const auto& expected_digest = nn::axera::route_identity::Digest("person_attributes.sha256");
        if (directory.empty() || expected_digest.empty())
            return {};
        Fd models{OpenTrustedAbsoluteDirectory(path::GetModelPath())};
        struct stat models_stat {};
        if (models.value < 0 || ::fstat(models.value, &models_stat) != 0)
            return {};

        Fd model_dir{OpenDirectory(models.value, directory.c_str())};
        if (!TrustedDirectory(model_dir.value, models_stat.st_uid))
            return {};
        Fd fd{::openat(model_dir.value, "model.axmodel", O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK)};
        struct stat model_stat {};
        if (fd.value < 0 || ::fstat(fd.value, &model_stat) != 0 || !S_ISREG(model_stat.st_mode) ||
            model_stat.st_uid != models_stat.st_uid || (model_stat.st_mode & 0022) ||
            model_stat.st_size <= 0 || model_stat.st_size > kNativeModelSizeLimit)
            return {};

        auto bytes =
            std::make_shared<std::vector<std::uint8_t>>(static_cast<std::size_t>(model_stat.st_size));
        std::size_t read_bytes = 0;
        while (read_bytes < bytes->size()) {
            const auto n = ::read(fd.value, bytes->data() + read_bytes, bytes->size() - read_bytes);
            if (n < 0 && errno == EINTR)
                continue;
            if (n <= 0)
                return {};
            read_bytes += static_cast<std::size_t>(n);
        }

        std::array<std::uint8_t, CryptoPP::SHA256::DIGESTSIZE> digest{};
        CryptoPP::SHA256().CalculateDigest(digest.data(), bytes->data(), bytes->size());
        constexpr char digits[] = "0123456789abcdef";
        std::string hash;
        hash.reserve(digest.size() * 2);
        for (const auto byte : digest) {
            hash.push_back(digits[byte >> 4]);
            hash.push_back(digits[byte & 15]);
        }
        if (hash != expected_digest)
            return {};
        return bytes;
    }

    std::uint64_t NextNativeGeneration() {
        static std::atomic<std::uint64_t> next{1};
        auto value = next.load(std::memory_order_relaxed);
        while (value && value != std::numeric_limits<std::uint64_t>::max())
            if (next.compare_exchange_weak(value, value + 1, std::memory_order_relaxed))
                return value;
        return 0;
    }
}  // namespace

AiDetector::NativeSelection AiDetector::NativeFaceSelection(const ActionNode* action) const {
    const auto& required = native_face::ProcessRequiredCodes();
    // Reject ambiguous aliases that mention the native vehicle model even if
    // it was omitted from the startup strict set.
    if (action && !native_face::ConsistentStrictAction("2000001", alg_code_, *action))
        return NativeSelection::Invalid;
#if defined(COSMO_AXERA_HS_ROUTE_CANDIDATE) &&                                                               \
    (defined(COSMO_AXERA_FACE_ISOLATED_DEVICE_TEST) || defined(COSMO_AXERA_HS_ROUTE_PRODUCTION))
    if (action &&
        !native_face::ConsistentStrictAction(nn::axera::hs_contract::kAtomicCode, alg_code_, *action))
        return NativeSelection::Invalid;
#endif
#if defined(COSMO_AXERA_MOUSE_ROUTE_CANDIDATE) &&                                                            \
    (defined(COSMO_AXERA_FACE_ISOLATED_DEVICE_TEST) || defined(COSMO_AXERA_MOUSE_ROUTE_PRODUCTION))
    if (action &&
        !native_face::ConsistentStrictAction(nn::axera::mouse_contract::kAtomicCode, alg_code_, *action))
        return NativeSelection::Invalid;
#else
    if (alg_code_ == nn::axera::mouse_contract::kAtomicCode)
        return NativeSelection::Invalid;
#endif
#if defined(COSMO_AXERA_RUBBISH_ROUTE_CANDIDATE) &&                                                          \
    (defined(COSMO_AXERA_FACE_ISOLATED_DEVICE_TEST) || defined(COSMO_AXERA_RUBBISH_ROUTE_PRODUCTION))
    if (action &&
        !native_face::ConsistentStrictAction(nn::axera::rubbish_contract::kAtomicCode, alg_code_, *action))
        return NativeSelection::Invalid;
#else
    if (alg_code_ == nn::axera::rubbish_contract::kAtomicCode)
        return NativeSelection::Invalid;
#endif
    if (!required.valid)
        return NativeSelection::Invalid;
    const char* configured_fire_code = std::getenv("COSMO_AXERA_NATIVE_FIRE_CODE");
    const std::string fire_code      = configured_fire_code ? configured_fire_code : "";
    const bool listed =
        std::find(required.codes.begin(), required.codes.end(), alg_code_) != required.codes.end();
    if (native_vehicle::RequireStrict(alg_code_, listed) == native_vehicle::Selection::Invalid)
        return NativeSelection::Invalid;
#if defined(COSMO_AXERA_HS_ROUTE_CANDIDATE) &&                                                               \
    (defined(COSMO_AXERA_FACE_ISOLATED_DEVICE_TEST) || defined(COSMO_AXERA_HS_ROUTE_PRODUCTION))
    if (native_hs::RequireStrict(alg_code_, listed) == native_hs::Selection::Invalid)
        return NativeSelection::Invalid;
#endif
#if defined(COSMO_AXERA_MOUSE_ROUTE_CANDIDATE) &&                                                            \
    (defined(COSMO_AXERA_FACE_ISOLATED_DEVICE_TEST) || defined(COSMO_AXERA_MOUSE_ROUTE_PRODUCTION))
    if (native_mouse::RequireStrict(alg_code_, listed) == native_mouse::Selection::Invalid)
        return NativeSelection::Invalid;
#endif
#if defined(COSMO_AXERA_RUBBISH_ROUTE_CANDIDATE) &&                                                          \
    (defined(COSMO_AXERA_FACE_ISOLATED_DEVICE_TEST) || defined(COSMO_AXERA_RUBBISH_ROUTE_PRODUCTION))
    if (native_rubbish::RequireStrict(alg_code_, listed) == native_rubbish::Selection::Invalid)
        return NativeSelection::Invalid;
#endif
    if (native_fire::RequireStrict(alg_code_, fire_code, listed) == native_fire::Selection::Invalid)
        return NativeSelection::Invalid;
    if (required.codes.empty())
        return NativeSelection::NotSelected;
    for (const auto& code : required.codes)
        if (action && !native_face::ConsistentStrictAction(code, alg_code_, *action))
            return NativeSelection::Invalid;
    return std::find(required.codes.begin(), required.codes.end(), alg_code_) != required.codes.end()
               ? NativeSelection::Selected
               : NativeSelection::NotSelected;
}

AiDetector::NativeProvisionStatus AiDetector::PrepareProvisionedNativeFace(const std::string& task_scene,
                                                                           const ActionAlg* task_graph) {
    using Status = NativeProvisionStatus;
    try {
        // This service-startup selector expresses only the intent to require the
        // strict route. It cannot authorize a model, scenario, color or execution.
        const auto selected = NativeFaceSelection();
        if (selected == NativeSelection::NotSelected)
            return Status::NotConfigured;
        if (selected == NativeSelection::Invalid)
            return Status::Failed;
        // A service-owned selector distinguishes the fire contract from the
        // existing one-class schemas. It conveys no model or scene authority.
        const char* configured_fire_code = std::getenv("COSMO_AXERA_NATIVE_FIRE_CODE");
        const std::string fire_code      = configured_fire_code ? configured_fire_code : "";
        const auto fire_selection        = native_fire::Select(alg_code_, fire_code);
        if (fire_selection == native_fire::Selection::Invalid)
            return Status::Failed;
        const bool fire_selected    = fire_selection == native_fire::Selection::Selected;
        const bool vehicle_selected = alg_code_ == "2000001";
#if defined(COSMO_AXERA_HS_ROUTE_CANDIDATE) &&                                                               \
    (defined(COSMO_AXERA_FACE_ISOLATED_DEVICE_TEST) || defined(COSMO_AXERA_HS_ROUTE_PRODUCTION))
        const bool hs_selected = alg_code_ == nn::axera::hs_contract::kAtomicCode;
#else
        constexpr bool hs_selected = false;
#endif
#if defined(COSMO_AXERA_MOUSE_ROUTE_CANDIDATE) &&                                                            \
    (defined(COSMO_AXERA_FACE_ISOLATED_DEVICE_TEST) || defined(COSMO_AXERA_MOUSE_ROUTE_PRODUCTION))
        const bool mouse_selected = alg_code_ == nn::axera::mouse_contract::kAtomicCode;
#else
        constexpr bool mouse_selected = false;
#endif
#if defined(COSMO_AXERA_RUBBISH_ROUTE_CANDIDATE) &&                                                          \
    (defined(COSMO_AXERA_FACE_ISOLATED_DEVICE_TEST) || defined(COSMO_AXERA_RUBBISH_ROUTE_PRODUCTION))
        const bool rubbish_selected = alg_code_ == nn::axera::rubbish_contract::kAtomicCode;
#else
        constexpr bool rubbish_selected = false;
#endif
#ifndef COSMO_AXERA_VEHICLE_ROUTE_CANDIDATE
        if (vehicle_selected)
            return Status::Failed;
#endif
#ifndef COSMO_AXERA_FIRE_ROUTE_CANDIDATE
        if (fire_selected)
            return Status::Failed;
#endif
#ifndef COSMO_AXERA_PERSON_ROUTE_CANDIDATE
        // Default builds stay closed pending full-chain numerical acceptance.
        if (alg_code_ == "1001003")
            return Status::Failed;
#endif
        std::shared_ptr<const NativeFaceRegistration> benchmark_reuse;
        {
            std::lock_guard<std::mutex> lock(native_registration_mutex_);
            if (native_registration_) {
#ifdef COSMO_AXERA_SCENARIOBENCH_CANDIDATE
                const auto bench_scene = [](const std::string& scene) {
                    return scene == "930927510" || scene == "930007463";
                };
                if (alg_code_ == "9275710" && native_person_registered_ &&
                    native_registration_->bench_entry_.lock() && native_registration_->bench_ticket_ &&
                    bench_scene(native_scene_id_) && bench_scene(task_scene))
                    benchmark_reuse = native_registration_;
                else
#endif
                    return Status::Failed;
            }
        }
#ifdef COSMO_AXERA_SCENARIOBENCH_CANDIDATE
        if (benchmark_reuse) {
            // Reuse is committed by the same worker that owns the model. The
            // mailbox serializes this validation with worker Stop().
            struct ReusePrepared final : NativeFaceWorkerInit::Prepared {
                explicit ReusePrepared(std::function<bool()> f) : check(std::move(f)) {}
                bool Commit() override {
                    return check();
                }
                std::function<bool()> check;
            };
            const bool ready = native_worker_init_.Request(
                [this, benchmark_reuse]() -> std::unique_ptr<NativeFaceWorkerInit::Prepared> {
                    return std::make_unique<ReusePrepared>([this, benchmark_reuse] {
                        auto queue  = benchmark_reuse->queue_.lock();
                        auto output = benchmark_reuse->output_distributor_.lock();
                        if (!queue || !output || output != distributor)
                            return false;
                        auto lease = queue->AcquireLifecycle(benchmark_reuse->queue_lifecycle_);
                        if (!lease)
                            return false;
                        std::lock_guard<std::mutex> lock(native_registration_mutex_);
                        return running && data_queue == queue && detector_ &&
                               native_registration_ == benchmark_reuse &&
                               detector_->native_registration_ == benchmark_reuse &&
                               detector_->native_identity_ == benchmark_reuse->detector_identity_ &&
                               detector_->bench_entry_ &&
                               benchmark_reuse->bench_entry_.lock() == detector_->bench_entry_ &&
                               benchmark_reuse->bench_ticket_ && native_person_registered_;
                    });
                },
                std::chrono::seconds(30));
            return ready ? Status::Ready : Status::Failed;
        }
#endif
        if (nn::axera::route_identity::IsPersonAttributesModel(alg_code_)) {
            if (!nn::axera::route_identity::MatchesPersonAttributesRoute(alg_code_, task_scene))
                return Status::Failed;
            auto bytes = ReadVerifiedE24Model();
            if (!bytes) {
                LOG_WARN("AX E24 native model could not be verified for scene {}", task_scene);
                return Status::Failed;
            }
            const auto generation = NextNativeGeneration();
            if (!generation)
                return Status::Failed;
            const bool ready = native_worker_init_.Request(
                [this, bytes = std::move(bytes), generation, task_scene] {
                    return PrepareE24OnWorker(bytes, generation, task_scene);
                },
                std::chrono::seconds(30));
            return ready ? Status::Ready : Status::Failed;
        }
        const auto app_root = path::GetAppBaseDir();
        if (app_root.empty())
            return Status::Failed;
        Fd root{OpenTrustedAbsoluteDirectory(app_root)};
        if (root.value < 0)
            return Status::Failed;
        struct stat root_stat {};
        if (::fstat(root.value, &root_stat) || !S_ISDIR(root_stat.st_mode) || (root_stat.st_mode & 0022) ||
            (root_stat.st_uid != ::geteuid() && root_stat.st_uid != 0))
            return Status::Failed;
        const auto publisher = root_stat.st_uid;
        Fd resource{OpenDirectory(root.value, "resource")};
        if (resource.value < 0)
            return Status::Failed;
        if (!TrustedDirectory(resource.value, publisher))
            return Status::Failed;
        Fd native{OpenDirectory(resource.value, "axera-native")};
        if (native.value < 0)
            return Status::Failed;
        if (!TrustedDirectory(native.value, publisher))
            return Status::Failed;
        auto roots             = std::make_shared<Roots>();
        roots->contracts.value = OpenDirectory(native.value, "contracts");
        if (!TrustedDirectory(roots->contracts.value, publisher))
            return Status::Failed;
        if (task_scene.empty() || !path::IsSafePathComponent(alg_code_, 120) ||
            ((fire_selected || vehicle_selected || hs_selected || mouse_selected || rubbish_selected) &&
             !path::IsSafePathComponent(task_scene, 120)))
            return Status::Failed;
        bool person_template = false;
        if (alg_code_ == "1001003" && task_graph) {
            person_template =
                person_template_contract::MatchesActionGraph(task_graph->workFlow, task_graph->category);
        }
        std::string manifest;
        if (alg_code_ == "9275710") {
            if (task_scene != "930927510" && task_scene != "930007463")
                return Status::Failed;
            manifest.clear();
        } else if (alg_code_ == "1001003") {
            const auto person_manifest =
                nn::axera::person_manifest::Select(roots->contracts.value, alg_code_, task_scene);
            if (!person_manifest)
                return Status::Failed;
            manifest = *person_manifest;
        } else {
            manifest =
                (fire_selected || vehicle_selected || hs_selected || mouse_selected || rubbish_selected ||
                 (alg_code_ == "1000001" &&
                  (task_scene == "88110" || task_scene == "88117" || task_scene == "88119")))
                    ? alg_code_ + "-" + task_scene + ".json"
                    : alg_code_ + ".json";
            struct stat manifest_stat {};
            if (::fstatat(roots->contracts.value, manifest.c_str(), &manifest_stat, AT_SYMLINK_NOFOLLOW) != 0)
                return Status::Failed;
        }
        roots->models.value = OpenDirectory(resource.value, "models");
        if (!TrustedDirectory(roots->models.value, publisher))
            return Status::Failed;
        roots->publisher_uid  = static_cast<std::uint32_t>(publisher);
        const auto service_id = service::ServiceRegistry::Instance().Get<service::IAppInfoService>().DevId();
        if (service_id.empty())
            return Status::Failed;
#ifdef COSMO_AXERA_FACE_ISOLATED_DEVICE_TEST
        const auto device_identity = nn::axera::face_deployment::ReadLocalSshHostKeySha256();
        if (device_identity.empty())
            return Status::Failed;
#endif
        const bool ready = native_worker_init_.Request(
            [this, roots, manifest, service_id, task_scene, fire_selected, vehicle_selected, hs_selected,
             mouse_selected, rubbish_selected, person_template
#ifdef COSMO_AXERA_FACE_ISOLATED_DEVICE_TEST
             ,
             device_identity
#endif
        ]() -> std::unique_ptr<NativeFaceWorkerInit::Prepared> {
                if (alg_code_ == "9275710" && (task_scene == "930927510" || task_scene == "930007463")) {
                    const auto& directory =
                        nn::axera::route_identity::Lookup("benchmark.person_model_directory");
                    if (directory.empty())
                        return nullptr;
                    Fd model_dir{OpenDirectory(roots->models.value, directory.c_str())};
                    if (!TrustedDirectory(model_dir.value, roots->publisher_uid))
                        return nullptr;
                    Fd fd{::openat(model_dir.value, "model.axmodel", O_RDONLY | O_CLOEXEC | O_NOFOLLOW)};
                    struct stat st {};
                    if (fd.value < 0 || ::fstat(fd.value, &st) != 0 || !S_ISREG(st.st_mode) ||
                        st.st_uid != roots->publisher_uid || (st.st_mode & 0022) || st.st_size <= 0 ||
                        st.st_size > 32 * 1024 * 1024)
                        return nullptr;
                    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(st.st_size));
                    std::size_t used = 0;
                    while (used < bytes.size()) {
                        const auto n = ::read(fd.value, bytes.data() + used, bytes.size() - used);
                        if (n <= 0)
                            return nullptr;
                        used += static_cast<std::size_t>(n);
                    }
                    auto executor         = nn::axera::benchmark::PersonExecutor::Load(std::move(bytes));
                    const auto generation = NextNativeGeneration();
                    if (!executor || !generation)
                        return nullptr;
                    return PrepareBenchmarkPersonOnWorker(std::move(executor), generation, task_scene);
                }
#ifdef COSMO_AXERA_FIRE_ROUTE_CANDIDATE
                if (fire_selected) {
                    namespace fire = nn::axera::fire_deployment;
#ifdef COSMO_AXERA_FACE_ISOLATED_DEVICE_TEST
                    auto loaded = fire::LoadIsolatedDeviceTestFromDirectory(
                        roots->contracts.value, roots->models.value, manifest, roots->publisher_uid,
                        service_id, device_identity);
#else
                    auto loaded = fire::LoadFromDirectory(roots->contracts.value, roots->models.value,
                                                          manifest, roots->publisher_uid, service_id);
#endif
                    if (loaded.error != fire::Error::None || !loaded.deployment ||
                        loaded.deployment->atomic_code != alg_code_ ||
                        loaded.deployment->contract.scene_id != task_scene || !loaded.deployment->model_bytes)
                        return nullptr;
                    auto binding = std::make_shared<nn::axera::fire_binding::Binding>();
                    nn::axera::face_binding::SdkLoader loader;
                    const auto& bytes = *loaded.deployment->model_bytes;
                    auto capability =
                        binding->Bind(bytes.data(), bytes.size(), loaded.deployment->contract, loader);
                    const auto generation = NextNativeGeneration();
                    if (!capability || !generation)
                        return nullptr;
                    return PrepareNativeFireOnWorker(std::move(binding), std::move(capability), generation,
                                                     task_scene);
                }
#endif
#ifdef COSMO_AXERA_VEHICLE_ROUTE_CANDIDATE
                if (vehicle_selected) {
                    namespace vehicle = nn::axera::vehicle_deployment;
#ifdef COSMO_AXERA_FACE_ISOLATED_DEVICE_TEST
                    auto loaded = vehicle::LoadIsolatedDeviceTestFromDirectory(
                        roots->contracts.value, roots->models.value, manifest, roots->publisher_uid,
                        service_id, device_identity);
#else
                    auto loaded = vehicle::LoadFromDirectory(roots->contracts.value, roots->models.value,
                                                             manifest, roots->publisher_uid, service_id);
#endif
                    if (loaded.error != vehicle::Error::None || !loaded.deployment ||
                        loaded.deployment->atomic_code != alg_code_ ||
                        loaded.deployment->contract.scene_id != task_scene || !loaded.deployment->model_bytes)
                        return nullptr;
                    auto binding = std::make_shared<nn::axera::vehicle_binding::Binding>();
                    nn::axera::face_binding::SdkLoader loader;
                    const auto& bytes = *loaded.deployment->model_bytes;
                    auto capability =
                        binding->Bind(bytes.data(), bytes.size(), loaded.deployment->contract, loader);
                    const auto generation = NextNativeGeneration();
                    if (!capability || !generation)
                        return nullptr;
                    return PrepareNativeVehicleOnWorker(std::move(binding), std::move(capability), generation,
                                                        task_scene);
                }
#endif
#if defined(COSMO_AXERA_HS_ROUTE_CANDIDATE) &&                                                               \
    (defined(COSMO_AXERA_FACE_ISOLATED_DEVICE_TEST) || defined(COSMO_AXERA_HS_ROUTE_PRODUCTION))
                if (hs_selected) {
                    namespace person = nn::axera::person_deployment;
#ifdef COSMO_AXERA_FACE_ISOLATED_DEVICE_TEST
                    auto loaded = person::LoadHsIsolatedDeviceTestFromDirectory(
                        roots->contracts.value, roots->models.value, manifest, roots->publisher_uid,
                        service_id, device_identity);
#else
                    auto loaded =
                        person::LoadHsProductionFromDirectory(roots->contracts.value, roots->models.value,
                                                              manifest, roots->publisher_uid, service_id);
#endif
                    if (loaded.error != person::Error::None || !loaded.deployment ||
                        loaded.deployment->atomic_code != alg_code_ ||
                        loaded.deployment->contract.scene_id != task_scene ||
                        !loaded.deployment->model_bytes ||
                        !nn::axera::hs_contract::IsThreshold(loaded.deployment->scene_threshold))
                        return nullptr;
                    auto binding = std::make_shared<nn::axera::person_binding::Binding>();
                    nn::axera::face_binding::SdkLoader loader;
                    const auto& bytes = *loaded.deployment->model_bytes;
                    auto capability =
                        binding->Bind(bytes.data(), bytes.size(), loaded.deployment->contract, loader);
                    const auto generation = NextNativeGeneration();
                    if (!capability || !generation)
                        return nullptr;
                    return PrepareNativePersonOnWorker(std::move(binding), std::move(capability), generation,
                                                       task_scene, loaded.deployment->scene_threshold);
                }
#endif
#if defined(COSMO_AXERA_MOUSE_ROUTE_CANDIDATE) &&                                                            \
    (defined(COSMO_AXERA_FACE_ISOLATED_DEVICE_TEST) || defined(COSMO_AXERA_MOUSE_ROUTE_PRODUCTION))
                if (mouse_selected) {
                    namespace person = nn::axera::person_deployment;
#ifdef COSMO_AXERA_FACE_ISOLATED_DEVICE_TEST
                    auto loaded = person::LoadMouseIsolatedDeviceTestFromDirectory(
                        roots->contracts.value, roots->models.value, manifest, roots->publisher_uid,
                        service_id, device_identity);
#else
                    auto loaded =
                        person::LoadMouseProductionFromDirectory(roots->contracts.value, roots->models.value,
                                                                 manifest, roots->publisher_uid, service_id);
#endif
                    if (loaded.error != person::Error::None || !loaded.deployment ||
                        loaded.deployment->atomic_code != alg_code_ ||
                        loaded.deployment->contract.scene_id != task_scene ||
                        !loaded.deployment->model_bytes ||
                        !nn::axera::mouse_contract::IsThreshold(loaded.deployment->scene_threshold))
                        return nullptr;
                    auto binding = std::make_shared<nn::axera::person_binding::Binding>();
                    nn::axera::face_binding::SdkLoader loader;
                    const auto& bytes = *loaded.deployment->model_bytes;
                    auto capability =
                        binding->Bind(bytes.data(), bytes.size(), loaded.deployment->contract, loader);
                    const auto generation = NextNativeGeneration();
                    if (!capability || !generation)
                        return nullptr;
                    return PrepareNativePersonOnWorker(std::move(binding), std::move(capability), generation,
                                                       task_scene, loaded.deployment->scene_threshold);
                }
#endif
#if defined(COSMO_AXERA_RUBBISH_ROUTE_CANDIDATE) &&                                                          \
    (defined(COSMO_AXERA_FACE_ISOLATED_DEVICE_TEST) || defined(COSMO_AXERA_RUBBISH_ROUTE_PRODUCTION))
                if (rubbish_selected) {
                    namespace person = nn::axera::person_deployment;
#ifdef COSMO_AXERA_FACE_ISOLATED_DEVICE_TEST
                    auto loaded = person::LoadRubbishIsolatedDeviceTestFromDirectory(
                        roots->contracts.value, roots->models.value, manifest, roots->publisher_uid,
                        service_id, device_identity);
#else
                    auto loaded = person::LoadRubbishProductionFromDirectory(
                        roots->contracts.value, roots->models.value, manifest, roots->publisher_uid,
                        service_id);
#endif
                    if (loaded.error != person::Error::None || !loaded.deployment ||
                        loaded.deployment->atomic_code != alg_code_ ||
                        loaded.deployment->contract.scene_id != task_scene ||
                        !loaded.deployment->model_bytes ||
                        !nn::axera::rubbish_contract::IsThreshold(loaded.deployment->scene_threshold))
                        return nullptr;
                    auto binding = std::make_shared<nn::axera::person_binding::Binding>();
                    nn::axera::face_binding::SdkLoader loader;
                    const auto& bytes = *loaded.deployment->model_bytes;
                    auto capability =
                        binding->Bind(bytes.data(), bytes.size(), loaded.deployment->contract, loader);
                    const auto generation = NextNativeGeneration();
                    if (!capability || !generation)
                        return nullptr;
                    return PrepareNativePersonOnWorker(std::move(binding), std::move(capability), generation,
                                                       task_scene, loaded.deployment->scene_threshold);
                }
#endif
#ifdef COSMO_AXERA_PERSON_ROUTE_CANDIDATE
                if (alg_code_ == "1001003") {
                    namespace person = nn::axera::person_deployment;
#ifdef COSMO_AXERA_FACE_ISOLATED_DEVICE_TEST
                    auto loaded = person::LoadIsolatedDeviceTestFromDirectory(
                        roots->contracts.value, roots->models.value, manifest, roots->publisher_uid,
                        service_id, device_identity);
#else
                    auto loaded = person::LoadFromDirectory(roots->contracts.value, roots->models.value,
                                                            manifest, roots->publisher_uid, service_id);
#endif
                    if (loaded.error != person::Error::None || !loaded.deployment ||
                        loaded.deployment->atomic_code != alg_code_ || !loaded.deployment->model_bytes)
                        return nullptr;
                    auto contract                      = loaded.deployment->contract;
                    const bool dynamic_person_template = person_template_contract::CanReuseReference(
                        person_template, manifest, contract.scene_id, contract.model_sha256,
                        loaded.deployment->source_onnx_sha256, contract.semantic_contract);
                    if (contract.scene_id != task_scene && !dynamic_person_template)
                        return nullptr;
                    if (dynamic_person_template)
                        contract.scene_id = task_scene;
                    auto binding = std::make_shared<nn::axera::person_binding::Binding>();
                    nn::axera::face_binding::SdkLoader loader;
                    const auto& bytes     = *loaded.deployment->model_bytes;
                    auto capability       = binding->Bind(bytes.data(), bytes.size(), contract, loader);
                    const auto generation = NextNativeGeneration();
                    if (!capability || !generation)
                        return nullptr;
                    return PrepareNativePersonOnWorker(std::move(binding), std::move(capability), generation,
                                                       task_scene, loaded.deployment->scene_threshold,
                                                       dynamic_person_template);
                }
#endif
                namespace deployment = nn::axera::face_deployment;
#ifdef COSMO_AXERA_FACE_ISOLATED_DEVICE_TEST
                auto loaded = deployment::LoadIsolatedDeviceTestFromDirectory(
                    roots->contracts.value, roots->models.value, manifest, roots->publisher_uid, service_id,
                    device_identity);
#else
                auto loaded = deployment::LoadFromDirectory(roots->contracts.value, roots->models.value,
                                                            manifest, roots->publisher_uid, service_id);
#endif
                if (loaded.error != deployment::Error::None || !loaded.deployment ||
                    loaded.deployment->atomic_code != alg_code_ ||
                    loaded.deployment->contract.scene_id != task_scene || !loaded.deployment->model_bytes)
                    return nullptr;
                auto binding = std::make_shared<nn::axera::face_binding::Binding>();
                nn::axera::face_binding::SdkLoader loader;
                const auto& bytes = *loaded.deployment->model_bytes;
                auto capability =
                    binding->Bind(bytes.data(), bytes.size(), loaded.deployment->contract, loader);
                const auto generation = NextNativeGeneration();
                if (!capability || !generation)
                    return nullptr;
                return PrepareNativeFaceOnWorker(std::move(binding), std::move(capability), generation,
                                                 task_scene, loaded.deployment->scene_threshold);
            },
            std::chrono::seconds(30));
        return ready ? Status::Ready : Status::Failed;
    } catch (...) {
        return Status::Failed;
    }
}
}  // namespace cosmo
#endif
