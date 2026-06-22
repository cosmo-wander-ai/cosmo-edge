// clang-format off
#include "service/detail/ServiceRegistry.h"
#include "service/model/impl/ModelUploadHelper.h"
// clang-format on

#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

#include "util/ErrorCode.h"
#include "util/Log.h"
#include "util/PathUtil.h"
#include "util/TimingConstants.h"

namespace cosmo::service {

cosmo::util::ErrorEnum ModelUploadHelper::UploadTempFile(
    const std::string& filePath, const std::string& fileName, const std::string& contentLength,
    const std::string& uploadId, const std::string& chunkIndex, const std::string& totalChunks,
    std::string& persistentPath) {
    LOG_INFO("UploadTemp received filePath: {}, fileName: {}, contentLength: {}", filePath, fileName,
             contentLength);

    if (filePath.empty()) {
        LOG_WARN("{}", "filePath is empty in UploadTemp request");
        return cosmo::util::ErrorEnum::InvalidParam;
    }

    namespace fs = std::filesystem;
    fs::path sourceFilePath(filePath);

    LOG_INFO("Checking uploaded file existence: {}", filePath);

    // Allow retries since file may be in the process of being written
    int retry_count  = 5;
    bool file_exists = false;
    for (int i = 0; i < retry_count; i++) {
        if (fs::exists(sourceFilePath) && fs::is_regular_file(sourceFilePath)) {
            file_exists = true;
            break;
        }
        if (i < retry_count - 1) {
            LOG_INFO("File not found, retrying... ({}/{})", i + 1, retry_count);
            std::this_thread::sleep_for(timing::kMediumPollInterval);
        }
    }

    if (!file_exists) {
        LOG_WARN("Uploaded file does not exist after {} retries: {}", retry_count, filePath);
        return cosmo::util::ErrorEnum::FileNotExist;
    }

    // Get file size
    try {
        size_t file_size = fs::file_size(sourceFilePath);
        LOG_INFO("Source file exists: {}, size: {} bytes", filePath, file_size);
    } catch (const std::exception& e) {
        LOG_WARN("Failed to get file size: {}", e.what());
    }

    const std::string persistent_dir = cosmo::path::GetModelUploadTmpDir();

    auto sanitize = [](std::string s) {
        for (auto& c : s) {
            if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '_' || c == '-')) {
                c = '_';
            }
        }
        if (s.size() > 200)
            s.resize(200);
        return s;
    };

    // Chunk upload mode: same endpoint called multiple times, each time uploading a chunk
    auto parse_int = [](const std::string& s, int def_val) -> int {
        try {
            if (s.empty())
                return def_val;
            return std::stoi(s);
        } catch (const std::exception&) {
            return def_val;
        }
    };

    const int chunk_idx      = parse_int(chunkIndex, -1);
    const int total_ch       = parse_int(totalChunks, 0);
    const bool is_chunk_mode = (!uploadId.empty() && total_ch > 0 && chunk_idx >= 0);
    if (is_chunk_mode) {
        const std::string sanitized_upload_id = sanitize(uploadId);
        const std::string sanitized_file_name = sanitize(fileName);
        const fs::path destPath =
            fs::path(persistent_dir) / ("chunk_" + sanitized_upload_id + "_" + sanitized_file_name);
        const fs::path metaPath = fs::path(persistent_dir) / ("chunk_" + sanitized_upload_id + ".meta");

        // Sequence validation: record nextIndex, require sequential upload
        int expected_index = 0;
        if (fs::exists(metaPath)) {
            try {
                std::ifstream metaIn(metaPath);
                metaIn >> expected_index;
            } catch (const std::exception& e) {
                LOG_WARN("[UploadTempChunk] read meta failed: {}", e.what());
            }
        } else {
            try {
                std::ofstream metaOut(metaPath, std::ios::trunc);
                metaOut << 0;
            } catch (const std::exception& e) {
                LOG_WARN("[UploadTempChunk] create meta failed: {}", e.what());
            }
        }

        if (chunk_idx != expected_index) {
            LOG_WARN("[UploadTempChunk] out-of-order chunk: uploadId={}, got={}, expected={}",
                     sanitized_upload_id, chunk_idx, expected_index);
            return cosmo::util::ErrorEnum::InvalidParam;
        }

        // Append write
        try {
            std::ifstream in(sourceFilePath, std::ios::binary);
            if (!in.is_open()) {
                LOG_WARN("[UploadTempChunk] open chunk input failed: {}", sourceFilePath.string());
                return cosmo::util::ErrorEnum::SysErr;
            }
            std::ofstream out(destPath, std::ios::binary | std::ios::app);
            if (!out.is_open()) {
                LOG_WARN("[UploadTempChunk] open dest failed: {}", destPath.string());
                return cosmo::util::ErrorEnum::SysErr;
            }

            char buf[1024 * 1024];
            while (in.good()) {
                in.read(buf, sizeof(buf));
                std::streamsize got = in.gcount();
                if (got > 0)
                    out.write(buf, got);
            }
            out.flush();
        } catch (const std::exception& e) {
            LOG_WARN("[UploadTempChunk] append failed: {}", e.what());
            return cosmo::util::ErrorEnum::SysErr;
        }

        // Update meta: nextIndex++
        try {
            std::ofstream metaOut(metaPath, std::ios::trunc);
            metaOut << (expected_index + 1);
        } catch (const std::exception& e) {
            LOG_WARN("[UploadTempChunk] update meta failed: {}", e.what());
        }

        // Try to clean up current chunk file
        try {
            fs::remove(sourceFilePath);
        } catch (const std::exception& e) {
            LOG_WARN("[UploadTempChunk] cleanup chunk file failed: {}", e.what());
        }

        const bool is_last = (chunk_idx + 1 >= total_ch);
        if (is_last) {
            size_t final_size = 0;
            try {
                final_size = fs::file_size(destPath);
            } catch (const std::exception& e) {
                LOG_WARN("[UploadTempChunk] get final file size failed: {}", e.what());
            }
            LOG_INFO("[UploadTempChunk] completed uploadId={}, file={}, size={}", sanitized_upload_id,
                     destPath.string(), final_size);

            try {
                fs::remove(metaPath);
            } catch (const std::exception& e) {
                LOG_WARN("[UploadTempChunk] remove meta file failed: {}", e.what());
            }
        }

        persistentPath = destPath.string();
        return cosmo::util::ErrorEnum::Success;
    }

    // Legacy single upload: copy temp file to persistent location, return persistent path
    auto now       = std::chrono::system_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    std::string persistent_file_name = "model_" + std::to_string(timestamp) + "_" + sanitize(fileName);
    fs::path persistentFilePath      = fs::path(persistent_dir) / persistent_file_name;

    LOG_INFO("Copying file to persistent location: {}", persistentFilePath.string());

    try {
        fs::copy_file(sourceFilePath, persistentFilePath, fs::copy_options::overwrite_existing);
        if (fs::exists(persistentFilePath) && fs::is_regular_file(persistentFilePath)) {
            auto persistent_file_size = fs::file_size(persistentFilePath);
            LOG_INFO("File copied successfully to persistent location: {}, size: {} bytes",
                     persistentFilePath.string(), persistent_file_size);
            persistentPath = persistentFilePath.string();
            return cosmo::util::ErrorEnum::Success;
        }
        LOG_WARN("Failed to verify copied file at persistent location: {}", persistentFilePath.string());
        return cosmo::util::ErrorEnum::SysErr;
    } catch (const fs::filesystem_error& e) {
        LOG_WARN("Filesystem error when copying file to persistent location: {}", e.what());
        return cosmo::util::ErrorEnum::SysErr;
    } catch (const std::exception& e) {
        LOG_WARN("Exception when copying file to persistent location: {}", e.what());
        return cosmo::util::ErrorEnum::SysErr;
    }
}

}  // namespace cosmo::service
