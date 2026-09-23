#include "service/gb28181/dto/Gb28181Dto.h"
namespace cosmo::camera {
void from_json(const nlohmann::json& j, MsgGb28181ManageRecv& value) {
    from_json(j, static_cast<MsgRecvHead&>(value));
    value.input = j;
}
void to_json(nlohmann::json& j, const MsgGb28181ManageRecv& value) {
    // Never serialize credentials into dispatch logs.
    to_json(j, static_cast<const MsgRecvHead&>(value));
}
void to_json(nlohmann::json& j, const MsgGb28181ManageSend& value) {
    to_json(j, static_cast<const MsgSendHead&>(value));
    j["resData"] = value.resData;
}
}  // namespace cosmo::camera
