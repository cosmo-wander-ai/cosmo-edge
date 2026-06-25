#pragma once
// clang-format off
#include "catch_amalgamated.hpp"
#include "catch2/trompeloeil.hpp"
// clang-format on
#include "flow/common/AlgDataUnit.h"
#include "service/camera/ICameraService.h"
#include "trompeloeil.hpp"
#include "util/PathUtil.h"

namespace cosmo::test {

class MockCameraService : public cosmo::service::ICameraService {
public:
    MAKE_MOCK2(Add, cosmo::util::ErrorEnum(cosmo::MsgCameraInfo&, std::string&), override);
    MAKE_MOCK1(Update, cosmo::util::ErrorEnum(cosmo::MsgCameraInfo&), override);
    MAKE_MOCK1(Delete, cosmo::util::ErrorEnum(const std::string&), override);
    MAKE_MOCK5(Query, std::vector<cosmo::MsgCameraInfo>(const std::string&, int, int, int, size_t&),
               override);
    MAKE_MOCK3(ModifyTaskParam,
               cosmo::util::ErrorEnum(const std::string&, const std::string&, cosmo::MsgTaskConfig&),
               override);
    MAKE_MOCK3(QueryTaskParam,
               cosmo::util::ErrorEnum(const std::string&, const std::string&,
                                      std::vector<cosmo::MsgDynamicKeyValue>&),
               override);
    MAKE_MOCK4(ModifyTaskArea,
               cosmo::util::ErrorEnum(const std::string&, const std::string&,
                                      const std::vector<cosmo::MsgTaskArea>&,
                                      const std::vector<cosmo::MsgTaskArea>&),
               override);
    MAKE_MOCK4(QueryTaskArea,
               cosmo::util::ErrorEnum(const std::string&, const std::string&,
                                      std::vector<cosmo::MsgTaskArea>&, std::vector<cosmo::MsgTaskArea>&),
               override);
    MAKE_MOCK3(ModifyTaskStrategy,
               cosmo::util::ErrorEnum(const std::string&, const std::string&, const std::string&), override);
    MAKE_MOCK3(QueryTaskStrategy,
               cosmo::util::ErrorEnum(const std::string&, const std::string&, std::string&), override);
    MAKE_MOCK3(SwitchTask, cosmo::util::ErrorEnum(const std::string&, const std::string&, bool), override);
    MAKE_MOCK3(QuerySwitch, cosmo::util::ErrorEnum(const std::string&, const std::string&, bool&), override);
    MAKE_MOCK2(DeleteTask, cosmo::util::ErrorEnum(const std::string&, const std::string&), override);
    MAKE_MOCK1(GetTasks, std::vector<service::camera::CameraTaskDto>(const std::string&), override);
    MAKE_MOCK2(NotifyAlgorithmsChanged, void(const std::vector<std::string>&, bool), override);
    MAKE_MOCK1(NotifyAlgorithmsDeleted, void(const std::vector<std::string>&), override);
    MAKE_MOCK1(IsAlgorithmInUse, bool(const std::string&), const override);
    MAKE_MOCK1(ScheduleInUse, bool(const std::string&), override);
    MAKE_MOCK2(CaptureImage, VideoFramePtr(const std::string&, int), override);
    MAKE_MOCK4(BindTaskLibPara,
               cosmo::util::ErrorEnum(const std::string&, const std::string&, const std::vector<std::string>&,
                                      const std::string&),
               override);
    MAKE_MOCK0(IsIotNetworkMode, bool(), override);
    MAKE_MOCK1(EncodeJpeg, std::vector<uint8_t>(const VideoFramePtr&), override);
    MAKE_MOCK1(GetWebLocalPath, std::string(int64_t), override);
    MAKE_MOCK1(GetWebAccessPath, std::string(int64_t), override);
    MAKE_MOCK0(QueryUsbCameraList, std::vector<cosmo::camera::MsgUsbCameraDevice>(), override);
    MAKE_MOCK1(GetChannelInst, cosmo::AlgChannelPtr(const std::string&), override);
    MAKE_MOCK1(GetChannelName, std::string(const std::string&), const override);
    MAKE_MOCK0(InitCameraEntities, void(), override);
};

}  // namespace cosmo::test
