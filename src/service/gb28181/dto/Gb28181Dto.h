#pragma once
#include <nlohmann/json.hpp>

#include "util/MsgBaseTypes.h"

namespace cosmo::camera {
struct MsgGb28181ManageRecv : MsgRecvHead {
    nlohmann::json input;
};
struct MsgGb28181ManageSend : MsgSendHead {
    nlohmann::json resData;
};
void from_json(const nlohmann::json& j, MsgGb28181ManageRecv& value);
void to_json(nlohmann::json& j, const MsgGb28181ManageRecv& value);
void to_json(nlohmann::json& j, const MsgGb28181ManageSend& value);
}  // namespace cosmo::camera
