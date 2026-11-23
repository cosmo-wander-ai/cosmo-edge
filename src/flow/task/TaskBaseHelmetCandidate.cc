#if defined(COSMO_NN_USE_AXERA_BACKEND) && defined(COSMO_MEDIA_USE_AXERA_BACKEND) &&                         \
    defined(COSMO_AXERA_HELMET_ROUTE_CANDIDATE)
#include "flow/classify/AiClassifier.h"
#include "flow/task/TaskBase.h"
#include "nn/device/axera/axera_route_identity.h"
#ifdef COSMO_AXERA_FACE_FEATURE_ROUTE_CANDIDATE
#include "flow/recognizer/AiRecognizer.h"
#endif
#include "flow/classify/NativeHelmetTicket.h"
#ifdef COSMO_AXERA_PHONE_ROUTE_CANDIDATE
#include "flow/task/GarmentSceneBridge.h"
#include "flow/task/PhoneSceneBridge.h"
#ifdef COSMO_AXERA_ELECTRIC_ROUTE_CANDIDATE
#include "flow/task/ElectricSceneBridge.h"
#include "flow/task/VehicleColorSceneBridge.h"
#endif
#ifdef COSMO_AXERA_MASK_ROUTE_CANDIDATE
#include "flow/task/MaskSceneBridge.h"
#ifdef COSMO_AXERA_FACEQUALITY_ROUTE_CANDIDATE
#include "flow/task/FaceQualitySceneBridge.h"
#endif
#ifdef COSMO_AXERA_HSHELMET_ROUTE_CANDIDATE
#include "flow/task/HsHelmetSceneBridge.h"
#endif
#endif
#include <cmath>
#include <cstdlib>
#include <optional>

#include "flow/task/PhoneTaskParams.h"
#endif
#ifdef COSMO_AXERA_FALL_ROUTE_CANDIDATE
#include "flow/task/FallSceneBridge.h"
#endif
#ifdef COSMO_AXERA_SLEEP_ROUTE_CANDIDATE
#include "flow/task/SleepSceneBridge.h"
#include "nn/device/axera/helmet/SleepModelBytes.h"
#endif
#include "flow/detect/AiDetector.h"
#include "flow/detect/NativeFaceRoute.h"
#ifdef COSMO_AXERA_SCENARIOBENCH_CANDIDATE
#include "nn/device/axera/benchmark/BenchLegacyHelmetMode.h"
#endif
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <limits>
#include <vector>

#include "service/detail/ServiceRegistry.h"
#include "service/model/IModelPathMapping.h"
#include "util/NativeCascadeScenePolicy.h"
namespace cosmo {
namespace {
    struct Fd {
        int fd{-1};
        ~Fd() {
            if (fd >= 0)
                ::close(fd);
        }
    };
    std::shared_ptr<const std::vector<std::uint8_t>> ReadHelmetModel(const std::string& dir,
                                                                     const char* name = "model.axmodel") {
        if (dir.empty())
            return {};
        Fd root{::open(dir.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW)};
        if (root.fd < 0)
            return {};
        struct stat st {};
        if (::fstat(root.fd, &st) || !S_ISDIR(st.st_mode) || (st.st_mode & 0022) ||
            (st.st_uid != 0 && st.st_uid != ::geteuid()))
            return {};
        Fd f{::openat(root.fd, name, O_RDONLY | O_CLOEXEC | O_NOFOLLOW)};
        struct stat a {
        }, b{};
        if (f.fd < 0 || ::fstat(f.fd, &a) || !S_ISREG(a.st_mode) || (a.st_mode & 0022) ||
            (a.st_uid != 0 && a.st_uid != ::geteuid()) || a.st_size <= 0 || a.st_size > 256ll * 1024 * 1024)
            return {};
        auto bytes      = std::make_shared<std::vector<std::uint8_t>>(static_cast<std::size_t>(a.st_size));
        std::size_t pos = 0;
        while (pos < bytes->size()) {
            auto n = ::read(f.fd, bytes->data() + pos, bytes->size() - pos);
            if (n < 0 && errno == EINTR)
                continue;
            if (n <= 0)
                return {};
            pos += static_cast<std::size_t>(n);
        }
        if (::fstat(f.fd, &b) || a.st_dev != b.st_dev || a.st_ino != b.st_ino || a.st_size != b.st_size ||
            a.st_mtim.tv_sec != b.st_mtim.tv_sec || a.st_mtim.tv_nsec != b.st_mtim.tv_nsec ||
            a.st_ctim.tv_sec != b.st_ctim.tv_sec || a.st_ctim.tv_nsec != b.st_ctim.tv_nsec)
            return {};
        return bytes;
    }
#ifdef COSMO_AXERA_PHONE_ROUTE_CANDIDATE
    std::optional<float> PhoneThreshold(const TaskElement& task, const std::string& label) {
        return phone_candidate::PhoneThreshold(task.params, label);
    }
#endif
    struct Nodes {
        TaskAction *video{}, *detector{}, *tracker{}, *filter{}, *classifier{}, *logic{}, *recognizer{},
            *sensitivity{}, *alarm{};
    };
    bool FindNodes(TaskElement& t, Nodes& n) {
        const bool helmet = t.GetAlgId() == "15";
#ifdef COSMO_AXERA_SCENARIOBENCH_CANDIDATE
        const bool bench_helmet = t.GetAlgId() == "930007463";
#else
        constexpr bool bench_helmet = false;
#endif
#ifdef COSMO_AXERA_FALL_ROUTE_CANDIDATE
        const bool fall = t.GetAlgId() == "6";
#else
        constexpr bool fall = false;
#endif
#ifdef COSMO_AXERA_PHONE_ROUTE_CANDIDATE
        const bool phone = t.GetAlgId() == "11" || t.GetAlgId() == "12" || t.GetAlgId() == "22";
#else
        constexpr bool phone = false;
#endif
#ifdef COSMO_AXERA_SLEEP_ROUTE_CANDIDATE
        const bool sleep = t.GetAlgId() == "65";
#else
        constexpr bool sleep = false;
#endif
#ifdef COSMO_AXERA_FACE_FEATURE_ROUTE_CANDIDATE
        const bool facefeature = t.GetAlgId() == "88117";
#else
        constexpr bool facefeature = false;
#endif
#ifdef COSMO_AXERA_REID_ROUTE_CANDIDATE
        const bool reid = (t.GetAlgId() == "88118" || t.GetAlgId() == "88120" || t.GetAlgId() == "58" ||
                           t.GetAlgId() == "32");
#else
        constexpr bool reid = false;
#endif
        const bool mask         = t.GetAlgId() == "88110";
        const bool facequality  = t.GetAlgId() == "88119";
        const bool hshelmet     = t.GetAlgId() == "88112";
        const bool electric     = t.GetAlgId() == "88111";
        const bool vehiclecolor = t.GetAlgId() == "88113" || t.GetAlgId() == "88114";
        const bool garment      = t.GetAlgId() == "88115" || t.GetAlgId() == "88116";
        if ((!helmet && !bench_helmet && !fall && !phone && !sleep && !mask && !electric && !vehiclecolor &&
             !garment && !hshelmet && !facefeature && !reid && !facequality) ||
            t.channelId.empty() || t.taskId.empty() || t.actions.size() != 8)
            return false;
        for (auto& a : t.actions) {
            TaskAction** dst = nullptr;
            if (a.action.actionId == "BA_00001")
                dst = &n.video;
            else if (a.action.actionId == "AA_00001")
                dst = &n.detector;
            else if (a.action.actionId == "AA_00003")
                dst = &n.tracker;
            else if (a.action.actionId == "BA_00002")
                dst = &n.filter;
            else if (a.action.actionId == "AA_00002")
                dst = &n.classifier;
            else if (a.action.actionId == "AA_00005")
                dst = &n.recognizer;
            else if (a.action.actionId == "BA_90001")
                dst = &n.logic;
            else if (a.action.actionId == "BA_00003")
                dst = &n.sensitivity;
            else if (a.action.actionId == "BA_00004")
                dst = &n.alarm;
            if (!dst || *dst || !a.actionInst || a.action.flowActionId.empty())
                return false;
            *dst = &a;
        }
        std::array<TaskAction*, 8> chain{n.video,       n.detector,
                                         n.tracker,     n.filter,
                                         n.classifier,  (facefeature || reid) ? n.recognizer : n.logic,
                                         n.sensitivity, n.alarm};
        for (auto* p : chain)
            if (!p || p->action.flowActionId == "-1")
                return false;
        if (n.video->action.preFlowActionId != "-1" || n.video->fatherAction)
            return false;
        for (std::size_t i = 0; i < chain.size(); ++i)
            for (std::size_t j = 0; j < i; ++j)
                if (chain[i]->action.flowActionId == chain[j]->action.flowActionId)
                    return false;
        for (std::size_t i = 1; i < chain.size(); ++i)
            if (chain[i]->action.preFlowActionId != chain[i - 1]->action.flowActionId ||
                chain[i]->fatherAction != chain[i - 1]->actionInst)
                return false;
#ifdef COSMO_AXERA_REID_ROUTE_CANDIDATE
        if (reid) {
            if (n.detector->action.atomicCode != "1001003" || n.classifier->action.atomicCode != "1001007" ||
                !n.recognizer || n.recognizer->action.atomicCode != "1001007")
                return false;
            if (!PhoneThreshold(t, "pedestrian"))
                return false;
            return true;
        }
#endif
#ifdef COSMO_AXERA_FACE_FEATURE_ROUTE_CANDIDATE
        if (facefeature) {
            if (n.detector->action.atomicCode != "1000001" || n.classifier->action.atomicCode != "1000005" ||
                !n.recognizer || n.recognizer->action.atomicCode != "1000005")
                return false;
            const auto detector = PhoneThreshold(t, "face");
            if (!detector)
                return false;
            return true;  // Explicit task-score and library binding remain recognizer inputs.
        }
#endif
#ifdef COSMO_AXERA_VEHICLECOLOR_ROUTE_CANDIDATE
        if (vehiclecolor) {
            if (n.detector->action.atomicCode != "2000001" || n.classifier->action.atomicCode != "2000011")
                return false;
            const auto scene    = t.GetAlgId() == "88113" ? vehiclecolor_scene_candidate::Scene::WhiteVehicle
                                                          : vehiclecolor_scene_candidate::Scene::BlackVehicle;
            const auto* spec    = vehiclecolor_scene_candidate::Lookup(scene);
            const auto detector = PhoneThreshold(t, "Vehicle"),
                       classifier = PhoneThreshold(t, std::string(spec->output_label));
            if (!detector || !classifier)
                return false;
            const auto& hash = cosmo::nn::axera::route_identity::Digest("vehicle_color.scene_binding_sha256");
            if (hash.empty())
                return false;
            const vehiclecolor_scene_candidate::Admission binding{scene,
                                                                  "2000011",
                                                                  "2000001",
                                                                  spec->output_label,
                                                                  spec->repository_scene_label_id,
                                                                  spec->reference_config_label_id,
                                                                  spec->tensor_channel,
                                                                  27,
                                                                  hash,
                                                                  hash};
            std::vector<ActionNode> actions;
            actions.reserve(t.actions.size());
            for (const auto& a : t.actions)
                actions.push_back(a.action);
            return vehiclecolor_candidate::AdmitVehicleColorActionGraph(actions, binding, *detector,
                                                                        *classifier);
        }
#endif
#ifdef COSMO_AXERA_ELECTRIC_ROUTE_CANDIDATE
        if (electric) {
            if (n.detector->action.atomicCode != "2000001" || n.classifier->action.atomicCode != "2000020")
                return false;
            constexpr auto scene  = electric_scene_candidate::Scene::ElectricVehicle;
            const auto* spec      = electric_scene_candidate::Lookup(scene);
            const auto detector   = PhoneThreshold(t, "Vehicle"),
                       classifier = PhoneThreshold(t, std::string(spec->output_label));
            if (!detector || !classifier)
                return false;
            const auto& hash = cosmo::nn::axera::route_identity::Digest("electric.scene_binding_sha256");
            if (hash.empty())
                return false;
            const electric_scene_candidate::Admission binding{
                scene, "2000020", "2000001", spec->output_label, 1, 1, 1, 3, hash, hash};
            std::vector<ActionNode> actions;
            actions.reserve(t.actions.size());
            for (const auto& a : t.actions)
                actions.push_back(a.action);
            return electric_candidate::AdmitElectricActionGraph(actions, binding, *detector, *classifier);
        }
#endif
#ifdef COSMO_AXERA_FACEQUALITY_ROUTE_CANDIDATE
        if (facequality) {
            if (n.detector->action.atomicCode != "1000001" || n.classifier->action.atomicCode != "1000012")
                return false;
            constexpr auto scene  = facequality_scene_candidate::Scene::FrontFace;
            const auto* spec      = facequality_scene_candidate::Lookup(scene);
            const auto detector   = PhoneThreshold(t, "face"),
                       classifier = PhoneThreshold(t, std::string(spec->output_label));
            if (!detector || !classifier)
                return false;
            const auto& hash = cosmo::nn::axera::route_identity::Digest("face_quality.scene_binding_sha256");
            if (hash.empty())
                return false;
            const facequality_scene_candidate::Admission binding{
                scene, "1000012", "1000001", spec->output_label, 1, 1, 1, 6, hash, hash};
            std::vector<ActionNode> actions;
            actions.reserve(t.actions.size());
            for (const auto& a : t.actions)
                actions.push_back(a.action);
            return facequality_candidate::AdmitFaceQualityActionGraph(actions, binding, *detector,
                                                                      *classifier);
        }
#endif
#ifdef COSMO_AXERA_MASK_ROUTE_CANDIDATE
        if (mask) {
            if (n.detector->action.atomicCode != "1000001" || n.classifier->action.atomicCode != "1000010")
                return false;
            constexpr auto scene  = mask_scene_candidate::Scene::NoMask;
            const auto* spec      = mask_scene_candidate::Lookup(scene);
            const auto detector   = PhoneThreshold(t, "face"),
                       classifier = PhoneThreshold(t, std::string(spec->output_label));
            if (!detector || !classifier)
                return false;
            const auto& hash = cosmo::nn::axera::route_identity::Digest("mask.scene_binding_sha256");
            if (hash.empty())
                return false;
            const mask_scene_candidate::Admission binding{
                scene, "1000010", "1000001", spec->output_label, 1, 1, 1, 2, hash, hash};
            std::vector<ActionNode> actions;
            actions.reserve(t.actions.size());
            for (const auto& a : t.actions)
                actions.push_back(a.action);
            return mask_candidate::AdmitMaskActionGraph(actions, binding, *detector, *classifier);
        }
#endif
#ifdef COSMO_AXERA_HSHELMET_ROUTE_CANDIDATE
        if (hshelmet) {
            if (n.detector->action.atomicCode != "1001001" || n.classifier->action.atomicCode != "2001002")
                return false;
            constexpr auto scene  = hshelmet_scene_candidate::Scene::NoHeadHelmet;
            const auto* spec      = hshelmet_scene_candidate::Lookup(scene);
            const auto detector   = PhoneThreshold(t, "headShoulder"),
                       classifier = PhoneThreshold(t, std::string(spec->output_label));
            if (!detector || !classifier)
                return false;
            const auto& hash =
                cosmo::nn::axera::route_identity::Digest("head_shoulder_helmet.scene_binding_sha256");
            if (hash.empty())
                return false;
            const hshelmet_scene_candidate::Admission binding{
                scene, "2001002", "1001001", spec->output_label, 1, 1, 1, 5, hash, hash};
            std::vector<ActionNode> actions;
            actions.reserve(t.actions.size());
            for (const auto& a : t.actions)
                actions.push_back(a.action);
            return hshelmet_candidate::AdmitHsHelmetActionGraph(actions, binding, *detector, *classifier);
        }
#endif
#ifdef COSMO_AXERA_SCENARIOBENCH_CANDIDATE
        if (bench_helmet)
            return n.detector->action.atomicCode == "9275710" && n.classifier->action.atomicCode == "7982161";
#endif
        if (n.detector->action.atomicCode != "1001003")
            return false;
        if (helmet)
            return n.classifier->action.atomicCode == "2001036";
#ifdef COSMO_AXERA_GARMENT_ROUTE_CANDIDATE
        if (garment) {
            if (n.classifier->action.atomicCode != "1001005")
                return false;
            const auto scene = t.GetAlgId() == "88115" ? garment_scene_candidate::Scene::RedUpper
                                                       : garment_scene_candidate::Scene::BlackUpper;
            const auto* spec = garment_scene_candidate::Lookup(scene);
            if (!spec)
                return false;
            const auto detector   = PhoneThreshold(t, "pedestrian"),
                       classifier = PhoneThreshold(t, std::string(spec->output_label));
            if (!detector || !classifier)
                return false;
            const auto& hash = cosmo::nn::axera::route_identity::Digest("garment.scene_binding_sha256");
            if (hash.empty())
                return false;
            const garment_scene_candidate::Admission binding{scene,
                                                             "1001005",
                                                             "1001003",
                                                             spec->output_label,
                                                             spec->repository_scene_label_id,
                                                             spec->reference_config_label_id,
                                                             spec->tensor_channel,
                                                             66,
                                                             hash,
                                                             hash};
            std::vector<ActionNode> actions;
            actions.reserve(t.actions.size());
            for (const auto& a : t.actions)
                actions.push_back(a.action);
            return garment_candidate::AdmitGarmentActionGraph(actions, binding, *detector, *classifier);
        }
#endif
#ifdef COSMO_AXERA_PHONE_ROUTE_CANDIDATE
        if (phone) {
            if (n.classifier->action.atomicCode != "2002011")
                return false;
            const auto scene = t.GetAlgId() == "11"   ? phone_scene_candidate::Scene::Smoking
                               : t.GetAlgId() == "12" ? phone_scene_candidate::Scene::Answer
                                                      : phone_scene_candidate::Scene::Play;
            const auto* spec = phone_scene_candidate::Lookup(scene);
            if (!spec)
                return false;
            const auto detector   = PhoneThreshold(t, "pedestrian"),
                       classifier = PhoneThreshold(t, std::string(spec->output_label));
            if (!detector || !classifier)
                return false;
            const auto& hash = cosmo::nn::axera::route_identity::Digest("phone.scene_binding_sha256");
            if (hash.empty())
                return false;
            const phone_scene_candidate::Admission binding{scene,
                                                           "2002011",
                                                           "1001003",
                                                           spec->output_label,
                                                           spec->repository_scene_label_id,
                                                           spec->reference_config_label_id,
                                                           spec->tensor_channel,
                                                           7,
                                                           hash,
                                                           hash};
            std::vector<ActionNode> actions;
            actions.reserve(t.actions.size());
            for (const auto& a : t.actions)
                actions.push_back(a.action);
            return phone_candidate::AdmitPhoneActionGraph(actions, binding, *detector, *classifier);
        }
#endif
#ifdef COSMO_AXERA_SLEEP_ROUTE_CANDIDATE
        if (sleep) {
            std::vector<ActionNode> actions;
            actions.reserve(t.actions.size());
            for (const auto& a : t.actions)
                actions.push_back(a.action);
            return sleep_candidate::AdmitSleepActionGraph(t.GetAlgId(), actions,
                                                          sleep_candidate::BoundSleepReference());
        }
#endif
#ifdef COSMO_AXERA_FALL_ROUTE_CANDIDATE
        if (n.classifier->action.atomicCode != "2002008")
            return false;
        std::vector<ActionNode> actions;
        actions.reserve(t.actions.size());
        for (const auto& a : t.actions)
            actions.push_back(a.action);
        return fall_candidate::AdmitFallActionGraph(actions);
#else
        return false;
#endif
    }
    std::uint64_t NextIdentity() {
        static std::atomic<std::uint64_t> seq{1};
        auto v = seq.load();
        while (v && v < std::numeric_limits<std::uint64_t>::max())
            if (seq.compare_exchange_weak(v, v + 1))
                return v;
        return 0;
    }
}  // namespace
bool TaskBase::ConfigureNativeHelmetTask(TaskElementPtr task) {
    if (!task)
        return false;
    if (!native_cascade::ProtectedScene(task->GetAlgId()))
        return true;
    Nodes n;
    if (!FindNodes(*task, n)) {
        LOG_WARN("ScenarioBench cascade graph admission failed scene={} actions={}", task->GetAlgId(),
                 task->actions.size());
        return false;
    }
    auto classifier = std::dynamic_pointer_cast<AiClassifier>(n.classifier->actionInst);
    auto detector   = std::dynamic_pointer_cast<AiDetector>(n.detector->actionInst);
    if (!classifier || !detector || classifier->GetActiveTaskCount() != 0 || classifier->batch_count_ != 1 ||
        detector->NativeFaceSelection(&n.detector->action) != AiDetector::NativeSelection::Selected) {
        LOG_WARN("ScenarioBench cascade initial gate scene={} classifier={} detector={} active={} batch={}",
                 task->GetAlgId(), bool(classifier), bool(detector),
                 classifier ? classifier->GetActiveTaskCount() : -1,
                 classifier ? classifier->batch_count_ : 0);
        return false;
    }
    auto& mapping   = service::ServiceRegistry::Instance().Get<service::IModelPathMapping>();
    const bool fall = task->GetAlgId() == "6";
#ifdef COSMO_AXERA_PHONE_ROUTE_CANDIDATE
    const bool phone = task->GetAlgId() == "11" || task->GetAlgId() == "12" || task->GetAlgId() == "22";
#else
    constexpr bool phone = false;
#endif
#ifdef COSMO_AXERA_SLEEP_ROUTE_CANDIDATE
    const bool sleep = task->GetAlgId() == "65";
#else
    constexpr bool sleep = false;
#endif
#ifdef COSMO_AXERA_FACE_FEATURE_ROUTE_CANDIDATE
    const bool facefeature = task->GetAlgId() == "88117";
#else
    constexpr bool facefeature = false;
#endif
#ifdef COSMO_AXERA_REID_ROUTE_CANDIDATE
    const bool reid = (task->GetAlgId() == "88118" || task->GetAlgId() == "88120" ||
                       task->GetAlgId() == "58" || task->GetAlgId() == "32");
#else
    constexpr bool reid = false;
#endif
    const bool mask         = task->GetAlgId() == "88110";
    const bool facequality  = task->GetAlgId() == "88119";
    const bool hshelmet     = task->GetAlgId() == "88112";
    const bool electric     = task->GetAlgId() == "88111";
    const bool vehiclecolor = task->GetAlgId() == "88113" || task->GetAlgId() == "88114";
    const bool garment      = task->GetAlgId() == "88115" || task->GetAlgId() == "88116";
#ifdef COSMO_AXERA_SCENARIOBENCH_CANDIDATE
    const bool bench_helmet = task->GetAlgId() == "930007463";
#else
    constexpr bool bench_helmet = false;
#endif
    const char* model_name = "model.axmodel";
#ifdef COSMO_AXERA_SCENARIOBENCH_CANDIDATE
    if (bench_helmet && bench_ax::LegacyHelmetSelected())
        model_name = "model-legacy-helmet.axmodel";
#endif
    auto bytes = ReadHelmetModel(mapping.GetModelPathMapping(bench_helmet   ? "7982161"
                                                             : facequality  ? "1000012"
                                                             : reid         ? "1001007"
                                                             : facefeature  ? "1000005"
                                                             : garment      ? "1001005"
                                                             : vehiclecolor ? "2000011"
                                                             : electric     ? "2000020"
                                                             : hshelmet     ? "2001002"
                                                             : mask         ? "1000010"
                                                             : phone        ? "2002011"
                                                             : sleep        ? "1001010"
                                                             : fall         ? "2002008"
                                                                            : "2001036"),
                                 model_name);
    if (!bytes) {
        LOG_WARN("ScenarioBench cascade model bytes unavailable scene={}", task->GetAlgId());
        return false;
    }
#ifdef COSMO_AXERA_REID_ROUTE_CANDIDATE
    if (reid) {
        const auto dir = mapping.GetModelPathMapping("1001007");
        std::array<std::shared_ptr<const std::vector<std::uint8_t>>, 2> models{
            bytes, ReadHelmetModel(dir, "video-reid1.axmodel")};
        auto recognizer = std::dynamic_pointer_cast<AiRecognizer>(n.recognizer->actionInst);
        auto quality = task->GetAlgId() == "88120" ? ReadHelmetModel(mapping.GetModelPathMapping("1001008"))
                                                   : std::shared_ptr<const std::vector<std::uint8_t>>{};
        if (task->GetAlgId() == "88120" && !quality)
            return false;
        if (!recognizer || !recognizer->ConfigureNativeBodyConsumer(task->taskId) ||
            !classifier->ConfigureNativeReid(std::move(models), task->taskId, std::move(quality)))
            return false;
    } else
#endif
#ifdef COSMO_AXERA_FACE_FEATURE_ROUTE_CANDIDATE
        if (facefeature) {
        const auto dir = mapping.GetModelPathMapping("1000005");
        std::array<std::shared_ptr<const std::vector<std::uint8_t>>, 5> models{
            bytes, ReadHelmetModel(dir, "crop.axmodel"), ReadHelmetModel(dir, "feature0.axmodel"),
            ReadHelmetModel(dir, "feature1.axmodel"), ReadHelmetModel(dir, "feature2.axmodel")};
        auto recognizer = std::dynamic_pointer_cast<AiRecognizer>(n.recognizer->actionInst);
        if (!recognizer || !recognizer->ConfigureNativeFaceConsumer(task->taskId) ||
            !classifier->ConfigureNativeFaceFeature(std::move(models), task->taskId))
            return false;
    } else
#endif
#ifdef COSMO_AXERA_VEHICLECOLOR_ROUTE_CANDIDATE
        if (vehiclecolor) {
        const auto scene = task->GetAlgId() == "88113" ? vehiclecolor_scene_candidate::Scene::WhiteVehicle
                                                       : vehiclecolor_scene_candidate::Scene::BlackVehicle;
        const auto* spec = vehiclecolor_scene_candidate::Lookup(scene);
        if (!spec)
            return false;
        const auto threshold = PhoneThreshold(*task, std::string(spec->output_label));
        if (!threshold ||
            !classifier->ConfigureNativeVehicleColor(std::move(bytes), scene, *threshold, task->taskId))
            return false;
    } else
#endif
#ifdef COSMO_AXERA_ELECTRIC_ROUTE_CANDIDATE
        if (electric) {
        const auto threshold = PhoneThreshold(*task, "electricVehicle");
        if (!threshold ||
            !classifier->ConfigureNativeElectric(
                std::move(bytes), electric_scene_candidate::Scene::ElectricVehicle, *threshold, task->taskId))
            return false;
    } else
#endif
#ifdef COSMO_AXERA_FACEQUALITY_ROUTE_CANDIDATE
        if (facequality) {
        const auto threshold = PhoneThreshold(*task, "frontFace");
        if (!threshold ||
            !classifier->ConfigureNativeFaceQuality(
                std::move(bytes), facequality_scene_candidate::Scene::FrontFace, *threshold, task->taskId))
            return false;
    } else
#endif
#ifdef COSMO_AXERA_MASK_ROUTE_CANDIDATE
        if (mask) {
        const auto threshold = PhoneThreshold(*task, "normal");
        if (!threshold ||
            !classifier->ConfigureNativeMask(std::move(bytes), mask_scene_candidate::Scene::NoMask,
                                             *threshold, task->taskId))
            return false;
    } else
#endif
#ifdef COSMO_AXERA_HSHELMET_ROUTE_CANDIDATE
        if (hshelmet) {
        const auto threshold = PhoneThreshold(*task, "hatless");
        if (!threshold ||
            !classifier->ConfigureNativeHsHelmet(
                std::move(bytes), hshelmet_scene_candidate::Scene::NoHeadHelmet, *threshold, task->taskId))
            return false;
    } else
#endif
#ifdef COSMO_AXERA_GARMENT_ROUTE_CANDIDATE
        if (garment) {
        const auto scene = task->GetAlgId() == "88115" ? garment_scene_candidate::Scene::RedUpper
                                                       : garment_scene_candidate::Scene::BlackUpper;
        const auto* spec = garment_scene_candidate::Lookup(scene);
        if (!spec)
            return false;
        const auto threshold = PhoneThreshold(*task, std::string(spec->output_label));
        if (!threshold ||
            !classifier->ConfigureNativeGarment(std::move(bytes), scene, *threshold, task->taskId))
            return false;
    } else
#endif
#ifdef COSMO_AXERA_PHONE_ROUTE_CANDIDATE
        if (phone) {
        const auto scene = task->GetAlgId() == "11"   ? phone_scene_candidate::Scene::Smoking
                           : task->GetAlgId() == "12" ? phone_scene_candidate::Scene::Answer
                                                      : phone_scene_candidate::Scene::Play;
        const auto* spec = phone_scene_candidate::Lookup(scene);
        if (!spec)
            return false;
        const auto threshold = PhoneThreshold(*task, std::string(spec->output_label));
        if (!threshold ||
            !classifier->ConfigureNativePhone(std::move(bytes), scene, *threshold, task->taskId))
            return false;
    } else
#endif
#ifdef COSMO_AXERA_SLEEP_ROUTE_CANDIDATE
        if (sleep) {
        if (!helmet_candidate::VerifySleepModelBytes(bytes->data(), bytes->size()) ||
            !classifier->ConfigureNativeSleep(std::move(bytes)))
            return false;
    } else
#endif
#ifdef COSMO_AXERA_FALL_ROUTE_CANDIDATE
        if (fall) {
        if (!classifier->ConfigureNativeFall(std::move(bytes)))
            return false;
    } else
#endif
#ifdef COSMO_AXERA_SCENARIOBENCH_CANDIDATE
        if (bench_helmet) {
        if (!classifier->ConfigureBenchmarkHelmet(std::move(bytes), task->taskId)) {
            LOG_WARN("ScenarioBench cascade classifier configuration failed");
            return false;
        }
    } else
#endif
        if (!classifier->ConfigureNativeHelmet(std::move(bytes)))
        return false;
    task->helmet_task_identity  = std::make_shared<int>(0);
    task->helmet_model_identity = std::make_shared<int>(0);
    task->helmet_task_gate =
        helmet_candidate::LifecycleGate<helmet_candidate::TaskTag>::Create(task->helmet_task_identity, 1);
    task->helmet_model_gate =
        helmet_candidate::LifecycleGate<helmet_candidate::ModelTag>::Create(task->helmet_model_identity, 1);
    return task->helmet_task_gate && task->helmet_model_gate;
}
bool TaskBase::InstallNativeHelmetTask(TaskElementPtr task) {
    if (!task)
        return false;
    if (!native_cascade::ProtectedScene(task->GetAlgId()))
        return true;
    Nodes n;
    if (!FindNodes(*task, n) || !task->helmet_task_gate || !task->helmet_model_gate)
        return false;
    auto detector = std::dynamic_pointer_cast<AiDetector>(n.detector->actionInst);
    if (!detector)
        return false;
    auto registration = detector->NativeFaceRegistrationForQueue();
    if (!registration)
        return false;
    bool bench_cross = false;
#ifdef COSMO_AXERA_SCENARIOBENCH_CANDIDATE
    bench_cross = task->GetAlgId() == "930007463" && BenchmarkCrossChannelEnabled();
#endif
    {
        std::lock_guard<std::mutex> lock(detector->native_registration_mutex_);
        if (detector->native_registration_ != registration ||
            (!detector->native_person_registered_ && task->GetAlgId() != "88110" &&
             task->GetAlgId() != "88117" && task->GetAlgId() != "88119" && task->GetAlgId() != "88111" &&
             task->GetAlgId() != "88113" && task->GetAlgId() != "88114") ||
            (!bench_cross && detector->native_helmet_admission_))
            return false;
#ifdef COSMO_AXERA_SCENARIOBENCH_CANDIDATE
        if (bench_cross && detector->bench_helmet_admissions_.Find(task->channelId))
            return false;
#endif
#ifdef COSMO_AXERA_PHONE_ROUTE_CANDIDATE
        if (task->GetAlgId() == "88111" || task->GetAlgId() == "88113" || task->GetAlgId() == "88114") {
            if (!detector->native_vehicle_policies_)
                return false;
        } else if (task->GetAlgId() == "88112") {
            const auto threshold = PhoneThreshold(*task, "headShoulder");
            if (!threshold || detector->native_scene_threshold_ != *threshold)
                return false;
        } else if (task->GetAlgId() == "88110" || task->GetAlgId() == "88117" ||
                   task->GetAlgId() == "88119") {
            const auto threshold = PhoneThreshold(*task, "face");
            if (!threshold || detector->native_scene_threshold_ != *threshold)
                return false;
        } else if (task->GetAlgId() == "11" || task->GetAlgId() == "12" || task->GetAlgId() == "22" ||
                   task->GetAlgId() == "88115" || task->GetAlgId() == "88116" ||
                   (task->GetAlgId() == "88118" || task->GetAlgId() == "88120" || task->GetAlgId() == "58" ||
                    task->GetAlgId() == "32")) {
            const auto threshold = PhoneThreshold(*task, "pedestrian");
            if (!threshold || detector->native_scene_threshold_ != *threshold)
                return false;
        } else
#endif
#ifdef COSMO_AXERA_SCENARIOBENCH_CANDIDATE
            if (task->GetAlgId() == "930007463") {
            if (detector->native_scene_threshold_ != .25f)
                return false;
        } else
#endif
            if (detector->native_scene_threshold_ != .63f)
            return false;
    }
    if (task->GetAlgId() == "88111" || task->GetAlgId() == "88113" || task->GetAlgId() == "88114") {
        if (!registration->vehicle_entry_.lock() || !registration->vehicle_ticket_)
            return false;
    } else if (task->GetAlgId() == "88110" || task->GetAlgId() == "88117" || task->GetAlgId() == "88119") {
        if (!registration->entry_.lock() || !registration->ticket_)
            return false;
    }
#ifdef COSMO_AXERA_SCENARIOBENCH_CANDIDATE
    else if (task->GetAlgId() == "930007463") {
        if (!registration->bench_entry_.lock() || !registration->bench_ticket_)
            return false;
    }
#endif
    else {
        auto entry = registration->person_entry_.lock();
        if (!entry)
            return false;
#ifdef COSMO_AXERA_HSHELMET_ROUTE_CANDIDATE
        if (task->GetAlgId() == "88112") {
            if (!entry->MatchesHsHelmetHeadModel())
                return false;
        } else
#endif
            if (!entry->MatchesHelmetPersonModel())
            return false;
    }
#ifdef COSMO_AXERA_FACE_FEATURE_ROUTE_CANDIDATE
    if (task->GetAlgId() == "88117" || (task->GetAlgId() == "88118" || task->GetAlgId() == "88120" ||
                                        task->GetAlgId() == "58" || task->GetAlgId() == "32")) {
        auto q    = n.recognizer->actionInst->GetQueue();
        auto dist = n.classifier->actionInst->GetDistributor();
        AlgDataQueue<AlgDataPtr>::LifecycleSnapshot snapshot{};
        if (!q || !dist || !dist->SnapshotHelmetHop(task->channelId, task->taskId, q, snapshot))
            return false;
    }
#endif
    std::array<TaskAction*, 3> source{n.detector, n.tracker, n.filter},
        target{n.tracker, n.filter, n.classifier};
    std::array<NativeHelmetHop, 3> hops;
    for (std::size_t i = 0; i < hops.size(); ++i) {
        auto q    = target[i]->actionInst->GetQueue();
        auto dist = source[i]->actionInst->GetDistributor();
        AlgDataQueue<AlgDataPtr>::LifecycleSnapshot snap{};
        bool bench_pair = false;
#ifdef COSMO_AXERA_SCENARIOBENCH_CANDIDATE
        bench_pair = i == 0 && task->GetAlgId() == "930007463";
#endif
        if (!q || !dist || !dist->SnapshotHelmetHop(task->channelId, task->taskId, q, snap, bench_pair))
            return false;
        hops[i] = {std::static_pointer_cast<const void>(q), snap.generation, task->taskId};
    }
    auto q0 = hops[0].queue.lock(), q1 = hops[1].queue.lock(), q2 = hops[2].queue.lock();
    if (!q0 || !q1 || !q2 || q0 == q1 || q0 == q2 || q1 == q2)
        return false;
    auto id = NextIdentity();
    if (!id)
        return false;
    helmet_candidate::RouteIdentity identity{id, id, id, id, id, id, id, id};
    auto gate = std::make_shared<helmet_candidate::FramePermitGate>();
    if (!gate->Configure(identity))
        return false;
    auto plan = std::shared_ptr<const NativeHelmetAdmission>(new NativeHelmetAdmission(
        task->channelId, task->taskId, hops, gate, identity, task->helmet_task_gate, task->helmet_model_gate,
        task->helmet_task_identity, task->helmet_model_identity, 1, 1,
        std::static_pointer_cast<const void>(registration)));
    {
        std::lock_guard<std::mutex> lock(detector->native_registration_mutex_);
        if (detector->native_registration_ != registration)
            return false;
#ifdef COSMO_AXERA_SCENARIOBENCH_CANDIDATE
        if (bench_cross) {
            if (detector->native_helmet_admission_ ||
                !detector->bench_helmet_admissions_.Insert(task->channelId, plan))
                return false;
        } else
#endif
        {
            if (detector->native_helmet_admission_)
                return false;
            detector->native_helmet_admission_ = plan;
        }
    }
    task->helmet_admission = std::move(plan);
    return true;
}
void TaskBase::ClearNativeHelmetTask(TaskElementPtr task) noexcept {
    try {
        if (!task)
            return;
        for (auto& a : task->actions) {
            auto d = std::dynamic_pointer_cast<AiDetector>(a.actionInst);
            if (!d)
                continue;
            std::lock_guard<std::mutex> lock(d->native_registration_mutex_);
#ifdef COSMO_AXERA_SCENARIOBENCH_CANDIDATE
            if (task->GetAlgId() == "930007463" && BenchmarkCrossChannelEnabled()) {
                d->bench_helmet_admissions_.EraseIf(task->channelId, task->helmet_admission);
            } else
#endif
                if (d->native_helmet_admission_ == task->helmet_admission)
                d->native_helmet_admission_.reset();
        }
        task->helmet_admission.reset();
        task->helmet_task_gate.reset();
        task->helmet_model_gate.reset();
        task->helmet_task_identity.reset();
        task->helmet_model_identity.reset();
    } catch (...) {
    }
}
}  // namespace cosmo
#endif
