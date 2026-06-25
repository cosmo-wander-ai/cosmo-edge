#pragma once
// clang-format off
#include "catch_amalgamated.hpp"
#include "catch2/trompeloeil.hpp"
// clang-format on
#include "flow/common/AlgDataUnit.h"
#include "service/system/IAppInfoService.h"
#include "trompeloeil.hpp"
#include "util/PathUtil.h"

namespace cosmo::test {

class MockAppInfoService : public cosmo::service::IAppInfoService {
public:
    MAKE_MOCK0(GetHaveManager, bool(), override);
    MAKE_MOCK0(GetEngineType, std::string(), override);
    MAKE_MOCK0(DevId, std::string(), override);
    MAKE_MOCK0(GetAppRuntime, int64_t(), override);
    MAKE_MOCK0(GetPicTaskGroupCount, int(), override);
    MAKE_MOCK0(UserDataPath, std::string(), override);
    MAKE_MOCK0(GetTaskOverviewDataPath, std::string(), override);
    MAKE_MOCK1(SetOverviewStructureRecord, void(bool), override);
    MAKE_MOCK0(GetOverviewStructureRecord, bool(), override);
    MAKE_MOCK1(SetOverviewStructureFile, void(bool), override);
    MAKE_MOCK0(GetOverviewStructureFile, bool(), override);
    MAKE_MOCK0(GetModelDebug, bool(), override);
    MAKE_MOCK0(GetNumber, size_t(), override);
    MAKE_MOCK0(LogPath, std::string(), override);
    MAKE_MOCK0(LogWebPath, std::string(), override);
    MAKE_MOCK0(GetCpuUtilization, double(), override);
    MAKE_MOCK0(GetGpuUtilization, cosmo::MsgGpuInfo(), override);
    MAKE_MOCK0(GetMemoryUtilization, cosmo::MsgMemoryInfo(), override);
    MAKE_MOCK0(GetDiskUtilization, cosmo::MsgDiskInfo(), override);
    MAKE_MOCK0(GetNetUtilization, cosmo::MsgNetInfo(), override);
    MAKE_MOCK0(GetAvailableGpuMemoryMB, int64_t(), override);
    MAKE_MOCK0(GetGpuNum, size_t(), override);
    MAKE_MOCK2(SetModelPath, void(const std::string&, const std::string&), override);
    MAKE_MOCK0(OutputMallocBuf, std::string(), override);
    MAKE_MOCK0(GetMemoryPoolStatus, std::vector<service::PoolStatusDto>(), override);
    MAKE_MOCK2(GetPagedLogs, cosmo::MsgQueryLogsSend(const cosmo::MsgQueryLogsRecv&, std::error_condition&),
               override);
    MAKE_MOCK2(GetSystemOverviewInfo, cosmo::MsgInfoSend(const cosmo::MsgInfoRecv&, std::error_condition&),
               override);
};

}  // namespace cosmo::test
