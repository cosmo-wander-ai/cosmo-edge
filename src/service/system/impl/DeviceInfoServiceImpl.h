#pragma once

#include <memory>
#include <shared_mutex>
#include <string>

#include "service/system/IDeviceInfoService.h"

namespace cosmo::service {

class DeviceInfoServiceImpl : public IDeviceInfoService {
public:
    DeviceInfoServiceImpl();
    ~DeviceInfoServiceImpl() override;

    // ---- IDeviceInfoService ----
    DeviceBasicInfo GetDeviceInfo() override;
    std::vector<HwResourceItem> GetHardwareResource(double& customScore) override;

    // ---- IDeviceInfoService: HwInfo ----
    std::string GetDevSn() override;
    std::string GetDevModel() override;
    std::string GetDevVersion() override;
    std::string GetDevSpec() override;

    // ---- IDeviceInfoService: HwResUtilization ----
    double GetCpuUtilization() override;
    cosmo::MsgGpuInfo GetGpuUtilization() override;
    cosmo::MsgMemoryInfo GetMemoryUtilization() override;
    cosmo::MsgDiskInfo GetDiskUtilization() override;
    cosmo::MsgNetInfo GetNetUtilization() override;
    std::vector<std::pair<std::string, std::string>> GetMacs() override;
    std::string GetDevId() override;
    std::string GetIPV4() override;
    int64_t GetAvailableGpuMemoryMB() override;
    size_t GetGpuNum() override;

private:
    struct HwInfoState;
    std::unique_ptr<HwInfoState> hw_info_state_;

    struct HwResState;
    std::unique_ptr<HwResState> hw_res_state_;
};

}  // namespace cosmo::service
