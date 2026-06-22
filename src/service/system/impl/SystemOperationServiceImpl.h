#pragma once

#include <string>

#include "platform/SystemReboot.h"
#include "service/system/ISystemOperationService.h"

namespace cosmo::service {

class SystemOperationServiceImpl : public ISystemOperationService {
public:
    SystemOperationServiceImpl()           = default;
    ~SystemOperationServiceImpl() override = default;

    // ---- ISystemOperationService ----
    void RebootDevice(const std::string& reason) override;
    void ResetDevice(const std::string& reason) override;
    cosmo::util::ErrorEnum ExportLogs(std::string& fileName, std::string& fileUrl) override;
    cosmo::util::ErrorEnum Upgrade(const std::string& filePath) override;
    void ShowThreadDebugInfo() override;

private:
    cosmo::platform::RebootManager reboot_mgr_;
};

}  // namespace cosmo::service
