/// @file IFileService.h
/// @brief File service interface — abstracts file upload/download/URL
///        operations so that flow/device modules don't depend on network.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "service/detail/ServiceRegistry.h"

namespace cosmo::service {

/// File type enumeration for URL routing.
enum class FileType {
    Image = 1000,  ///< Image file (JPEG, PNG).
    Json,          ///< JSON metadata file.
    Video,         ///< Video recording file.
    Feature,       ///< Feature vector binary file.
};

/// Callback invoked when an async file upload completes.
/// @param taskId   Task identifier associated with the upload.
/// @param success  true if the upload succeeded.
/// @param userData Opaque user data pointer passed at upload time.
using FileUploadCallback = std::function<void(const std::string& taskId, bool success, void* userData)>;

/// Abstracts file I/O operations (upload/download) so that flow and
/// device modules are decoupled from the network/storage layer.
class IFileService {
public:
    virtual ~IFileService() = default;

    /// Get the upload URL for a given file type.
    /// @param type File type to upload.
    /// @return URL string for the upload endpoint.
    virtual std::string GetFileUrl(FileType type) = 0;

    /// Upload a local file to a remote destination asynchronously.
    /// @param taskId    Task identifier for progress tracking.
    /// @param callback  Completion callback.
    /// @param userData  Opaque user data forwarded to the callback.
    /// @param ext       File extension (e.g. "jpg").
    /// @param localPath Absolute path to the local file.
    /// @param bucket    Remote storage bucket name.
    /// @param remoteUrl Remote target URL.
    virtual void UploadFile(const std::string& taskId, const FileUploadCallback& callback, void* userData,
                            const std::string& ext, const std::string& localPath, const std::string& bucket,
                            const std::string& remoteUrl) = 0;

    /// Download a remote file into an in-memory buffer.
    /// @param url  Remote file URL.
    /// @param data [out] Downloaded file data.
    /// @return true on success.
    virtual bool DownloadFile(const std::string& url, std::vector<uint8_t>& data) = 0;
};

}  // namespace cosmo::service
