#pragma once
#include <nlohmann/json.hpp>

#include "util/MsgBaseTypes.h"

namespace cosmo::camera {
struct OnvifRequest {
    nlohmann::json input;
};
struct MsgOnvifInterfacesRecv : MsgRecvHead, OnvifRequest {};
struct MsgOnvifDiscoverRecv : MsgRecvHead, OnvifRequest {};
struct MsgOnvifProbeRecv : MsgRecvHead, OnvifRequest {};
struct MsgOnvifGetRecv : MsgRecvHead, OnvifRequest {};
struct MsgOnvifSaveRecv : MsgRecvHead, OnvifRequest {};
struct OnvifResponse : MsgSendHead {
    nlohmann::json resData;
};
using MsgOnvifInterfacesSend = OnvifResponse;
using MsgOnvifDiscoverSend   = OnvifResponse;
using MsgOnvifProbeSend      = OnvifResponse;
using MsgOnvifGetSend        = OnvifResponse;
using MsgOnvifSaveSend       = OnvifResponse;
void to_json(nlohmann::json& j, const OnvifResponse& v);
#define ONVIF_REQUEST_JSON(Name)                                                                             \
    void from_json(const nlohmann::json& j, Msg##Name##Recv& v);                                             \
    void to_json(nlohmann::json& j, const Msg##Name##Recv& v);
ONVIF_REQUEST_JSON(OnvifInterfaces)
ONVIF_REQUEST_JSON(OnvifDiscover)
ONVIF_REQUEST_JSON(OnvifProbe)
ONVIF_REQUEST_JSON(OnvifGet)
ONVIF_REQUEST_JSON(OnvifSave)
#undef ONVIF_REQUEST_JSON
}  // namespace cosmo::camera
