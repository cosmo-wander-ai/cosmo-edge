// PathUtil — Filesystem path helpers for the Cosmo application.

#include "util/PathUtil.h"

#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

#include "util/DateTimeFormat.h"
#include "util/FileUtil.h"
#include "util/FormatString.h"
#include "util/Log.h"
#include "util/UuidUtil.h"

namespace fs = std::filesystem;

namespace cosmo::path {

// ── Internal helpers ──────────────────────────────────────────────────────────

namespace {

    constexpr const char* kCwaiRuntimeDir = "cwai";

    // Root paths — mutable so OverrideRootPathForTest() can redirect them.
    // NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables)
    std::string g_dataPath{"/data/cwaiuserdata"};
    std::string g_appDataPath{"/appfs/cosmo_wander/cwai_data"};
    // NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

    // Derived-path accessors (recomputed on every call so they reflect overrides)
    std::string LogPath() {
        return g_dataPath + "/log";
    }
    std::string MainCfgPath() {
        return g_dataPath + "/conf";
    }
    std::string BackupCfgPath() {
        return g_dataPath + "/confb";
    }
    std::string UploadPath() {
        return g_dataPath + "/upload";
    }
    std::string ModelUploadTmpDir() {
        return g_dataPath + "/tmp/model_upload";
    }
    std::string UpgradePath() {
        return g_dataPath + "/upgrade";
    }
    std::string CameraPath() {
        return g_dataPath + "/camera";
    }
    std::string UserResourcePath() {
        return g_dataPath + "/resource";
    }
    std::string ResourcePath() {
        return g_appDataPath + "/resource";
    }
    std::string LayoutPath() {
        return ResourcePath() + "/layout";
    }
    std::string ModelPath() {
        return UserResourcePath() + "/models";
    }
    std::string PresetModelPath() {
        return ResourcePath() + "/models";
    }
    std::string ModelTemplatePath() {
        return ResourcePath() + "/model_template";
    }
    std::string AlgorithmPath() {
        return ResourcePath() + "/algorithm";
    }
    std::string TemporaryDir() {
        return g_dataPath + "/temporary";
    }
    std::string FileDir() {
        return g_dataPath + "/export";
    }
    std::string FaceLibPhotoDir() {
        return g_dataPath + "/weblibPic/facePicture";
    }
    std::string PersonLibPhotoDir() {
        return g_dataPath + "/weblibPic/personPicture";
    }
    std::string ArticlesReidLibPhotoDir() {
        return g_dataPath + "/weblibPic/articleslib";
    }
    std::string DbPath() {
        return g_dataPath + "/db";
    }
    std::string DbBackupPath() {
        return g_dataPath + "/db2";
    }

    // Relative path tokens used to build event / web paths
    const std::string kAtEvent{"event"};
    const std::string kAtWeb{"web"};
    const std::string kAtLogoWeb{"weblogo"};
    const std::string kAtPersonLibPicWeb{"/weblibPic/personPicture"};

    void EnsureDir(const std::string& path) {
        if (!fs::exists(path)) {
            std::error_code err;
            bool ret = fs::create_directories(path, err);
            LOG_INFO("create dir: {}, ret: {}", path, ret);
        }
    }

    std::string RelativePath(uint64_t timestamp) {
        time_t t  = static_cast<time_t>(timestamp / 1000);
        auto date = cosmo::util::YMDDate(t);
        return COSMO_FORMAT("{:04}/{:02}/{:02}", date.Year(), date.Month(), date.Day());
    }

    std::string RecordPath(const std::string& type, uint64_t timestamp, bool autoCreate) {
        std::string p = (fs::path(g_dataPath) / type / RelativePath(timestamp)).string();
        if (autoCreate)
            EnsureDir(p);
        return p;
    }

    std::string RecordPath(const std::string& type, bool autoCreate) {
        std::string p = (fs::path(g_dataPath) / type).string();
        if (autoCreate)
            EnsureDir(p);
        return p;
    }

    std::string WebPath(const std::string& type) {
        return (fs::path("/") / type).string();
    }

    std::string WebPath(const std::string& type, uint64_t timestamp) {
        return (fs::path("/") / type / RelativePath(timestamp)).string();
    }

}  // namespace

// ── Lifecycle ─────────────────────────────────────────────────────────────────

void Init() {
    cosmo::util::RemovePath(UploadPath());
    cosmo::util::RemovePath(RecordPath(kAtWeb, false));

    std::string cwaiRuntimePath = (fs::path(g_dataPath) / kCwaiRuntimeDir).string();
    cosmo::util::RemovePath(cwaiRuntimePath);
    EnsureDir(cwaiRuntimePath);
}

void OverrideRootPathForTest(const std::string& dataPath, const std::string& appDataPath) {
    g_dataPath    = dataPath;
    g_appDataPath = appDataPath;
}

// ── Configuration paths ───────────────────────────────────────────────────────

std::string GetCfgPath() {
    auto p = MainCfgPath();
    EnsureDir(p);
    return p;
}

std::string GetBackupCfgPath() {
    auto p = BackupCfgPath();
    EnsureDir(p);
    return p;
}

std::string GetCfgPath(const std::string& subPath) {
    std::string p = (fs::path(MainCfgPath()) / subPath).string();
    EnsureDir(p);
    return p;
}

std::string GetBackupCfgPath(const std::string& subPath) {
    std::string p = (fs::path(BackupCfgPath()) / subPath).string();
    EnsureDir(p);
    return p;
}

std::string GetUploadPath() {
    auto p = UploadPath();
    EnsureDir(p);
    return p;
}

std::string GetUploadTmpPath() {
    std::string uuid = cosmo::util::GenerateUUID();
    std::string p    = (fs::path(UploadPath()) / uuid).string();
    EnsureDir(p);
    return p;
}

std::string GetUpgradePath() {
    auto p = UpgradePath();
    EnsureDir(p);
    return p;
}

std::string GetLogPath() {
    auto p = LogPath();
    EnsureDir(p);
    return p;
}

std::string GetDbPath() {
    auto p = DbPath();
    EnsureDir(p);
    return p;
}

std::string GetDbBackUpPath() {
    auto p = DbBackupPath();
    EnsureDir(p);
    return p;
}

std::string GetBaseDir() {
    EnsureDir(g_dataPath);
    return g_dataPath;
}

// ── Event / recording paths ───────────────────────────────────────────────────

std::string GetEventRootPath(bool autoCreate) {
    return RecordPath(kAtEvent, autoCreate);
}

std::string GetEventPath(uint64_t timestamp, bool autoCreate) {
    return RecordPath(kAtEvent, timestamp, autoCreate);
}

std::string GetEventWebPath(uint64_t timestamp) {
    return WebPath(kAtEvent, timestamp);
}

std::string GetWebDir(const std::string& dir) {
    if (dir.size() > g_dataPath.size()) {
        return dir.substr(g_dataPath.size());
    }
    return {};
}

std::string GetWebRootPath() {
    return RecordPath(kAtWeb, false);
}

std::string GetWebLocalPath(uint64_t timestamp) {
    return RecordPath(kAtWeb, timestamp, true);
}

std::string GetWebLocalPath() {
    return RecordPath(kAtWeb, true);
}

std::string GetWebAcessPath(uint64_t timestamp) {
    return WebPath(kAtWeb, timestamp);
}

std::string GetWebAcessPath() {
    return WebPath(kAtWeb);
}

std::string GetTemporaryDirPath() {
    auto p = TemporaryDir();
    EnsureDir(p);
    return p;
}

// ── Model / algorithm paths ───────────────────────────────────────────────────

std::string GetModelPath() {
    auto p = ModelPath();
    EnsureDir(p);
    return p;
}

std::vector<std::string> GetModelSearchPaths() {
    auto mp = ModelPath();
    auto pp = PresetModelPath();
    EnsureDir(mp);
    EnsureDir(pp);
    return {mp, pp};
}

std::string GetModelTemplatePath() {
    auto p = ModelTemplatePath();
    EnsureDir(p);
    return p;
}

std::string GetModelUploadTmpDir() {
    auto p = ModelUploadTmpDir();
    EnsureDir(p);
    return p;
}

std::string GetAlgorithmPath() {
    auto p = AlgorithmPath();
    EnsureDir(p);
    return p;
}

std::string GetResourcePath() {
    auto p = ResourcePath();
    EnsureDir(p);
    return p;
}

std::string GetLayoutPath() {
    auto p = LayoutPath();
    EnsureDir(p);
    return p;
}

std::string GetActionsJsonPath() {
    return (fs::path(GetLayoutPath()) / "actions.json").string();
}

std::string GetModelComponentsJsonPath() {
    return (fs::path(GetLayoutPath()) / "modelComponents.json").string();
}

std::string GetLinkageStoragesJsonPath() {
    return (fs::path(GetLayoutPath()) / "linkageStorages.json").string();
}

// ── Media paths ───────────────────────────────────────────────────────────────

std::string GetExportFileDir() {
    auto p = FileDir();
    EnsureDir(p);
    return p;
}

std::string GetFaceLibPhotoDir() {
    auto p = FaceLibPhotoDir();
    EnsureDir(p);
    return p;
}

std::string GetPersonLibPhotoDir() {
    auto p = PersonLibPhotoDir();
    EnsureDir(p);
    return p;
}

std::string GetPersonLibPhotoWebDir() {
    return kAtPersonLibPicWeb;
}

std::string GetArticlesReidLibPhotoDir() {
    auto p = ArticlesReidLibPhotoDir();
    EnsureDir(p);
    return p;
}

std::string GetCameraPath() {
    auto p = CameraPath();
    EnsureDir(p);
    return p;
}

std::string GetLogoPath() {
    return RecordPath(kAtLogoWeb, true);
}

std::string GetLogoWebPath() {
    return WebPath(kAtLogoWeb);
}

// ── Task record paths ─────────────────────────────────────────────────────────

std::string GetRecordJsonPath() {
    auto dataTime        = cosmo::util::GetCurrentDateTime();
    std::string datePath = COSMO_FORMAT("{:04}/{:02}/{:02}", dataTime.Date().Year(), dataTime.Date().Month(),
                                        dataTime.Date().Day());
    std::string p        = (fs::path(g_dataPath) / kCwaiRuntimeDir / "record" / datePath).string();
    fs::create_directories(p);
    return p;
}

std::string GetTaskOverviewDataPath(const std::string& taskId) {
    std::string p = (fs::path(g_dataPath) / kCwaiRuntimeDir / "overview" / taskId).string();
    fs::create_directories(p);
    return p;
}

}  // namespace cosmo::path
