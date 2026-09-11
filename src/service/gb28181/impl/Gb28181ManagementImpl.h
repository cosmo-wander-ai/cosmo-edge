#pragma once
#include <memory>

#include "service/gb28181/IGb28181Management.h"

namespace cosmo::service {
class Gb28181ManagementImpl final : public IGb28181Management {
public:
    Gb28181ManagementImpl();
    ~Gb28181ManagementImpl() override;
    void Init() override;
    nlohmann::json Execute(const nlohmann::json& request) override;
    void Ensure(const std::string& channelId) override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace cosmo::service
