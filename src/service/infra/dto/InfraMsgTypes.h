// Infrastructure msg types — MsgNetCardInfo, StorageList

#pragma once

#include "util/MsgBaseTypes.h"

namespace cosmo {

// Search tool types  ==========================================================
struct MsgNetCardInfo {
    int mainCard{1};
    int dhcp{0};
    std::string ethName;
    std::string ipAddr;
    std::string netMask;
    std::string gateway;
    std::string mac;
    std::string dns1;
    std::string dns2;
    friend void to_json(nlohmann::json& j, const MsgNetCardInfo& v);
    friend void from_json(const nlohmann::json& j, MsgNetCardInfo& v);
};

struct StorageList {
    std::string id;
    std::string businessCategory;
    std::string actionName;
    std::string remark;
    std::string inputParamConfig;
    friend void to_json(nlohmann::json& j, const StorageList& v);
    friend void from_json(const nlohmann::json& j, StorageList& v);
};

}  // namespace cosmo
