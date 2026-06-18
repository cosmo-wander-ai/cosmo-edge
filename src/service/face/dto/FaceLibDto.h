#pragma once

// FaceLib DTO definitions (extracted from MessageFaceLibHandler.h)

#include <system_error>

#include "util/dto/EventMsgTypes.h"
#include "util/dto/ServerMsgTypes.h"

namespace cosmo {
namespace Lib {
    struct MsgModifyFaceLibSend : public MsgSendHead {
        struct ResData {
            std::string faceLibId;
            friend void to_json(nlohmann::json& j, const ResData& v);
            friend void from_json(const nlohmann::json& j, ResData& v);
        } resData;
    };

    void to_json(nlohmann::json& j, const MsgModifyFaceLibSend& v);
    void from_json(const nlohmann::json& j, MsgModifyFaceLibSend& v);
    struct MsgModifyFaceLibRecv : public MsgRecvHead {
        util::RangeInt<1, 2> faceLibOperation{1};
        MsgBaseFaceLibInfo faceLib;
    };

    void to_json(nlohmann::json& j, const MsgModifyFaceLibRecv& v);
    void from_json(const nlohmann::json& j, MsgModifyFaceLibRecv& v);
    struct MsgModifyFacePicLibSend : public MsgSendHead {
        struct ResData {
            std::string personId;
            friend void to_json(nlohmann::json& j, const ResData& v);
            friend void from_json(const nlohmann::json& j, ResData& v);
        } resData;
    };

    void to_json(nlohmann::json& j, const MsgModifyFacePicLibSend& v);
    void from_json(const nlohmann::json& j, MsgModifyFacePicLibSend& v);
    struct MsgModifyFacePicLibRecv : public MsgRecvHead, public MsgConditionLib {};

    void to_json(nlohmann::json& j, const MsgModifyFacePicLibRecv& v);
    void from_json(const nlohmann::json& j, MsgModifyFacePicLibRecv& v);
    struct MsgModifyTaskAreaSend : public MsgSendHead {
        struct ResData {
            std::vector<std::string> areaList;
            std::vector<std::string> invalidAreaList;
            friend void to_json(nlohmann::json& j, const ResData& v);
            friend void from_json(const nlohmann::json& j, ResData& v);
        } resData;
    };

    void to_json(nlohmann::json& j, const MsgModifyTaskAreaSend& v);
    void from_json(const nlohmann::json& j, MsgModifyTaskAreaSend& v);
    struct MsgQueryFaceLibInfoSend : public MsgSendHead, public MsgQueryFaceLibInfoS {};

    void to_json(nlohmann::json& j, const MsgQueryFaceLibInfoSend& v);
    void from_json(const nlohmann::json& j, MsgQueryFaceLibInfoSend& v);
    struct MsgQueryFaceLibInfoRecv : public MsgRecvHead, public MsgQueryFaceLibInfoR {};

    void to_json(nlohmann::json& j, const MsgQueryFaceLibInfoRecv& v);
    void from_json(const nlohmann::json& j, MsgQueryFaceLibInfoRecv& v);
    struct MsgDeleteFaceLibSend : public MsgSendHead {
        struct ResData {
            std::vector<MsgResultFaceLibInfo> failedFaceLibList;
            friend void to_json(nlohmann::json& j, const ResData& v);
            friend void from_json(const nlohmann::json& j, ResData& v);
        } resData;
    };

    void to_json(nlohmann::json& j, const MsgDeleteFaceLibSend& v);
    void from_json(const nlohmann::json& j, MsgDeleteFaceLibSend& v);
    struct MsgDeleteFaceLibRecv : public MsgRecvHead {
        std::vector<util::String<0, 36>> faceLibIdList;
    };

    void to_json(nlohmann::json& j, const MsgDeleteFaceLibRecv& v);
    void from_json(const nlohmann::json& j, MsgDeleteFaceLibRecv& v);
    struct MsgQueryFacesRecv : public MsgRecvHead, public MsgQueryFacesR {};

    void to_json(nlohmann::json& j, const MsgQueryFacesRecv& v);
    void from_json(const nlohmann::json& j, MsgQueryFacesRecv& v);

    struct MsgQueryFacesSend : public MsgSendHead, public MsgQueryFacesS {};

    void to_json(nlohmann::json& j, const MsgQueryFacesSend& v);
    void from_json(const nlohmann::json& j, MsgQueryFacesSend& v);
    struct MsgDeletePersonRecv : public MsgRecvHead {
        int removeAll{0};
        util::String<0, 36> faceLibId;
        std::vector<std::string> personIdList;
    };

    void to_json(nlohmann::json& j, const MsgDeletePersonRecv& v);
    void from_json(const nlohmann::json& j, MsgDeletePersonRecv& v);
    struct MsgDeletePersonSend : public MsgSendHead {
        std::vector<MsgResultInfo> failedList;
    };

    void to_json(nlohmann::json& j, const MsgDeletePersonSend& v);
    void from_json(const nlohmann::json& j, MsgDeletePersonSend& v);
}  // namespace Lib
}  // namespace cosmo
