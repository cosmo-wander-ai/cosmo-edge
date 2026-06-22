// SystemOperationServiceImpl — System Operation Service Impl implementation.

#include "service/system/impl/SystemOperationServiceImpl.h"

#include <filesystem>

#include "service/detail/ServiceRegistry.h"
#include "service/system/impl/PacketUpgrade.h"
#include "util/ErrorCode.h"
#include "util/Exec.h"
#include "util/Log.h"
#include "util/PathUtil.h"
#include "util/ThreadRegistry.h"
#include "util/TimeUtil.h"

namespace cosmo::service {

namespace fs = std::filesystem;

void SystemOperationServiceImpl::RebootDevice(const std::string& reason) {
    reboot_mgr_.Reboot(reason);
}

void SystemOperationServiceImpl::ResetDevice(const std::string& reason) {
    auto base_dir = cosmo::path::GetBaseDir();
    reboot_mgr_.Reset(reason, base_dir);
}

cosmo::util::ErrorEnum SystemOperationServiceImpl::ExportLogs(std::string& fileName, std::string& fileUrl) {
    auto timestamp      = cosmo::util::GetMilliseconds();
    auto web_local_path = cosmo::path::GetWebLocalPath(timestamp);
    for (auto& f : fs::directory_iterator(web_local_path)) {
        if (f.path().filename().string().find("IedLog") == 0 && f.path().extension() == ".tar") {
            LOG_INFO("remove {}", f.path());
            remove(f.path());
        }
    }

    fileName              = "IedLog" + std::to_string(timestamp) + ".tar";
    std::string save_path = (fs::path(web_local_path) / fileName).string();
    std::string str_cmd   = std::string("tar cf ") + cosmo::util::ShellEscape(save_path) + " " +
                          cosmo::path::GetLogPath() + "/*" + " " + (cosmo::path::GetCfgPath() + "/*");
    LOG_INFO("{}", str_cmd);
    std::string cmd_out;
    int ret = cosmo::util::Exec(str_cmd, cmd_out);
    if (ret != 0) {
        LOG_ERRO("tar failed ({}): {}", ret, cmd_out);
        return cosmo::util::ErrorEnum::SysErr;
    }

    fileUrl = (fs::path(cosmo::path::GetWebAcessPath(timestamp)) / fileName).string();
    return {};
}

cosmo::util::ErrorEnum SystemOperationServiceImpl::Upgrade(const std::string& filePath) {
    auto result = cosmo::PacketUpgrade(filePath);
    if (result == cosmo::util::ErrorEnum::Success) {
        reboot_mgr_.Reboot("upgrade Reboot");
    }
    return result;
}

void SystemOperationServiceImpl::ShowThreadDebugInfo() {
    cosmo::util::ShowAllThreads();
}

}  // namespace cosmo::service
