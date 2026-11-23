#pragma once

#include <cstdint>
#include <system_error>

#include "util/dto/ServerMsgTypes.h"

namespace cosmo::System {

// Restart/Reset request
struct MsgResetSystemRecv : public MsgRecvHead {
    int resetOperation{0};
};

void to_json(nlohmann::json& j, const MsgResetSystemRecv& v);
void from_json(const nlohmann::json& j, MsgResetSystemRecv& v);

// Restart/Reset response
struct MsgResetSystemSend : public MsgSendHead {
    struct ResData {
        int waitSeconds{10};
        std::string locationUrl;
        friend void to_json(nlohmann::json& j, const ResData& v);
        friend void from_json(const nlohmann::json& j, ResData& v);
    } resData;
};

void to_json(nlohmann::json& j, const MsgResetSystemSend& v);
void from_json(const nlohmann::json& j, MsgResetSystemSend& v);

// Config/Log export request
struct MsgExportFileRecv : public MsgRecvHead {
    int exportType{-1};
};

void to_json(nlohmann::json& j, const MsgExportFileRecv& v);
void from_json(const nlohmann::json& j, MsgExportFileRecv& v);

// Config/Log export response
struct MsgExportFileSend : public MsgSendHead {
    struct ResData {
        std::string fileName;
        std::string fileUrl;
        friend void to_json(nlohmann::json& j, const ResData& v);
        friend void from_json(const nlohmann::json& j, ResData& v);
    } resData;
};

void to_json(nlohmann::json& j, const MsgExportFileSend& v);
void from_json(const nlohmann::json& j, MsgExportFileSend& v);

struct MsgUpgradeRecv : public MsgRecvHead {
    std::string contentLength;
    std::string fileName;
    std::string filePath;
    std::string uploadId;
    std::string fileUrl;
};

void to_json(nlohmann::json& j, const MsgUpgradeRecv& v);
void from_json(const nlohmann::json& j, MsgUpgradeRecv& v);

//
struct MsgUpgradeSend : public MsgSendHead {};

struct MsgCheckUpgradeSpaceRecv : public MsgRecvHead {
    std::uint64_t packageSizeBytes{0};
    bool cleanupEventMedia{false};
};

void to_json(nlohmann::json& j, const MsgCheckUpgradeSpaceRecv& v);
void from_json(const nlohmann::json& j, MsgCheckUpgradeSpaceRecv& v);

struct MsgCheckUpgradeSpaceSend : public MsgSendHead {
    struct ResData {
        bool sufficient{false};
        std::uint64_t requiredBytes{0};
        std::uint64_t availableBytes{0};
        std::uint64_t eventMediaBytes{0};
        std::uint64_t deletedMediaBytes{0};
        std::uint64_t deletedMediaFiles{0};
        friend void to_json(nlohmann::json& j, const ResData& v);
        friend void from_json(const nlohmann::json& j, ResData& v);
    } resData;
};

void to_json(nlohmann::json& j, const MsgCheckUpgradeSpaceSend& v);
void from_json(const nlohmann::json& j, MsgCheckUpgradeSpaceSend& v);

struct MsgQueryModelAuthorizationRecv : public MsgRecvHead {};
struct MsgQueryModelAuthorizationSend : public MsgSendHead {
    struct ResData {
        bool supported{false};
        bool authorized{false};
        std::string state;
        friend void to_json(nlohmann::json& j, const ResData& v);
        friend void from_json(const nlohmann::json& j, ResData& v);
    } resData;
};
void to_json(nlohmann::json& j, const MsgQueryModelAuthorizationSend& v);
void from_json(const nlohmann::json& j, MsgQueryModelAuthorizationSend& v);

struct MsgDownloadModelAuthorizationRequestRecv : public MsgRecvHead {};
struct MsgDownloadModelAuthorizationRequestSend : public MsgSendHead {
    std::string filePath;
    std::string fileName;
};
void to_json(nlohmann::json& j, const MsgDownloadModelAuthorizationRequestSend& v);
void from_json(const nlohmann::json& j, MsgDownloadModelAuthorizationRequestSend& v);

struct MsgDownloadLogsRecv : public MsgRecvHead {};
struct MsgDownloadLogsSend : public MsgSendHead {
    std::string filePath;
    std::string fileName;
};
void to_json(nlohmann::json& j, const MsgDownloadLogsSend& v);
void from_json(const nlohmann::json& j, MsgDownloadLogsSend& v);

struct MsgInstallModelAuthorizationRecv : public MsgRecvHead {
    std::string uploadId;
    std::string filePath;
};
void to_json(nlohmann::json& j, const MsgInstallModelAuthorizationRecv& v);
void from_json(const nlohmann::json& j, MsgInstallModelAuthorizationRecv& v);
struct MsgInstallModelAuthorizationSend : public MsgSendHead {};

// Document download address request
struct MsgQueryDocumentUrlRecv : public MsgRecvHead {
    int type{0};
};

void to_json(nlohmann::json& j, const MsgQueryDocumentUrlRecv& v);
void from_json(const nlohmann::json& j, MsgQueryDocumentUrlRecv& v);

// Document download address response
struct MsgQueryDocumentUrlSend : public MsgSendHead {
    struct ResData {
        std::string url;
        friend void to_json(nlohmann::json& j, const ResData& v);
        friend void from_json(const nlohmann::json& j, ResData& v);
    } resData;
};

void to_json(nlohmann::json& j, const MsgQueryDocumentUrlSend& v);
void from_json(const nlohmann::json& j, MsgQueryDocumentUrlSend& v);

}  // namespace cosmo::System
