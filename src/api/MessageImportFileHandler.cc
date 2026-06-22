// MessageImportFileHandler — Message Import File Handler implementation.

#include "api/MessageImportFileHandler.h"

#include <charconv>
#include <filesystem>

#include "service/detail/ServiceRegistry.h"
#include "service/face/IFaceImport.h"
#include "service/media/IAudioService.h"
#include "util/ErrorCode.h"
#include "util/FileUtil.h"
#include "util/Log.h"

namespace cosmo {
namespace fs = std::filesystem;

static constexpr const char* kTag            = "ImportFileHandler";
static constexpr int64_t kMaxImportFileBytes = 500LL * 1024 * 1024;  // 500 MB

namespace {

    /// Parse content-length string to int64_t safely.  Returns -1 on failure.
    int64_t ParseContentLength(const std::string& s) {
        if (s.empty())
            return -1;
        int64_t value  = 0;
        auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), value);
        if (ec != std::errc{} || ptr != s.data() + s.size())
            return -1;
        return value;
    }

}  // namespace

service::MsgImportFileSend MessageImportFileHandler::Handle(service::MsgImportFileRecv&& data,
                                                            std::error_condition& errc) {
    LOG_INFO("{} filePath:{}  filename:{}", kTag, data.filePath, data.filename);

    if (data.importType == 1) {
        // Upgrade — not implemented
    } else if (data.importType == 2) {
        auto content_len = ParseContentLength(data.contentLength);
        if (content_len > kMaxImportFileBytes) {
            throw util::ErrorMessage(util::ErrorEnum::ParameterException, "File size cannot exceed 500MB");
        }
        service::ServiceRegistry::Instance().Get<service::IFaceImport>().ImportFile(data.filePath,
                                                                                    data.faceLibId);
    } else if (data.importType == 3) {
        // Person recognition import — not implemented
    } else if (data.importType == 4) {
        // Object import — not implemented
    } else if (data.importType == 5) {
        auto content_len = ParseContentLength(data.contentLength);
        if (content_len > kMaxImportFileBytes) {
            throw util::ErrorMessage(util::ErrorEnum::ParameterException, "File size cannot exceed 500MB");
        }
        std::error_code err;
        auto& audio_svc = service::ServiceRegistry::Instance().Get<service::IAudioService>();
        if (audio_svc.AudioFileCount() >= audio_svc.AudioFileMaxCount()) {
            errc               = util::ErrorEnum::MaxCountLimit;
            std::string errMsg = "Audio count (" + std::to_string(audio_svc.AudioFileCount()) +
                                 ") reached limit, cannot add more.";
            throw util::ErrorMessage(util::ErrorEnum::MaxCountLimit, errMsg.c_str());
        }
        if (fs::exists(data.filePath, err)) {
            if (!audio_svc.AddAudioFile(data.filePath)) {
                errc = util::ErrorEnum::ExistedName;
                LOG_WARN("{} File :{} In List", kTag, data.filePath);
                throw util::ErrorMessage(util::ErrorEnum::FileOpenFailed,
                                         "Uploaded file already exists, please upload another file");
            }
        } else {
            LOG_WARN("{} File :{} Not Exist", kTag, data.filePath);
            errc = util::ErrorEnum::FileOpenFailed;
            throw util::ErrorMessage(util::ErrorEnum::FileOpenFailed, "Uploaded file does not exist.");
        }
    } else {
        LOG_ERRO("{} data.importType :{} ", kTag, data.importType);
        errc = util::ErrorEnum::UnknownOperation;
    }
    return {};
}

// Import status query
service::MsgQueryImportStatusSend MessageImportFileHandler::Handle(service::MsgQueryImportStatusRecv&& data,
                                                                   std::error_condition& errc) {
    service::MsgQueryImportStatusSend result{};
    result.resData.importStatus.importType = data.importType;

    if (data.importType == 1) {
        return result;
    } else if (data.importType == 2) {
        auto& import_svc       = service::ServiceRegistry::Instance().Get<service::IFaceImport>();
        auto [success, failed] = import_svc.GetImportStatus();
        try {
            result.resData.importStatus.status = import_svc.ImportComplete() ? 1 : 0;
            result.resData.importStatus.statusMsg =
                "Success: " + std::to_string(success) + ", Failed: " + std::to_string(failed);
            auto total = import_svc.GetImportTotalCount();
            result.resData.importStatus.progress =
                (total > 0) ? static_cast<int>(static_cast<double>(success + failed) * 100.0 / total) : 100;
            result.resData.importStatus.processedNumber = success + failed;
            result.resData.importStatus.totalNumber     = total;
            if (failed != 0) {
                result.resData.importStatus.failedUrl = import_svc.GetImportFailedUrl();
            }
            result.resData.importStatus.successNumber = success;
            result.resData.importStatus.failedNumber  = failed;
        } catch (const util::ErrorMessage& e) {
            result.resData.importStatus.status    = 2;
            result.resData.importStatus.statusMsg = e.what();
            result.resData.importStatus.failedUrl = import_svc.GetImportFailedUrl();
            LOG_INFO("{} {}", kTag, e.what());
        }
        return result;
    } else if (data.importType == 3) {
        result.resData.importStatus.status    = 2;
        result.resData.importStatus.statusMsg = "Person import status module not migrated";
        return result;
    } else if (data.importType == 4) {
        result.resData.importStatus.status    = 2;
        result.resData.importStatus.statusMsg = "Object import status module not migrated";
        return result;
    }

    errc = util::ErrorEnum::UnknownOperation;
    return result;
}

}  // namespace cosmo