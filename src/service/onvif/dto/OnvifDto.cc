#include "service/onvif/dto/OnvifDto.h"

namespace cosmo::camera {
void to_json(nlohmann::json& j, const OnvifResponse& v) {
    to_json(j, static_cast<const MsgSendHead&>(v));
    j["resData"] = v.resData;
}
#define ONVIF_REQUEST_JSON(Name)                                                                             \
    void from_json(const nlohmann::json& j, Msg##Name##Recv& v) {                                            \
        from_json(j, static_cast<MsgRecvHead&>(v));                                                          \
        v.input = j;                                                                                         \
    }                                                                                                        \
    void to_json(nlohmann::json& j, const Msg##Name##Recv& v) {                                              \
        to_json(j, static_cast<const MsgRecvHead&>(v));                                                      \
    }
ONVIF_REQUEST_JSON(OnvifInterfaces)
ONVIF_REQUEST_JSON(OnvifDiscover)
ONVIF_REQUEST_JSON(OnvifProbe)
ONVIF_REQUEST_JSON(OnvifGet)
ONVIF_REQUEST_JSON(OnvifSave)
#undef ONVIF_REQUEST_JSON
}  // namespace cosmo::camera
