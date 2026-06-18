// ThingsLib DTO definitions (extracted from MessageThingsLibHandler.h)

#pragma once
#include <system_error>

#include "service/face/dto/ThingsLibMsgTypes.h"
#include "util/dto/CameraMsgTypes.h"
#include "util/dto/EventMsgTypes.h"
#include "util/dto/ServerMsgTypes.h"

namespace cosmo {
namespace ThingsLib {
    struct MsgModifyThingsLibRecv : public MsgRecvHead {
        util::RangeInt<1, 2> thingsLibOperation{1};
        MsgBaseThingsLibInfo thingsLib;
    };

    void to_json(nlohmann::json& j, const MsgModifyThingsLibRecv& v);
    void from_json(const nlohmann::json& j, MsgModifyThingsLibRecv& v);
    struct MsgModifyThingsLibSend : public MsgSendHead {
        struct ResData {
            std::string thingsLibId;
            friend void to_json(nlohmann::json& j, const ResData& v);
            friend void from_json(const nlohmann::json& j, ResData& v);
        } resData;
    };

    void to_json(nlohmann::json& j, const MsgModifyThingsLibSend& v);
    void from_json(const nlohmann::json& j, MsgModifyThingsLibSend& v);
    struct MsgDeleteThingsLibRecv : public MsgRecvHead {
        std::vector<util::String<0, 36>> thingsLibIdList;
    };

    void to_json(nlohmann::json& j, const MsgDeleteThingsLibRecv& v);
    void from_json(const nlohmann::json& j, MsgDeleteThingsLibRecv& v);
    struct MsgDeleteThingsLibSend : public MsgSendHead {
        struct ResData {
            std::vector<MsgResultThingsLibInfo> failedThingsLibList;
            friend void to_json(nlohmann::json& j, const ResData& v);
            friend void from_json(const nlohmann::json& j, ResData& v);
        } resData;
    };

    void to_json(nlohmann::json& j, const MsgDeleteThingsLibSend& v);
    void from_json(const nlohmann::json& j, MsgDeleteThingsLibSend& v);
    struct MsgQueryThingsLibInfoRecv : public MsgRecvHead, public MsgQueryThingsLibInfoR {};

    void to_json(nlohmann::json& j, const MsgQueryThingsLibInfoRecv& v);
    void from_json(const nlohmann::json& j, MsgQueryThingsLibInfoRecv& v);

    struct MsgQueryThingsLibInfoSend : public MsgSendHead, public MsgQueryThingsLibInfoS {};

    void to_json(nlohmann::json& j, const MsgQueryThingsLibInfoSend& v);
    void from_json(const nlohmann::json& j, MsgQueryThingsLibInfoSend& v);

    struct MsgQueryThingsPicturesRecv : public MsgRecvHead, public MsgQueryThingsPicturesR {};

    void to_json(nlohmann::json& j, const MsgQueryThingsPicturesRecv& v);
    void from_json(const nlohmann::json& j, MsgQueryThingsPicturesRecv& v);

    struct MsgTaskAreaRect {
        util::RangeValue<double> x{0.0, 1.0, -1.0};
        util::RangeValue<double> y{0.0, 1.0, -1.0};
        util::RangeValue<double> w{0.0, 1.0, -1.0};
        util::RangeValue<double> h{0.0, 1.0, -1.0};
        friend void to_json(nlohmann::json& j, const MsgTaskAreaRect& v);
        friend void from_json(const nlohmann::json& j, MsgTaskAreaRect& v);
    };
    struct MsgQueryThingsPicturesSend : public MsgSendHead, public MsgQueryThingsPicturesS {};

    void to_json(nlohmann::json& j, const MsgQueryThingsPicturesSend& v);
    void from_json(const nlohmann::json& j, MsgQueryThingsPicturesSend& v);
    struct MsgGetThingsPictureRecv : public MsgRecvHead {
        util::String<0, 36> cameraId;
        MsgTaskAreaRect rect;
        util::RangeInt<1, 207> taskType{148};
    };

    void to_json(nlohmann::json& j, const MsgGetThingsPictureRecv& v);
    void from_json(const nlohmann::json& j, MsgGetThingsPictureRecv& v);
    struct MsgGetThingsPictureSend : public MsgSendHead {
        struct ResData {
            std::string pictureUrl;
            friend void to_json(nlohmann::json& j, const ResData& v);
            friend void from_json(const nlohmann::json& j, ResData& v);
        } resData;
    };

    void to_json(nlohmann::json& j, const MsgGetThingsPictureSend& v);
    void from_json(const nlohmann::json& j, MsgGetThingsPictureSend& v);
    struct MsgAddLibThingsRecv : public MsgRecvHead {
        struct ArticlesReid {
            util::String<0, 256> pictureUrl;
            std::string pictureBase64;
            util::String<0, 32> pictureName;
            friend void to_json(nlohmann::json& j, const ArticlesReid& v);
            friend void from_json(const nlohmann::json& j, ArticlesReid& v);
        };

        util::RangeInt<1, 2> thingsOperation{0};
        util::String<0, 36> thingsLibId;
        std::vector<ArticlesReid> thingsList;
    };

    void to_json(nlohmann::json& j, const MsgAddLibThingsRecv& v);
    void from_json(const nlohmann::json& j, MsgAddLibThingsRecv& v);
    struct MsgAddLibThingsSend : public MsgSendHead {
        struct ResData {
            std::vector<std::string> thingsId;
            friend void to_json(nlohmann::json& j, const ResData& v);
            friend void from_json(const nlohmann::json& j, ResData& v);
        } resData;
    };

    void to_json(nlohmann::json& j, const MsgAddLibThingsSend& v);
    void from_json(const nlohmann::json& j, MsgAddLibThingsSend& v);
    struct MsgBindTaskThingsLibRecv : public MsgRecvHead, public MsgBaseCameraTask {
        int searchAll{1};
        std::vector<util::String<0, 36>> thingsLibId;
    };

    void to_json(nlohmann::json& j, const MsgBindTaskThingsLibRecv& v);
    void from_json(const nlohmann::json& j, MsgBindTaskThingsLibRecv& v);
    struct MsgBindTaskThingsLibSend : public MsgSendHead {
        struct ResData {
            std::vector<MsgResultThingsLibInfo> failedThingsLibList;
            friend void to_json(nlohmann::json& j, const ResData& v);
            friend void from_json(const nlohmann::json& j, ResData& v);
        } resData;
    };

    void to_json(nlohmann::json& j, const MsgBindTaskThingsLibSend& v);
    void from_json(const nlohmann::json& j, MsgBindTaskThingsLibSend& v);
    struct MsgDeleteLibThingsRecv : public MsgRecvHead {
        int removeAll{0};
        util::String<0, 36> thingsLibId;
        std::vector<std::string> thingsIdList;
    };

    void to_json(nlohmann::json& j, const MsgDeleteLibThingsRecv& v);
    void from_json(const nlohmann::json& j, MsgDeleteLibThingsRecv& v);
    struct MsgDeleteLibThingsSend : public MsgSendHead {
        std::vector<MsgResultInfo> failedList;
    };

    void to_json(nlohmann::json& j, const MsgDeleteLibThingsSend& v);
    void from_json(const nlohmann::json& j, MsgDeleteLibThingsSend& v);
}  // namespace ThingsLib
}  // namespace cosmo
