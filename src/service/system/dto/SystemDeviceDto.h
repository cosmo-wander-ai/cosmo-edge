#pragma once

#include <system_error>

#include "service/system/dto/SystemMsgTypes.h"
#include "util/dto/ServerMsgTypes.h"

namespace cosmo::System {

struct MsgQueryDeviceInfoRecv : public MsgRecvHead {};

struct MsgQueryDeviceInfoSend : public MsgSendHead {
    struct ResData {
        std::vector<DeviceInfo> devInfoList;
        friend void to_json(nlohmann::json& j, const ResData& v);
        friend void from_json(const nlohmann::json& j, ResData& v);
    } resData;
};

void to_json(nlohmann::json& j, const MsgQueryDeviceInfoSend& v);
void from_json(const nlohmann::json& j, MsgQueryDeviceInfoSend& v);

struct MsgQueryHardwareResourceRecv : public MsgRecvHead {};

struct MsgQueryHardwareResourceSend : public MsgSendHead {
    struct Item {
        std::string key;
        std::string name;
        int usedPercent{0};
        std::string usedSize;
        std::string unusedSize;
        int available{0};
        friend void to_json(nlohmann::json& j, const Item& v);
        friend void from_json(const nlohmann::json& j, Item& v);
    };

    struct ResData {
        std::string customScore;
        std::vector<Item> itemList;
        friend void to_json(nlohmann::json& j, const ResData& v);
        friend void from_json(const nlohmann::json& j, ResData& v);
    } resData;
};

void to_json(nlohmann::json& j, const MsgQueryHardwareResourceSend& v);
void from_json(const nlohmann::json& j, MsgQueryHardwareResourceSend& v);

// Query device info status request
struct MsgQueryDeviceStatusRecv : MsgRecvHead {};

// Query device info status response
struct MsgQueryDeviceStatusSend : public MsgSendHead {};

}  // namespace cosmo::System
