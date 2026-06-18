// ModelUploadHelper — handles chunked and single-shot temporary file uploads.
// Extracted from ModelServiceImpl to reduce class size.
#pragma once

#include <string>

#include "util/ErrorCode.h"

namespace cosmo::service {

class ModelUploadHelper {
public:
    ModelUploadHelper() = default;

    ModelUploadHelper(const ModelUploadHelper&)            = delete;
    ModelUploadHelper& operator=(const ModelUploadHelper&) = delete;

    cosmo::util::ErrorEnum UploadTempFile(const std::string& filePath, const std::string& fileName,
                                          const std::string& contentLength, const std::string& uploadId,
                                          const std::string& chunkIndex, const std::string& totalChunks,
                                          std::string& persistentPath);
};

}  // namespace cosmo::service
