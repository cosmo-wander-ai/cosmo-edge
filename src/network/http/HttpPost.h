#pragma once

#include <string>

namespace cosmo::network::http {

class HttpRequest;

class HttpPost {
public:
    HttpPost();

    HttpPost(const HttpPost&)            = delete;
    HttpPost& operator=(const HttpPost&) = delete;

    void SetIpPort(const std::string& ip_port);
    void SetAppInfo(const std::string& app_key, const std::string& app_secret);
    bool HasAppInfo() const noexcept;

    bool GetFileServerConfig(std::string& response);

private:
    bool AppendHeaderS(HttpRequest& request);
    bool LoadAppInfoFromRuntime();

    std::string app_key_;
    std::string app_secret_;
    std::string get_file_server_config_url_;
};

}  // namespace cosmo::network::http
