// ImportFile DTO definitions (extracted from MessageImportFileHandler.h)

#pragma once

#include <system_error>

#include "util/dto/ServerMsgTypes.h"

namespace cosmo {
namespace service {
    // Config/Update package response
    struct MsgImportFileSend : public MsgSendHead {
        struct ResData {
            std::string importId;
            friend void to_json(nlohmann::json& j, const ResData& v);
            friend void from_json(const nlohmann::json& j, ResData& v);
        } resData;
    };

    void to_json(nlohmann::json& j, const MsgImportFileSend& v);
    void from_json(const nlohmann::json& j, MsgImportFileSend& v);

    // Config/Update package request
    // Actual request is not this type
    struct MsgImportFileRecv : public MsgRecvHead {
        int importType{-1};
        std::string filePath;
        std::string filename;
        std::string faceLibId;
        std::string contentLength;
    };

    void to_json(nlohmann::json& j, const MsgImportFileRecv& v);
    void from_json(const nlohmann::json& j, MsgImportFileRecv& v);

    // Import status query request
    struct MsgQueryImportStatusRecv : public MsgRecvHead {
        int importType{-1};
        std::string importId;
    };

    void to_json(nlohmann::json& j, const MsgQueryImportStatusRecv& v);
    void from_json(const nlohmann::json& j, MsgQueryImportStatusRecv& v);

    // Import status query response
    struct MsgQueryImportStatusSend : public MsgSendHead {
        struct ResData {
            struct ImportStatus {
                int importType{-1};
                int status{0};
                std::string statusMsg;
                int progress{100};
                std::string failedUrl;
                int successNumber{0};
                int failedNumber{0};
                int processedNumber{0};
                int totalNumber{0};
            };

            friend void to_json(nlohmann::json& j, const ImportStatus& s);
            friend void from_json(const nlohmann::json& j, ImportStatus& s);

            ImportStatus importStatus;
            friend void to_json(nlohmann::json& j, const ResData& v);
            friend void from_json(const nlohmann::json& j, ResData& v);
        } resData;
    };

    void to_json(nlohmann::json& j, const MsgQueryImportStatusSend& v);
    void from_json(const nlohmann::json& j, MsgQueryImportStatusSend& v);

}  // namespace service
}  // namespace cosmo
