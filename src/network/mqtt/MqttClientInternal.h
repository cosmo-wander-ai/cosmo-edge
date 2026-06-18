// Internal declarations shared between CMvMQTTClient.cc, MvMQTTClientCallback.cc,
// and MvMQTTClientUtil.cc. Not part of the public API.
#pragma once

#include <atomic>
#include <chrono>
#include <map>
#include <mutex>
#include <string>

#include "MQTTClient.h"
#include "network/mqtt/MqttClient.h"
#include "network/mqtt/MqttTypes.h"
#include "util/Log.h"

namespace cosmo::network::mqtt {

// Constants
inline constexpr int kMsgReceived    = 1;
inline constexpr int kTimeoutZero    = 0;
inline constexpr int kTimeoutDefault = 5000;
inline constexpr int kMd5SaltRange   = 1000;

// Paho C callback declarations
void MQTTClient_ConnectionLost_CB(void* context, char* cause);
int MQTTClient_MessageArrived_CB(void* context, char* topicName, int topicLen, MQTTClient_message* m);
void MQTTClient_DeliveryComplete_CB(void* context, MQTTClient_deliveryToken dt);

// Utility function declarations
int ValidateConnectParams(const MqttConnectOptions& opts);
void InitConnectOptions(MqttConnectOptions& opts, void* paho_opts);
std::string GeneratePasswordByMD5(const MqttConnectOptions& opts);
void Md5Encode(const std::string& input, std::string& output);

}  // namespace cosmo::network::mqtt
