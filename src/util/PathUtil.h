/// @file PathUtil.h
/// @brief Filesystem path helpers for the Cosmo application.
///
/// All path accessors are pure free-functions in the `cosmo::path` namespace.
/// Call cosmo::path::Init() once at application startup (in SwDevicePreInit)
/// to run one-time cleanup of transient directories.
///
/// Replaces the PathServiceImpl / IPathService DI layer — no virtual dispatch,
/// no ServiceRegistry registration needed.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace cosmo::path {

// ── Lifecycle ────────────────────────────────────────────────────────────────

/// One-time startup initialisation: clears upload/web transient directories
/// and recreates the CWAI runtime data path. Call once from SwDevicePreInit().
void Init();

/// Test-only: override the root data paths to isolate unit tests from the
/// real filesystem.  Must be called before any path accessor.
/// @param dataPath    Replaces the default /data/cwaiuserdata
/// @param appDataPath Replaces the default /appfs/cosmo_wander/cwai_data
void OverrideRootPathForTest(const std::string& dataPath, const std::string& appDataPath);

// ── Configuration paths (formerly IPathConfig) ───────────────────────────────

/// Primary configuration directory.
std::string GetCfgPath();

/// Backup configuration directory.
std::string GetBackupCfgPath();

/// Primary configuration directory with @p subPath appended.
std::string GetCfgPath(const std::string& subPath);

/// Backup configuration directory with @p subPath appended.
std::string GetBackupCfgPath(const std::string& subPath);

/// Web file upload staging directory.
std::string GetUploadPath();

/// Per-request temporary upload directory (UUID-named subdirectory).
std::string GetUploadTmpPath();

/// Firmware upgrade package directory.
std::string GetUpgradePath();

/// Application log directory.
std::string GetLogPath();

/// Primary database file directory.
std::string GetDbPath();

/// Database backup directory.
std::string GetDbBackUpPath();

/// Root data directory for the application.
std::string GetBaseDir();

// ── Event / recording paths (formerly IPathEvent) ────────────────────────────

/// Root directory for event recordings.
/// @param autoCreate Create the directory if absent (default: true).
std::string GetEventRootPath(bool autoCreate = true);

/// Time-partitioned event directory for @p timestamp (ms epoch).
/// @param autoCreate Create the directory if absent.
std::string GetEventPath(uint64_t timestamp, bool autoCreate = true);

/// Web-accessible URL path for event files at @p timestamp.
std::string GetEventWebPath(uint64_t timestamp);

/// Strip the data-path prefix from @p dir to produce a web-relative path.
std::string GetWebDir(const std::string& dir);

/// Root web-accessible temporary file directory (no auto-create).
std::string GetWebRootPath();

/// Local temporary file path partitioned by @p timestamp.
std::string GetWebLocalPath(uint64_t timestamp);

/// Local temporary file path (current-time partition).
std::string GetWebLocalPath();

/// Web-accessible URL path for a temporary file at @p timestamp.
std::string GetWebAcessPath(uint64_t timestamp);

/// Web-accessible URL path for a temporary file (current-time partition).
std::string GetWebAcessPath();

/// Application temporary working directory.
std::string GetTemporaryDirPath();

// ── Model / algorithm paths (formerly IPathModel) ────────────────────────────

/// Primary user model storage directory.
std::string GetModelPath();

/// All directories to search for model files (user + preset).
std::vector<std::string> GetModelSearchPaths();

/// Model template directory (pre-installed default models).
std::string GetModelTemplatePath();

/// Temporary directory for atomic model uploads.
std::string GetModelUploadTmpDir();

/// Algorithm package storage directory.
std::string GetAlgorithmPath();

/// Shared resource directory (fonts, icons, etc.).
std::string GetResourcePath();

/// Algorithm layout configuration directory.
std::string GetLayoutPath();

/// Path to the actions.json definition file.
std::string GetActionsJsonPath();

/// Path to the modelComponents.json definition file.
std::string GetModelComponentsJsonPath();

/// Path to the linkageStorages.json definition file.
std::string GetLinkageStoragesJsonPath();

/// File export staging directory.
std::string GetExportFileDir();

/// Face library photo storage directory.
std::string GetFaceLibPhotoDir();

/// Person library photo storage directory.
std::string GetPersonLibPhotoDir();

/// Web-accessible person library photo directory.
std::string GetPersonLibPhotoWebDir();

/// Body/articles ReID library photo directory.
std::string GetArticlesReidLibPhotoDir();

/// Camera configuration storage directory.
std::string GetCameraPath();

/// System logo file storage directory.
std::string GetLogoPath();

/// Web-accessible system logo directory.
std::string GetLogoWebPath();

// ── Task record paths (formerly IPathService) ─────────────────────────────────

/// Path to the daily task record JSON directory.
std::string GetRecordJsonPath();

/// Task overview data directory for @p taskId.
std::string GetTaskOverviewDataPath(const std::string& taskId);

}  // namespace cosmo::path
