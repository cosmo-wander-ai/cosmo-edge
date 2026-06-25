#pragma once
// clang-format off
#include "catch_amalgamated.hpp"
#include "catch2/trompeloeil.hpp"
// clang-format on
#include "flow/common/AlgDataUnit.h"
#include "service/system/IDeviceInfoService.h"
#include "trompeloeil.hpp"
#include "util/PathUtil.h"

namespace cosmo::test {

class MockDeviceInfoService : public cosmo::service::IDeviceInfoService {
public:
    MAKE_MOCK0(GetDeviceInfo, cosmo::service::DeviceBasicInfo(), override);
    MAKE_MOCK1(GetHardwareResource, std::vector<cosmo::service::HwResourceItem>(double&), override);
    MAKE_MOCK0(GetDevSn, std::string(), override);
    MAKE_MOCK0(GetDevModel, std::string(), override);
    MAKE_MOCK0(GetDevVersion, std::string(), override);
    MAKE_MOCK0(GetDevSpec, std::string(), override);
    MAKE_MOCK0(GetCpuUtilization, double(), override);
    MAKE_MOCK0(GetGpuUtilization, cosmo::MsgGpuInfo(), override);
    MAKE_MOCK0(GetMemoryUtilization, cosmo::MsgMemoryInfo(), override);
    MAKE_MOCK0(GetDiskUtilization, cosmo::MsgDiskInfo(), override);
    MAKE_MOCK0(GetNetUtilization, cosmo::MsgNetInfo(), override);
    MAKE_MOCK0(GetMacs, (std::vector<std::pair<std::string, std::string>>)(), override);
    MAKE_MOCK0(GetDevId, std::string(), override);
    MAKE_MOCK0(GetIPV4, std::string(), override);
    MAKE_MOCK0(GetAvailableGpuMemoryMB, int64_t(), override);
    MAKE_MOCK0(GetGpuNum, size_t(), override);
};

}  // namespace cosmo::test
