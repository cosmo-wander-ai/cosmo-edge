// BodyLib DTO definitions (extracted from MessageBodyLibHandler.h)

#pragma once
#include <system_error>

#include "service/face/dto/PersonMsgTypes.h"
#include "util/dto/CameraMsgTypes.h"
#include "util/dto/EventMsgTypes.h"
#include "util/dto/ServerMsgTypes.h"

namespace cosmo {
namespace BodyLib {
    struct MsgModifyPersonLibRecv : public MsgRecvHead {
        util::RangeInt<1, 2> personLibOperation{1};
        MsgBasePersonLibInfo personLib;
    };

    void to_json(nlohmann::json& j, const MsgModifyPersonLibRecv& v);
    void from_json(const nlohmann::json& j, MsgModifyPersonLibRecv& v);
    struct MsgModifyPersonLibSend : public MsgSendHead {
        struct ResData {
            std::string personLibId;
            friend void to_json(nlohmann::json& j, const ResData& v);
            friend void from_json(const nlohmann::json& j, ResData& v);
        } resData;
    };

    void to_json(nlohmann::json& j, const MsgModifyPersonLibSend& v);
    void from_json(const nlohmann::json& j, MsgModifyPersonLibSend& v);
    struct MsgAddLibPersonRecv : public MsgRecvHead {
        struct Person {
            util::String<0, 256> pictureUrl;
            util::String<0, 32> pictureName;
            friend void to_json(nlohmann::json& j, const Person& v);
            friend void from_json(const nlohmann::json& j, Person& v);
        };

        util::RangeInt<1, 2> personOperation{1};
        util::String<0, 36> personLibId;
        std::vector<Person> personList;
    };

    void to_json(nlohmann::json& j, const MsgAddLibPersonRecv& v);
    void from_json(const nlohmann::json& j, MsgAddLibPersonRecv& v);
    struct MsgDeletePersonLibRecv : public MsgRecvHead {
        std::vector<util::String<0, 36>> personLibIdList;
    };

    void to_json(nlohmann::json& j, const MsgDeletePersonLibRecv& v);
    void from_json(const nlohmann::json& j, MsgDeletePersonLibRecv& v);
    struct MsgDeletePersonLibSend : public MsgSendHead {
        struct ResData {
            std::vector<MsgResultPersonLibInfo> failedPersonLibList;
            friend void to_json(nlohmann::json& j, const ResData& v);
            friend void from_json(const nlohmann::json& j, ResData& v);
        } resData;
    };

    void to_json(nlohmann::json& j, const MsgDeletePersonLibSend& v);
    void from_json(const nlohmann::json& j, MsgDeletePersonLibSend& v);
    struct MsgAddLibPersonSend : public MsgSendHead {
        struct ResData {
            std::vector<std::string> personId;
            friend void to_json(nlohmann::json& j, const ResData& v);
            friend void from_json(const nlohmann::json& j, ResData& v);
        } resData;
    };

    void to_json(nlohmann::json& j, const MsgAddLibPersonSend& v);
    void from_json(const nlohmann::json& j, MsgAddLibPersonSend& v);
    struct MsgQueryPersonLibInfoRecv : public MsgRecvHead, public MsgQueryPersonInfoRecv {};

    void to_json(nlohmann::json& j, const MsgQueryPersonLibInfoRecv& v);
    void from_json(const nlohmann::json& j, MsgQueryPersonLibInfoRecv& v);

    struct MsgQueryPersonLibInfoSend : public MsgSendHead, public MsgQueryPersonLibInfoS {};

    void to_json(nlohmann::json& j, const MsgQueryPersonLibInfoSend& v);
    void from_json(const nlohmann::json& j, MsgQueryPersonLibInfoSend& v);
    struct MsgBindTaskPersonLibSend : public MsgSendHead {
        struct ResData {
            std::vector<MsgResultPersonLibInfo> failedPersonLibList;
            friend void to_json(nlohmann::json& j, const ResData& v);
            friend void from_json(const nlohmann::json& j, ResData& v);
        } resData;
    };

    void to_json(nlohmann::json& j, const MsgBindTaskPersonLibSend& v);
    void from_json(const nlohmann::json& j, MsgBindTaskPersonLibSend& v);
    struct MsgBindTaskPersonLibRecv : public MsgRecvHead, public MsgBaseCameraTask {
        int searchAll{1};
        std::vector<util::String<0, 36>> personLibId;
    };

    void to_json(nlohmann::json& j, const MsgBindTaskPersonLibRecv& v);
    void from_json(const nlohmann::json& j, MsgBindTaskPersonLibRecv& v);
    struct MsgDeleteLibPersonRecv : public MsgRecvHead {
        int removeAll{0};
        util::String<0, 36> personLibId;
        std::vector<std::string> personIdList;
    };

    void to_json(nlohmann::json& j, const MsgDeleteLibPersonRecv& v);
    void from_json(const nlohmann::json& j, MsgDeleteLibPersonRecv& v);
    struct MsgDeleteLibPersonSend : public MsgSendHead {
        std::vector<MsgResultInfo> failedList;
    };

    void to_json(nlohmann::json& j, const MsgDeleteLibPersonSend& v);
    void from_json(const nlohmann::json& j, MsgDeleteLibPersonSend& v);
    struct MsgDetectPersonRecv : public MsgRecvHead {
        std::string imageBase64;
    };

    void to_json(nlohmann::json& j, const MsgDetectPersonRecv& v);
    void from_json(const nlohmann::json& j, MsgDetectPersonRecv& v);
    struct MsgDetectPersonSend : public MsgSendHead {
        struct ResData {
            std::string pictureUrl;
            friend void to_json(nlohmann::json& j, const ResData& v);
            friend void from_json(const nlohmann::json& j, ResData& v);
        } resData;
    };

    void to_json(nlohmann::json& j, const MsgDetectPersonSend& v);
    void from_json(const nlohmann::json& j, MsgDetectPersonSend& v);
    struct MsgGetPersonPictureRecv : public MsgRecvHead {
        util::String<0, 36> cameraId;
    };

    void to_json(nlohmann::json& j, const MsgGetPersonPictureRecv& v);
    void from_json(const nlohmann::json& j, MsgGetPersonPictureRecv& v);
    struct MsgGetPersonPictureSend : public MsgSendHead {
        struct ResData {
            std::string pictureUrl;
            friend void to_json(nlohmann::json& j, const ResData& v);
            friend void from_json(const nlohmann::json& j, ResData& v);
        } resData;
    };

    void to_json(nlohmann::json& j, const MsgGetPersonPictureSend& v);
    void from_json(const nlohmann::json& j, MsgGetPersonPictureSend& v);
    struct MsgQueryPersonPicturesRecv : public MsgRecvHead, public MsgQueryPersonPicturesR {};

    void to_json(nlohmann::json& j, const MsgQueryPersonPicturesRecv& v);
    void from_json(const nlohmann::json& j, MsgQueryPersonPicturesRecv& v);

    struct MsgQueryPersonPicturesSend : public MsgSendHead, public MsgQueryPersonPicturesS {};

    void to_json(nlohmann::json& j, const MsgQueryPersonPicturesSend& v);
    void from_json(const nlohmann::json& j, MsgQueryPersonPicturesSend& v);
}  // namespace BodyLib
}  // namespace cosmo
