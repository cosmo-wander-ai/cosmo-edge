#pragma once

#include <tinyxml2.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace cosmo::service::onvif {

struct Config {
    std::string endpoint;
    std::string username;
    std::string password;
    std::string profileToken;
    std::string videoSourceToken;
    std::string uuid;
    std::string rtspUsername;
    std::string rtspPassword;
    bool separateRtspCredentials{false};
};

std::string Escape(const std::string& text);
std::string NormalizeEndpoint(const std::string& endpoint);
std::vector<std::string> DiscoveryEndpoints(const std::string& advertised, const std::string& sender);
std::string AuthenticatedUri(const std::string& uri, const Config& config);
std::string UsernameToken(const Config& config, const std::string& nonce, const std::string& created);
std::string LocalName(const char* name);
const tinyxml2::XMLElement* Child(const tinyxml2::XMLNode* node, const char* name);
const tinyxml2::XMLElement* Find(const tinyxml2::XMLNode* node, const char* name);
std::string Text(const tinyxml2::XMLNode* node, const char* name);
std::unique_ptr<tinyxml2::XMLDocument> Parse(const std::string& xml);
nlohmann::json ParseProfiles(const std::string& xml);
nlohmann::json Interfaces();
nlohmann::json Discover(const std::string& address, int timeoutMs);

class Client {
public:
    explicit Client(Config config, const std::atomic<bool>* running = nullptr);
    nlohmann::json Probe();
    std::string StreamUri();

private:
    std::string Call(const std::string& url, const std::string& ns, const std::string& operation,
                     const std::string& body, bool anonymous = false);
    void Prepare();
    Config config_;
    const std::atomic<bool>* running_;
    std::chrono::steady_clock::time_point deadline_;
    std::string media_;
    std::string media2_;
    long time_offset_{0};
};
}  // namespace cosmo::service::onvif
