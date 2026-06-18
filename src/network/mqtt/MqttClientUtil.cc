// MQTT client utility functions — parameter validation, password generation, crypto.

#include <openssl/evp.h>

#include "network/mqtt/MqttClientInternal.h"

namespace cosmo::network::mqtt {

// Validate MQTT connection parameters.
int ValidateConnectParams(const MqttConnectOptions& opts) {
    if (opts.server_uri.empty()) {
        return static_cast<int>(MqttErrorCode::kServerUriEmpty);
    }
    if (opts.client_id.empty()) {
        return static_cast<int>(MqttErrorCode::kClientIdEmpty);
    }
    if (opts.auth_type < MqttAuthType::kMvIot || opts.auth_type > MqttAuthType::kAnonymous) {
        return static_cast<int>(MqttErrorCode::kAuthTypeInvalid);
    }
    if (opts.auth_type == MqttAuthType::kMvIot || opts.auth_type == MqttAuthType::kNormal) {
        if (opts.username.empty()) {
            return static_cast<int>(MqttErrorCode::kUsernameEmpty);
        }
        if (opts.auth_type == MqttAuthType::kNormal) {
            if (opts.password.empty()) {
                return static_cast<int>(MqttErrorCode::kPasswordEmpty);
            }
        }
        if (opts.auth_type == MqttAuthType::kMvIot) {
            if (opts.auth_key.empty()) {
                return static_cast<int>(MqttErrorCode::kAuthKeyEmpty);
            }
            if (opts.device_type.empty()) {
                return static_cast<int>(MqttErrorCode::kAuthDeviceTypeEmpty);
            }
            if (opts.device_sn.empty()) {
                return static_cast<int>(MqttErrorCode::kAuthDeviceSnEmpty);
            }
        }
    }
    return static_cast<int>(MqttErrorCode::kSuccess);
}

// Initialize paho MQTTClient_connectOptions from user-supplied parameters.
void InitConnectOptions(MqttConnectOptions& opts, void* paho_opts) {
    if (nullptr == paho_opts)
        return;

    auto* conn = static_cast<MQTTClient_connectOptions*>(paho_opts);
    if (opts.keep_alive_interval_s > 0) {
        conn->keepAliveInterval = opts.keep_alive_interval_s;
    }

    if (opts.auth_type == MqttAuthType::kNormal || opts.auth_type == MqttAuthType::kMvIot) {
        conn->username = opts.username.c_str();
        if (opts.auth_type == MqttAuthType::kNormal) {
            conn->password = opts.password.c_str();
        }
        if (opts.auth_type == MqttAuthType::kMvIot) {
            opts.password  = GeneratePasswordByMD5(opts);
            conn->password = opts.password.c_str();
        }
    }

    if (opts.max_inflight_msgs > 0) {
        conn->maxInflightMessages = opts.max_inflight_msgs;
    }

    if (opts.connect_timeout_s > 0) {
        conn->connectTimeout = opts.connect_timeout_s;
    }
}

// Generate CWAI IoT authentication password (MD5-based).
std::string GeneratePasswordByMD5(const MqttConnectOptions& opts) {
    std::string result;
    std::string temp = "aibox::" + opts.device_sn + "::cwai";
    Md5Encode(temp, result);
    return result;
}

// MD5 encoding using EVP API.
void Md5Encode(const std::string& input, std::string& output) {
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digest_len = 0;

    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (ctx) {
        EVP_DigestInit_ex(ctx, EVP_md5(), nullptr);
        EVP_DigestUpdate(ctx, input.c_str(), input.length());
        EVP_DigestFinal_ex(ctx, digest, &digest_len);
        EVP_MD_CTX_free(ctx);
    }

    char hex_string[64];
    for (unsigned int i = 0; i < digest_len; i++) {
        snprintf(&hex_string[i * 2], sizeof(hex_string) - i * 2, "%02x",
                 static_cast<unsigned int>(digest[i]));
    }
    output.assign(hex_string);
}

}  // namespace cosmo::network::mqtt
