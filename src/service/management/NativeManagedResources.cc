#include <fcntl.h>
#include <openssl/evp.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <fstream>
#include <set>
#include <thread>

#include "service/algorithm/IAlgorithmCrud.h"
#include "service/algorithm/IAlgorithmLayout.h"
#include "service/algorithm/IAlgorithmQuery.h"
#include "service/camera/ICameraDeviceCrud.h"
#include "service/camera/ICameraTaskConfig.h"
#include "service/detail/ServiceRegistry.h"
#include "service/management/ManagedDeviceChip.h"
#include "service/management/ManagedModelConfig.h"
#include "service/management/ManagementService.h"
#include "service/model/IModelService.h"
#include "service/onvif/IOnvifService.h"
#include "service/system/IDeviceHardware.h"
#include "service/task/IScheduleService.h"
#include "util/FileUtil.h"
#include "util/NnBackendConstants.h"
#include "util/PathUtil.h"
#include "util/UuidUtil.h"

namespace cosmo::service {
namespace {
    using Json   = nlohmann::json;
    namespace fs = std::filesystem;

    template <typename T>
    T& Service() {
        return ServiceRegistry::Instance().Get<T>();
    }

    void Require(bool value, const char* error) {
        if (!value)
            throw ManagementError(error);
    }

    void Check(util::ErrorEnum result) {
        if (result != util::ErrorEnum::Success) {
            throw ManagementError("NATIVE_ERROR_" + std::to_string(static_cast<int>(result)));
        }
    }

    void SyncNativePath(const fs::path& path) {
        int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
        Require(fd >= 0, "STORAGE_FAILED");
        int result = ::fsync(fd);
        ::close(fd);
        Require(result == 0, "STORAGE_FAILED");
    }

    fs::path ModelDirectory(const std::string& id) {
        Require(id.size() == 7 && id.find_first_not_of("0123456789") == std::string::npos,
                "INVALID_LOCAL_ID");
        return fs::path(Service<IModelService>().GetModelPath()) /
               (std::string(util::kNewDirPrefix) + id + "_managed" + id + "_V1.0.0");
    }

    std::string Reference(const Json& resource, const std::string& kind, bool required = true) {
        std::string id;
        for (const auto& ref : resource.at("references")) {
            if (ref.at("kind") == kind) {
                Require(id.empty(), "AMBIGUOUS_DEPENDENCY");
                id = ref.at("localId").get<std::string>();
            }
        }
        Require(!required || !id.empty(), "MISSING_DEPENDENCY");
        return id;
    }

    std::string Fingerprint(const Json& value) {
        const auto text = value.dump();
        unsigned char output[EVP_MAX_MD_SIZE];
        unsigned int length = 0;
        Require(EVP_Digest(text.data(), text.size(), output, &length, EVP_sha256(), nullptr) == 1,
                "HASH_FAILED");
        std::string result;
        for (unsigned int i = 0; i < length; ++i) {
            result += "0123456789abcdef"[output[i] >> 4];
            result += "0123456789abcdef"[output[i] & 15];
        }
        return result;
    }

    std::string NativeChip() {
#ifdef COSMO_NN_USE_CPU_BACKEND
        return "x86";
#elif defined(COSMO_NN_USE_RKNN_BACKEND)
        std::string chip = util::kEngineType;
        std::transform(chip.begin(), chip.end(), chip.begin(), ::tolower);
        return chip;
#else
        return detail::ReadSophonManagementChip();
#endif
    }

    std::string NativeRuntime() {
#ifdef COSMO_NN_USE_CPU_BACKEND
        return "onnx";
#elif defined(COSMO_NN_USE_RKNN_BACKEND)
        return "rknn";
#else
        return "bmrt";
#endif
    }

    MsgScheduleTemplate Schedule(const Json& resource) {
        MsgScheduleTemplate result;
        result.scheduleId   = resource.at("localId").get<std::string>();
        result.scheduleName = resource.at("name").get<std::string>();
        const auto& periods = resource.at("config").at("periods");
        Require(periods.is_array() && periods.size() <= 128, "INVALID_PERIODS");
        auto time = [](const Json& value) {
            Require(value.is_string(), "INVALID_TIME");
            auto s = value.get<std::string>();
            Require(s.size() == 5 && s[2] == ':' && s.find_first_not_of("0123456789:") == std::string::npos &&
                        s[0] >= '0' && s[0] <= '2' && s[1] >= '0' && s[1] <= '9' && s[3] >= '0' &&
                        s[3] <= '5' && s[4] >= '0' && s[4] <= '9' && s.substr(0, 2) <= "23",
                    "INVALID_TIME");
            return s + ":00";
        };
        for (const auto& period : periods) {
            Require(period.at("week").is_number_integer(), "INVALID_WEEKDAY");
            int day = period["week"];
            Require(day >= 0 && day <= 6, "INVALID_WEEKDAY");
            MsgScheduleConfig entry;
            entry.weekDay = day;
            MsgRunTime window;
            window.timeBegin = time(period.at("begin"));
            window.timeEnd   = time(period.at("end"));
            Require(window.timeBegin < window.timeEnd, "INVALID_TIME_RANGE");
            entry.runTime.push_back(window);
            result.scheduleConfig.push_back(entry);
        }
        return result;
    }

    MsgCameraInfo Channel(const Json& resource) {
        const auto& config = resource.at("config");
        MsgCameraInfo result;
        result.videoChannelId = resource.at("localId").get<std::string>();
        result.channelName    = resource.at("name").get<std::string>();
        result.channelCode    = result.videoChannelId;
        const auto type       = config.at("type").get<std::string>();
        if (type == "rtsp")
            result.channelType = MsgCameraType::MsgCameraTypeLive;
        else if (type == "usb")
            result.channelType = MsgCameraType::MsgCameraTypeUsb;
        else if (type == "gb28181")
            result.channelType = MsgCameraType::MsgCameraTypeGb28181;
        else if (type == "video")
            result.channelType = MsgCameraType::MsgCameraTypeLocalVideo;
        else if (type == "onvif")
            result.channelType = config.contains("onvif") ? MsgCameraType::MsgCameraTypeOnvif
                                                          : MsgCameraType::MsgCameraTypeLive;
        else
            throw ManagementError("CHANNEL_TYPE_REQUIRES_NATIVE_SOURCE");
        result.url = config.value(type == "usb" ? "devicePath" : "url", std::string{});
        if (type == "onvif" && config.contains("onvif"))
            result.url = "onvif://managed-" + result.videoChannelId;
        if (type == "video") {
            Require(resource.at("files").size() == 1, "ONE_VIDEO_FILE_REQUIRED");
            auto extension =
                fs::path(resource["files"][0].at("name").get<std::string>()).extension().string();
            Require(extension == ".mp4" || extension == ".avi" || extension == ".mkv" ||
                        extension == ".mov" || extension == ".ts",
                    "UNSUPPORTED_VIDEO_FILE");
            result.url = (fs::path(path::GetCameraPath()) / (result.videoChannelId + extension)).string();
        }
        Require(!result.url.empty(), "EMPTY_CHANNEL_SOURCE");
        return result;
    }

    MsgTaskConfig TaskConfig(const Json& resource) {
        const auto& config = resource.at("config");
        MsgTaskConfig result;
        const auto overrides = config.value("overrides", Json::object());
        Require(overrides.is_object(), "INVALID_OVERRIDES");
        for (auto it = overrides.begin(); it != overrides.end(); ++it) {
            MsgDynamicKeyValue parameter;
            parameter.key   = it.key();
            parameter.value = it->is_string() ? it->get<std::string>() : it->dump();
            result.params.push_back(parameter);
        }
        const auto roi = config.value("roi", Json::object());
        Require(roi.is_object(), "INVALID_ROI");
        for (auto it = roi.begin(); it != roi.end(); ++it) {
            Require(it.key() == "areas" || it.key() == "shieldedAreas", "UNSUPPORTED_ROI_FIELD");
        }
        if (roi.contains("areas"))
            result.areas = roi["areas"].get<std::vector<MsgTaskArea>>();
        if (roi.contains("shieldedAreas"))
            result.shieldedAreas = roi["shieldedAreas"].get<std::vector<MsgTaskArea>>();
        return result;
    }

    algorithm::LayoutSaveReq Layout(const Json& config, const std::string& id, const std::string& version,
                                    const std::string& name = "") {
        const auto& layout = config.at("edgeLayout");
        algorithm::LayoutSaveReq result;
        result.algorithmId          = id;
        result.algorithmName        = name;
        result.confVersionId        = version;
        result.configVersionName    = "默认";
        result.algorithmCategory    = layout.value("algorithmCategory", std::string("1"));
        result.algorithmUsage       = layout.value("algorithmUsage", std::string("1"));
        result.algorithmProcessdata = layout.at("algorithmProcessdata").get<std::string>();
        result.algorithmMetadata    = layout.at("algorithmMetadata").get<std::string>();
        result.atomicList           = layout.at("atomicList").get<std::string>();
        // Decode now; malformed JSON strings must fail before native file mutation.
        for (const auto& value : {result.algorithmProcessdata, result.algorithmMetadata, result.atomicList}) {
            Require(!Json::parse(value, nullptr, false).is_discarded(), "INVALID_LAYOUT_JSON");
        }
        return result;
    }

    class NativeManagedResources final : public IManagedResources {
    public:
        Json DeviceFacts() override {
            return {{"sn", Service<IDeviceHardware>().GetDevSn()},
                    {"chip", NativeChip()},
                    {"runtimes", {NativeRuntime()}},
                    {"resourceKinds", {"model", "scene", "channel", "schedule", "task"}},
                    {"nativeFeatures", {"model-configuration-v1"}}};
        }

        bool Exists(const std::string& kind, const std::string& id) override {
            if (kind == "model") {
                std::string config, defaults;
                bool exportable = false;
                return Service<IModelService>().GetModelConfig(id, config, exportable, defaults) ==
                       util::ErrorEnum::Success;
            }
            if (kind == "scene") {
                size_t count = 0;
                return !Service<IAlgorithmQuery>().Query("", "", "", id, "", 1, 1, count).empty();
            }
            if (kind == "schedule")
                return Service<IScheduleService>().Exist(id);
            if (kind == "channel") {
                size_t count = 0;
                for (const auto& channel : Service<ICameraDeviceCrud>().Query("", -1, 1, 10000, count)) {
                    if (channel.videoChannelId == id)
                        return true;
                }
            }
            return false;
        }

        void Apply(Json& resource, const fs::path& blobs) override {
            const auto kind = resource.at("kind").get<std::string>();
            const auto id   = resource.at("localId").get<std::string>();
            if (kind == "model") {
                ApplyModel(resource, blobs);
            } else if (kind == "channel") {
                auto config = Channel(resource);
                if (config.channelType == MsgCameraType::MsgCameraTypeOnvif) {
                    try {
                        Require(Service<IOnvifService>().SaveManaged(resource["config"].at("onvif"),
                                                                     config.url) == config.url,
                                "ONVIF_SOURCE_MISMATCH");
                    } catch (const std::exception&) {
                        throw ManagementError("ONVIF_SOURCE_SAVE_FAILED");
                    }
                }
                if (Exists(kind, id)) {
                    // An immutable version must never be silently repaired over a local edit.
                    Require(Inspect(resource).value("state", "") == "READY", "NATIVE_CONFIGURATION_DRIFT");
                } else {
                    if (config.channelType == MsgCameraType::MsgCameraTypeLocalVideo) {
                        const auto& file = resource["files"][0];
                        auto staging     = fs::path(path::GetUploadTmpPath()) /
                                       ("management-video-" + id + fs::path(config.url).extension().string());
                        fs::copy_file(blobs / file.at("hash").get<std::string>(), staging,
                                      fs::copy_options::overwrite_existing);
                        config.url = staging.string();
                    }
                    std::string created;
                    Check(Service<ICameraDeviceCrud>().Add(config, created));
                    Require(created == id, "NATIVE_ID_MISMATCH");
                }
                if (config.channelType == MsgCameraType::MsgCameraTypeLocalVideo) {
                    SyncNativePath(Channel(resource).url);
                    SyncNativePath(path::GetCameraPath());
                }
                Require(Service<ICameraDeviceCrud>().FlushConfiguration(), "STORAGE_FAILED");
            } else if (kind == "schedule") {
                Check(Service<IScheduleService>().PutManaged(Schedule(resource)));
            } else if (kind == "scene") {
                Check(Service<IAlgorithmLayout>().LayoutSave(
                    Layout(resource["config"], id, resource["versionId"], resource["name"])));
            } else if (kind == "task") {
                const auto scene = resource.at("executionId").get<std::string>();
                Reference(resource, "scene");
                const auto camera = Reference(resource, "channel");
                auto schedule     = Reference(resource, "schedule", false);
                if (schedule.empty())
                    schedule = Service<IScheduleService>().GetDefaultId();
                auto params = TaskConfig(resource);
                Check(Service<IAlgorithmLayout>().LayoutSave(Layout(
                    resource.at("executionLayout"), scene, resource.at("versionId"), resource.at("name"))));
                Check(Service<ICameraTaskConfig>().PrepareTask(camera, scene, params, schedule));
                bool found = false;
                for (const auto& task : Service<ICameraTaskConfig>().GetTasks(camera)) {
                    if (task.algorithmCode == scene) {
                        Require(task.ready && !task.enable, "TASK_NOT_PREPARED");
                        resource["localId"] = task.taskId;
                        found               = !task.taskId.empty();
                    }
                }
                Require(found, "TASK_NOT_PREPARED");
            }
        }

        Json Inspect(const Json& resource) override {
            const auto kind = resource.at("kind").get<std::string>();
            const auto id   = resource.at("localId").get<std::string>();
            bool ready      = false;
            Json snapshot;
            if (kind == "task") {
                for (const auto& task :
                     Service<ICameraTaskConfig>().GetTasks(Reference(resource, "channel"))) {
                    if (task.algorithmCode == resource.at("executionId")) {
                        std::vector<MsgDynamicKeyValue> params;
                        std::vector<MsgTaskArea> areas, shielded;
                        const auto camera = Reference(resource, "channel");
                        const auto scene  = resource.at("executionId").get<std::string>();
                        Check(Service<ICameraTaskConfig>().QueryTaskParam(camera, scene, params));
                        Check(Service<ICameraTaskConfig>().QueryTaskArea(camera, scene, areas, shielded));
                        algorithm::LayoutDetailResult layout;
                        Check(Service<IAlgorithmLayout>().GetLayoutDetail(scene, "", layout));
                        snapshot = {{"params", params},
                                    {"areas", areas},
                                    {"shieldedAreas", shielded},
                                    {"scheduleId", task.scheduleId},
                                    {"metadata", layout.algorithmMetadata},
                                    {"flow", layout.algorithmProcessdata},
                                    {"atomicList", layout.atomicList}};
                        return {{"state", task.ready && task.taskId == id ? "READY" : "FAILED"},
                                {"runtimeState", task.runtimeState},
                                {"fingerprint", Fingerprint(snapshot)}};
                    }
                }
                return {{"state", "MISSING"}, {"runtimeState", "unknown"}};
            }
            if (kind == "model") {
                ready = Service<IModelService>().ModelValid(id);
                std::string config, model;
                ready = ready && Service<IModelService>().GetModelCfg(id, config, model) &&
                        (fs::is_regular_file(model) || fs::is_directory(model));
                const auto directory = ModelDirectory(id);
                std::ifstream marker(directory / ".management-ready");
                std::string receipt;
                std::getline(marker, receipt);
                ready = ready && receipt == resource.at("hash").get<std::string>();
                std::string content, defaults;
                bool exportable = false;
                if (ready && Service<IModelService>().GetModelConfig(id, content, exportable, defaults) ==
                                 util::ErrorEnum::Success) {
                    snapshot = {{"config", Json::parse(content)}, {"files", Json::object()}};
                    for (const auto& file : fs::directory_iterator(directory)) {
                        if (file.is_regular_file() && file.path().filename() != ".management-ready") {
                            snapshot["files"][file.path().filename().string()] = {
                                {"size", file.file_size()},
                                {"modified", file.last_write_time().time_since_epoch().count()}};
                        }
                    }
                } else
                    ready = false;
            } else if (kind == "scene") {
                algorithm::LayoutDetailResult actual;
                auto expected = Layout(resource["config"], id, resource["versionId"], resource["name"]);
                ready =
                    Service<IAlgorithmLayout>().GetLayoutDetail(id, "", actual) == util::ErrorEnum::Success &&
                    Service<IAlgorithmQuery>().GetAlgorithm(id) != nullptr &&
                    actual.algorithmProcessdata == expected.algorithmProcessdata &&
                    actual.atomicList == expected.atomicList;
                snapshot = {{"metadata", actual.algorithmMetadata}, {"flow", actual.algorithmProcessdata},
                            {"atomicList", actual.atomicList},      {"name", actual.algorithmName},
                            {"category", actual.algorithmCategory}, {"usage", actual.algorithmUsage}};
            } else if (kind == "schedule") {
                auto expected = Schedule(resource);
                size_t count  = 0;
                for (const auto& actual : Service<IScheduleService>().Query("", 1, 10000, count)) {
                    if (actual.scheduleId == id)
                        ready = Json(actual) == Json(expected);
                }
            } else if (kind == "channel") {
                auto expected = Channel(resource);
                size_t count  = 0;
                for (const auto& actual : Service<ICameraDeviceCrud>().Query("", -1, 1, 10000, count)) {
                    if (actual.videoChannelId == id)
                        ready = actual.url == expected.url && actual.channelType == expected.channelType &&
                                actual.channelName == expected.channelName;
                    if (actual.videoChannelId == id &&
                        expected.channelType == MsgCameraType::MsgCameraTypeLocalVideo)
                        ready = ready && fs::is_regular_file(actual.url);
                    if (actual.videoChannelId == id &&
                        expected.channelType == MsgCameraType::MsgCameraTypeOnvif) {
                        const auto source = Service<IOnvifService>().Describe(expected.url);
                        ready             = ready &&
                                source.at("profileToken") == resource["config"]["onvif"].at("profileToken");
                    }
                }
            }
            Json result = {{"state", ready ? "READY" : "MISSING"}};
            if (!snapshot.is_null())
                result["fingerprint"] = Fingerprint(snapshot);
            return result;
        }

        void Remove(const Json& resource) override {
            const auto kind = resource.at("kind").get<std::string>();
            const auto id   = resource.at("localId").get<std::string>();
            if (kind == "task") {
                auto camera = Reference(resource, "channel", false);
                auto scene  = resource.at("executionId").get<std::string>();
                if (!camera.empty()) {
                    for (const auto& task : Service<ICameraTaskConfig>().GetTasks(camera)) {
                        if (task.algorithmCode == scene)
                            Check(Service<ICameraTaskConfig>().DeleteTask(camera, scene));
                    }
                }
                if (Exists("scene", scene))
                    Check(Service<IAlgorithmCrud>().Delete(scene));
                return;
            }
            if (!Exists(kind, id)) {
                CleanupFiles(resource);
                return;
            }
            if (kind == "model") {
                Require(Service<IAlgorithmQuery>().GetAlgorithmsByModelId(id).empty(), "RESOURCE_IN_USE");
                Check(Service<IModelService>().DeleteModel(id));
            } else if (kind == "scene") {
                Require(!Service<ICameraTaskConfig>().IsAlgorithmInUse(id), "RESOURCE_IN_USE");
                Check(Service<IAlgorithmCrud>().Delete(id));
            } else if (kind == "schedule") {
                Require(!Service<ICameraTaskConfig>().ScheduleInUse(id), "RESOURCE_IN_USE");
                Check(Service<IScheduleService>().Delete(id));
            } else if (kind == "channel") {
                Require(Service<ICameraTaskConfig>().GetTasks(id).empty(), "RESOURCE_IN_USE");
                Check(Service<ICameraDeviceCrud>().Delete(id));
            }
            Require(!Exists(kind, id), "NATIVE_REMOVE_FAILED");
            CleanupFiles(resource);
        }

        void Activate(const Json& resource, bool enabled) override {
            auto camera = Reference(resource, "channel");
            auto scene  = resource.at("executionId").get<std::string>();
            // Do not let the legacy SwitchTask path auto-create an unprepared task.
            bool exists = false;
            for (const auto& task : Service<ICameraTaskConfig>().GetTasks(camera)) {
                if (task.algorithmCode == scene)
                    exists = true;
            }
            if (!enabled && !exists)
                return;
            Require(exists, "TASK_NOT_PREPARED");
            Check(Service<ICameraTaskConfig>().SwitchManagedTask(camera, scene, enabled));
            if (!enabled) {
                for (int i = 0; i < 100; ++i) {
                    bool stopped = true;
                    for (const auto& task : Service<ICameraTaskConfig>().GetTasks(camera)) {
                        if (task.algorithmCode == scene && (task.enable || task.runtimeState == "stopping" ||
                                                            task.runtimeState == "running"))
                            stopped = false;
                    }
                    if (stopped)
                        return;
                    std::this_thread::sleep_for(std::chrono::milliseconds(20));
                }
                // This is recoverable, not a terminal business rejection. Keep PENDING.
                throw std::runtime_error("Native task stop is pending");
            }
        }

    private:
        // A failed native create may leave files before its registry entry exists.
        // Only paths derived from our durable, reserved IDs are cleaned here.
        void CleanupFiles(const Json& resource) {
            const auto kind = resource.at("kind").get<std::string>();
            const auto id   = resource.at("localId").get<std::string>();
            if (kind == "model") {
                auto directory = ModelDirectory(id);
                fs::remove_all(directory);
                if (fs::exists(directory.parent_path()))
                    SyncNativePath(directory.parent_path());
            } else if (kind == "channel") {
                Require(Service<ICameraDeviceCrud>().FlushConfiguration(), "STORAGE_FAILED");
                Require(id.size() == 32 && id.find_first_not_of("0123456789abcdef") == std::string::npos,
                        "INVALID_LOCAL_ID");
                const auto type = resource.at("config").value("type", std::string{});
                if (type == "onvif" && resource["config"].contains("onvif"))
                    Service<IOnvifService>().Remove("onvif://managed-" + id);
                if (type == "video") {
                    for (const char* ext : {".mp4", ".avi", ".mkv", ".mov", ".ts"})
                        fs::remove(fs::path(path::GetCameraPath()) / (id + ext));
                    if (fs::exists(path::GetCameraPath()))
                        SyncNativePath(path::GetCameraPath());
                }
            }
        }

        void ApplyModel(const Json& resource, const fs::path& blobs) {
            const auto& config = resource.at("config");
            auto id            = resource.at("localId").get<std::string>();
            Require(config.at("chip") == NativeChip() && NativeChip() != "unknown", "MODEL_CHIP_MISMATCH");
            Require(config.at("runtime") == NativeRuntime(), "MODEL_RUNTIME_MISMATCH");
            auto options = config.value("config", Json::object());
            Require(options.is_object(), "INVALID_MODEL_OPTIONS");
            for (auto it = options.begin(); it != options.end(); ++it) {
                Require(it.key() == "normalizationMode" || it.key() == "colorChannel" ||
                            it.key() == "artifactRoles",
                        "UNSUPPORTED_MODEL_OPTION");
            }
            if (Inspect(resource).value("state", "") == "READY")
                return;
            cosmo::Model::MsgAddRecv request;
            request.modelType = config.at("modelType").get<std::string>();
            Require(path::IsSafePathComponent(request.modelType, 64), "INVALID_MODEL_TYPE");
            request.description       = resource.at("name").get<std::string>();
            request.normalizationMode = options.value("normalizationMode", std::string{});
            request.colorChannel      = options.value("colorChannel", std::string{});
            auto roles                = options.value("artifactRoles", Json::object());
            Require(roles.is_object(), "INVALID_ARTIFACT_ROLES");
            auto staging = fs::path(path::GetUploadTmpPath()) / ("management-" + util::GenerateUUID());
            fs::create_directories(staging);
            try {
                std::set<std::string> assigned;
                for (const auto& file : resource.at("files")) {
                    const auto name = file.at("name").get<std::string>();
                    const auto hash = file.at("hash").get<std::string>();
                    const auto role = roles.value(name, std::string("model"));
                    Require(assigned.insert(role).second, "DUPLICATE_ARTIFACT_ROLE");
                    const auto ext = fs::path(name).extension().string();
                    Require(ext == ".onnx" || ext == ".bmodel" || ext == ".rknn" || ext == ".rkllm" ||
                                ext == ".txt" || ext == ".json",
                            "UNSUPPORTED_MODEL_FILE");
                    if (role != "vocab" && role != "tokenizer" && role != "characters") {
                        Require((NativeRuntime() == "onnx" && ext == ".onnx") ||
                                    (NativeRuntime() == "bmrt" && ext == ".bmodel") ||
                                    (NativeRuntime() == "rknn" && (ext == ".rknn" || ext == ".rkllm")),
                                "MODEL_BINARY_RUNTIME_MISMATCH");
                    }
                    auto dest = staging / (hash + ext);
                    fs::copy_file(blobs / hash, dest, fs::copy_options::overwrite_existing);
                    if (role == "vocab")
                        request.vocabFilePath = dest.string();
                    else if (role == "tokenizer")
                        request.tokenizerFilePath = dest.string();
                    else if (role == "characters")
                        request.characterTableFilePath = dest.string();
                    else {
                        cosmo::Model::BmodelFileInfo info;
                        info.role     = role;
                        info.filePath = dest.string();
                        request.bmodelFiles.push_back(info);
                    }
                }
                Check(Service<IModelService>().AddManagedModel(id, request));
                if (config.contains("nativeConfig")) {
                    std::string current, defaults;
                    bool exportable = false;
                    Check(Service<IModelService>().GetModelConfig(id, current, exportable, defaults));
                    const auto merged = MergeManagedModelConfig(Json::parse(current), config["nativeConfig"]);
                    Check(Service<IModelService>().SaveModelConfig(id, merged.dump()));
                }
                const auto directory = ModelDirectory(id);
                for (const auto& file : fs::recursive_directory_iterator(directory)) {
                    if (file.is_regular_file())
                        SyncNativePath(file.path());
                }
                SyncNativePath(directory);
                SyncNativePath(directory.parent_path());
                Require(util::WriteFileAtomically((directory / ".management-ready").string(),
                                                  resource.at("hash").get<std::string>()),
                        "STORAGE_FAILED");
            } catch (...) {
                fs::remove_all(staging);
                throw;
            }
            fs::remove_all(staging);
        }
    };
}  // namespace

std::unique_ptr<IManagedResources> MakeNativeManagedResources() {
    return std::make_unique<NativeManagedResources>();
}
}  // namespace cosmo::service
