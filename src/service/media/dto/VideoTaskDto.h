// VideoTask DTO definitions (extracted from MessageVideoTaskHandler.h)

#pragma once

#include <system_error>

#include "service/task/dto/StatusMsgTypes.h"
#include "util/dto/EventMsgTypes.h"
#include "util/dto/ServerMsgTypes.h"
#include "util/dto/TaskCreateTypes.h"

namespace cosmo {
namespace VideoTask {
    struct MsgChannelTask {
        std::string id;
        std::string channelId;
        std::string algorithmId;
    };

    void to_json(nlohmann::json& j, const MsgChannelTask& v);
    void from_json(const nlohmann::json& j, MsgChannelTask& v);

    struct MsgModifyParamRecv : public MsgRecvHead, public MsgChannelTask {
        MsgTaskConfig taskConfig;
        std::string scheduleId;
    };

    void to_json(nlohmann::json& j, const MsgModifyParamRecv& v);
    void from_json(const nlohmann::json& j, MsgModifyParamRecv& v);

    //
    struct MsgModifyParamSend : public MsgSendHead {};

    struct MsgQueryParamRecv : public MsgRecvHead, public MsgChannelTask {};

    void to_json(nlohmann::json& j, const MsgQueryParamRecv& v);
    void from_json(const nlohmann::json& j, MsgQueryParamRecv& v);

    //
    struct MsgQueryParamSend : public MsgSendHead {
        struct ResData {
            std::vector<MsgDynamicKeyValue> params;
            friend void to_json(nlohmann::json& j, const ResData& v);
            friend void from_json(const nlohmann::json& j, ResData& v);
        } resData;
    };

    void to_json(nlohmann::json& j, const MsgQueryParamSend& v);
    void from_json(const nlohmann::json& j, MsgQueryParamSend& v);

    struct MsgModifyAreaRecv : public MsgRecvHead, public MsgChannelTask {
        std::vector<MsgTaskArea> areas;
        std::vector<MsgTaskArea> shieldedAreas;
    };

    void to_json(nlohmann::json& j, const MsgModifyAreaRecv& v);
    void from_json(const nlohmann::json& j, MsgModifyAreaRecv& v);

    //
    struct MsgModifyAreaSend : public MsgSendHead {};

    struct MsgQueryAreaRecv : public MsgRecvHead, public MsgChannelTask {};

    void to_json(nlohmann::json& j, const MsgQueryAreaRecv& v);
    void from_json(const nlohmann::json& j, MsgQueryAreaRecv& v);

    //
    struct MsgQueryAreaSend : public MsgSendHead {
        struct ResData {
            std::vector<MsgTaskArea> areas;
            std::vector<MsgTaskArea> shieldedAreas;
            friend void to_json(nlohmann::json& j, const ResData& v);
            friend void from_json(const nlohmann::json& j, ResData& v);
        } resData;
    };

    void to_json(nlohmann::json& j, const MsgQueryAreaSend& v);
    void from_json(const nlohmann::json& j, MsgQueryAreaSend& v);

    struct MsgModifyStrategyRecv : public MsgRecvHead, public MsgChannelTask {
        int videoRepeatCount{1};
        std::string scheduleId;
    };

    void to_json(nlohmann::json& j, const MsgModifyStrategyRecv& v);
    void from_json(const nlohmann::json& j, MsgModifyStrategyRecv& v);

    //
    struct MsgModifyStrategySend : public MsgSendHead {};

    struct MsgQueryStrategyRecv : public MsgRecvHead, public MsgChannelTask {};

    void to_json(nlohmann::json& j, const MsgQueryStrategyRecv& v);
    void from_json(const nlohmann::json& j, MsgQueryStrategyRecv& v);

    //
    struct MsgQueryStrategySend : public MsgSendHead {
        struct ResData {
            int videoRepeatCount{1};
            std::string scheduleId;
            friend void to_json(nlohmann::json& j, const ResData& v);
            friend void from_json(const nlohmann::json& j, ResData& v);
        } resData;
    };

    void to_json(nlohmann::json& j, const MsgQueryStrategySend& v);
    void from_json(const nlohmann::json& j, MsgQueryStrategySend& v);

    struct MsgSwitchTaskRecv : public MsgRecvHead, public MsgChannelTask {
        int enable{0};
    };

    void to_json(nlohmann::json& j, const MsgSwitchTaskRecv& v);
    void from_json(const nlohmann::json& j, MsgSwitchTaskRecv& v);

    //
    struct MsgSwitchTaskSend : public MsgSendHead {};

    struct MsgTaskSwitch : public MsgChannelTask {
        int enable{0};
    };

    void to_json(nlohmann::json& j, const MsgTaskSwitch& v);
    void from_json(const nlohmann::json& j, MsgTaskSwitch& v);

    struct MsgBatchSwitchTaskRecv : public MsgRecvHead {
        std::vector<MsgTaskSwitch> tasks;
    };

    void to_json(nlohmann::json& j, const MsgBatchSwitchTaskRecv& v);
    void from_json(const nlohmann::json& j, MsgBatchSwitchTaskRecv& v);

    //
    struct MsgBatchSwitchTaskSend : public MsgSendHead {
        struct ResData {
            std::vector<MsgResultInfo> failedList;
            friend void to_json(nlohmann::json& j, const ResData& v);
            friend void from_json(const nlohmann::json& j, ResData& v);
        } resData;
    };

    void to_json(nlohmann::json& j, const MsgBatchSwitchTaskSend& v);
    void from_json(const nlohmann::json& j, MsgBatchSwitchTaskSend& v);

    struct MsgQuerySwitchRecv : public MsgRecvHead, public MsgChannelTask {};

    void to_json(nlohmann::json& j, const MsgQuerySwitchRecv& v);
    void from_json(const nlohmann::json& j, MsgQuerySwitchRecv& v);

    // Switch query response inner data
    struct SwitchResData {
        int enable{0};
        friend void to_json(nlohmann::json& j, const SwitchResData& v);
        friend void from_json(const nlohmann::json& j, SwitchResData& v);
    };

    //
    struct MsgQuerySwitchSend : public MsgSendHead {
        SwitchResData resData;
    };

    void to_json(nlohmann::json& j, const MsgQuerySwitchSend& v);
    void from_json(const nlohmann::json& j, MsgQuerySwitchSend& v);

    /// Default 24/7 schedule template ID.
    inline constexpr std::string_view kDefaultScheduleId = "e89c6c6385e5454b35cde0d1653vg";

    struct MsgSaveOrUpdateRecv : public MsgRecvHead, public MsgChannelTask {
        MsgTaskConfig taskConfig;
        std::string scheduleId{std::string(kDefaultScheduleId)};  // Default 24/7
    };

    void to_json(nlohmann::json& j, const MsgSaveOrUpdateRecv& v);
    void from_json(const nlohmann::json& j, MsgSaveOrUpdateRecv& v);

    //
    struct MsgSaveOrUpdateSend : public MsgSendHead {};

    struct MsgSelectConfigByAlgorithmIdRecv : public MsgRecvHead, public MsgChannelTask {};

    void to_json(nlohmann::json& j, const MsgSelectConfigByAlgorithmIdRecv& v);
    void from_json(const nlohmann::json& j, MsgSelectConfigByAlgorithmIdRecv& v);

    //
    struct MsgSelectConfigByAlgorithmIdSend : public MsgSendHead {
        struct ResData {
            MsgTaskConfig taskConfig;
            std::string scheduleId;
            std::string algorithmMetadata;
            int taskStatus{0};        // 0-Stopped 1-Running
            int taskEnableStatus{0};  // Enable status
            friend void to_json(nlohmann::json& j, const ResData& v);
            friend void from_json(const nlohmann::json& j, ResData& v);
        } resData;
    };

    void to_json(nlohmann::json& j, const MsgSelectConfigByAlgorithmIdSend& v);
    void from_json(const nlohmann::json& j, MsgSelectConfigByAlgorithmIdSend& v);

    // Delete task
    struct MsgDeleteRecv : public MsgRecvHead, public MsgChannelTask {};

    void to_json(nlohmann::json& j, const MsgDeleteRecv& v);
    void from_json(const nlohmann::json& j, MsgDeleteRecv& v);

    struct MsgDeleteSend : public MsgSendHead {};

    // Delete task
    struct MsgBatchDeleteRecv : public MsgRecvHead {
        std::vector<MsgChannelTask> tasks;
    };

    void to_json(nlohmann::json& j, const MsgBatchDeleteRecv& v);
    void from_json(const nlohmann::json& j, MsgBatchDeleteRecv& v);

    struct MsgBatchDeleteSend : public MsgSendHead {
        struct ResData {
            std::vector<MsgResultInfo> failedList;
            friend void to_json(nlohmann::json& j, const ResData& v);
            friend void from_json(const nlohmann::json& j, ResData& v);
        } resData;
    };

    void to_json(nlohmann::json& j, const MsgBatchDeleteSend& v);
    void from_json(const nlohmann::json& j, MsgBatchDeleteSend& v);

    struct MsgSelectAllAlgorithmInfoRecv : public MsgRecvHead, public MsgChannelTask {};

    void to_json(nlohmann::json& j, const MsgSelectAllAlgorithmInfoRecv& v);
    void from_json(const nlohmann::json& j, MsgSelectAllAlgorithmInfoRecv& v);

    struct SelectAllAlgorithmInfoCategoryInfo {
        std::string algorithmId;
        std::string algorithmName;
        std::string blanceCount;
        int algorithmCode{0};
        int algorithmCategory{0};
        std::string algorithmCategoryName;
        int algorithmUsage{0};

        friend void to_json(nlohmann::json& j, const SelectAllAlgorithmInfoCategoryInfo& v);
        friend void from_json(const nlohmann::json& j, SelectAllAlgorithmInfoCategoryInfo& v);
    };

    struct SelectAllAlgorithmInfoCategory : public MsgSendHead {
        int algorithmCategory;
        std::string algorithmCategoryName;
        std::vector<SelectAllAlgorithmInfoCategoryInfo> simpleAlgorithmInfos;
        friend void to_json(nlohmann::json& j, const SelectAllAlgorithmInfoCategory& v);
        friend void from_json(const nlohmann::json& j, SelectAllAlgorithmInfoCategory& v);
    };

    struct MsgSelectAllAlgorithmInfoSend : public MsgSendHead {
        struct ResData {
            std::vector<std::string> algorithmIds;                      // Configured algorithm list
            std::vector<SelectAllAlgorithmInfoCategory> algorithmList;  // Configurable algorithm list
            friend void to_json(nlohmann::json& j, const ResData& v);
            friend void from_json(const nlohmann::json& j, ResData& v);
        } resData;
    };

    void to_json(nlohmann::json& j, const MsgSelectAllAlgorithmInfoSend& v);
    void from_json(const nlohmann::json& j, MsgSelectAllAlgorithmInfoSend& v);

    struct MsgListChannelRecv : public MsgRecvHead {
        std::string algorithmId;
    };

    void to_json(nlohmann::json& j, const MsgListChannelRecv& v);
    void from_json(const nlohmann::json& j, MsgListChannelRecv& v);

    struct MsgListChannelItem {
        std::string id;
        std::string channelId;
        std::string channelName;
        friend void to_json(nlohmann::json& j, const MsgListChannelItem& v);
        friend void from_json(const nlohmann::json& j, MsgListChannelItem& v);
    };

    struct MsgListChannelSend : public MsgSendHead {
        std::vector<MsgListChannelItem> resData;
    };

    void to_json(nlohmann::json& j, const MsgListChannelSend& v);
    void from_json(const nlohmann::json& j, MsgListChannelSend& v);

    struct MsgApplyParamsBatchRecv : public MsgSaveOrUpdateRecv {
        std::vector<std::string> targetChannelIds;
    };

    void to_json(nlohmann::json& j, const MsgApplyParamsBatchRecv& v);
    void from_json(const nlohmann::json& j, MsgApplyParamsBatchRecv& v);

    struct MsgApplyParamsBatchSend : public MsgSendHead {
        struct ResData {
            std::vector<MsgResultInfo> failedList;
            friend void to_json(nlohmann::json& j, const ResData& v);
            friend void from_json(const nlohmann::json& j, ResData& v);
        } resData;
    };

    void to_json(nlohmann::json& j, const MsgApplyParamsBatchSend& v);
    void from_json(const nlohmann::json& j, MsgApplyParamsBatchSend& v);

    struct MsgRunningDetailRecv : public MsgRecvHead {
        std::vector<std::string> tasks;
    };

    void to_json(nlohmann::json& j, const MsgRunningDetailRecv& v);
    void from_json(const nlohmann::json& j, MsgRunningDetailRecv& v);
    struct MsgRunningDetailSend : public MsgSendHead {
        struct ResData {
            std::vector<MsgTaskStatus> status;
            friend void to_json(nlohmann::json& j, const ResData& v);
            friend void from_json(const nlohmann::json& j, ResData& v);
        } resData;
    };

    void to_json(nlohmann::json& j, const MsgRunningDetailSend& v);
    void from_json(const nlohmann::json& j, MsgRunningDetailSend& v);
}  // namespace VideoTask
}  // namespace cosmo
