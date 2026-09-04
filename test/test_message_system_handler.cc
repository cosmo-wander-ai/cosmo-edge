// Unit tests for MessageSystemHandler

// clang-format off
#include "catch_amalgamated.hpp"
#include "catch2/trompeloeil.hpp"
// clang-format on

#include "api/MessageSystemHandler.h"
#include "media/PreviewPipelineMetrics.h"
#include "mock/MockConfigNetworkService.h"
#include "mock/MockConfigReadService.h"
#include "mock/MockConfigWriteService.h"
#include "mock/MockDeviceInfoService.h"
#include "mock/MockSystemOperationService.h"
#include "mock/MockTimeService.h"
#include "nn/core/inference_pipeline_metrics.h"
#include "util/ErrorCode.h"

using namespace cosmo;
using namespace cosmo::test;
using trompeloeil::_;

namespace {

struct SystemHandlerMocks {
    MockConfigReadService configReadSvc;
    MockConfigWriteService configWriteSvc;
    MockConfigNetworkService configNetSvc;
    MockDeviceInfoService deviceInfoSvc;
    MockSystemOperationService systemOpSvc;
    MockTimeService timeSvc;
};

MessageSystemHandler MakeHandler(SystemHandlerMocks& mocks) {
    return MessageSystemHandler(mocks.configReadSvc, mocks.configWriteSvc, mocks.configNetSvc,
                                mocks.deviceInfoSvc, mocks.systemOpSvc, mocks.timeSvc);
}

}  // namespace

TEST_CASE("SystemHandler: QueryDeviceInfo", "[system-handler]") {
    SystemHandlerMocks mocks;
    auto handler = MakeHandler(mocks);

    service::DeviceBasicInfo info;
    info.devSn      = "SN-TEST-001";
    info.devModel   = "COSMO-X";
    info.devVersion = "1.0.0";
    REQUIRE_CALL(mocks.deviceInfoSvc, GetDeviceInfo()).RETURN(info);

    System::MsgQueryDeviceInfoRecv data{};
    std::error_condition errc;
    auto ret = handler.Handle(std::move(data), errc);
    REQUIRE(!errc);
}

TEST_CASE("SystemHandler: QueryHardwareResource exposes accelerator preview telemetry", "[system-handler]") {
    SystemHandlerMocks mocks;
    auto handler = MakeHandler(mocks);

    service::HwResourceItem system_memory{
        "generalMemoryUtilization", "系统内存使用率", 18, "1.37 GiB", "6.36 GiB", 1, "system"};
    REQUIRE_CALL(mocks.deviceInfoSvc, GetHardwareResource(_))
        .RETURN(std::vector<service::HwResourceItem>{system_memory});
    MsgGpuInfo gpu;
    gpu.gpuusage          = 0.5;
    gpu.gpuusageAvailable = false;
    REQUIRE_CALL(mocks.deviceInfoSvc, GetGpuUtilization()).RETURN(gpu);
    const auto preview   = media::GetPreviewPipelineMetrics().Snapshot();
    const auto inference = nn::GetInferencePipelineMetrics().Snapshot();

    System::MsgQueryHardwareResourceRecv data{};
    std::error_condition errc;
    auto ret = handler.Handle(std::move(data), errc);

    CHECK(ret.resData.accelerator.gpuusage == 0.5);
    CHECK_FALSE(ret.resData.accelerator.gpuusageAvailable);
    CHECK(ret.resData.accelerator.activePreviewStreams == preview.active_preview_streams);
    CHECK(ret.resData.accelerator.activeAlgorithmPreviewStreams == preview.active_algorithm_preview_streams);
    CHECK_FALSE(ret.resData.accelerator.videoEncoderBackend.empty());
    CHECK_FALSE(ret.resData.accelerator.videoEncoderDetail.empty());
    CHECK_FALSE(ret.resData.accelerator.videoDecoderBackend.empty());
    CHECK_FALSE(ret.resData.accelerator.videoDecoderDetail.empty());
    CHECK(ret.resData.accelerator.rgaFrames == preview.rga_frames);
    CHECK(ret.resData.accelerator.mppEncodedFrames == preview.mpp_encoded_frames);
    CHECK(ret.resData.accelerator.mppDecodedFrames == preview.mpp_decoded_frames);
    CHECK(ret.resData.accelerator.mppCopyOutFrames == preview.mpp_copy_out_frames);
    CHECK(ret.resData.accelerator.mppRgaCopyOutFrames == preview.mpp_rga_copy_out_frames);
    CHECK(ret.resData.accelerator.mppRgaCopyInFrames == preview.mpp_rga_copy_in_frames);
    CHECK(ret.resData.accelerator.mppEarlyDroppedFrames == preview.mpp_early_dropped_frames);
    CHECK(ret.resData.accelerator.colorConvertFrames == inference.color_convert_frames);
    CHECK(ret.resData.accelerator.rknnForwards == inference.rknn_forwards);
    CHECK(ret.resData.accelerator.rknnDetectorForwards == inference.rknn_detector_forwards);
    CHECK(ret.resData.accelerator.rknnPreprocessFastHits == inference.rknn_preprocess_fast_hits);
    CHECK(ret.resData.accelerator.rknnRgaCropResizeCalls == inference.rknn_rga_crop_resize_calls);
    CHECK(ret.resData.accelerator.rknnRgaCropResizeFailures == inference.rknn_rga_crop_resize_failures);
    CHECK(ret.resData.accelerator.rknnRgaCropDmaBufFrames == inference.rknn_rga_crop_dmabuf_frames);
    CHECK(ret.resData.accelerator.rknnRgaCropHostFallbacks == inference.rknn_rga_crop_host_fallbacks);
    CHECK(ret.resData.accelerator.rknnCpuCropResizeFallbackCalls ==
          inference.rknn_cpu_crop_resize_fallback_calls);
    CHECK(ret.resData.accelerator.rknnOutputsReleaseCalls == inference.rknn_outputs_release_calls);
    CHECK(ret.resData.accelerator.rknnNativeInt8Outputs == inference.rknn_native_int8_outputs);
    CHECK(ret.resData.accelerator.rknnBoundInputBindAttempts == inference.rknn_bound_input_bind_attempts);
    CHECK(ret.resData.accelerator.rknnBoundInputFrames == inference.rknn_bound_input_frames);
    CHECK(ret.resData.accelerator.rknnRgaBoundInputBindAttempts ==
          inference.rknn_rga_bound_input_bind_attempts);
    CHECK(ret.resData.accelerator.rknnRgaBoundInputImportCalls ==
          inference.rknn_rga_bound_input_import_calls);
    CHECK(ret.resData.accelerator.rknnRgaBoundInputFrames == inference.rknn_rga_bound_input_frames);
    CHECK(ret.resData.accelerator.rknnRgaBoundUint8Frames == inference.rknn_rga_bound_uint8_frames);
    CHECK(ret.resData.accelerator.rknnRgaBoundRequantizeCalls == inference.rknn_rga_bound_requantize_calls);
    CHECK(ret.resData.accelerator.rknnRgaBoundInputNormalizeBypasses ==
          inference.rknn_rga_bound_input_normalize_bypasses);
    CHECK(ret.resData.accelerator.rknnMppDmaBufImportCalls == inference.rknn_mpp_dmabuf_import_calls);
    CHECK(ret.resData.accelerator.rknnMppDmaBufFrames == inference.rknn_mpp_dmabuf_frames);
    CHECK(ret.resData.accelerator.rknnMppDmaBufFallbacks == inference.rknn_mpp_dmabuf_fallbacks);
    CHECK(ret.resData.accelerator.rknnOutputCompatibilityFallbacks ==
          inference.rknn_output_compatibility_fallbacks);
    CHECK(ret.resData.accelerator.rknnYolov8DflCalls == inference.rknn_yolov8_dfl_calls);
    CHECK(ret.resData.accelerator.rknnYolov8DirectCandidateCalls ==
          inference.rknn_yolov8_direct_candidate_calls);
    CHECK(ret.resData.accelerator.rknnYolov8ScoreSumPointsRejected ==
          inference.rknn_yolov8_score_sum_points_rejected);
    CHECK(ret.resData.accelerator.yolov8PostprocessCalls == inference.yolov8_postprocess_calls);
    REQUIRE(ret.resData.itemList.size() == 1);
    CHECK(ret.resData.itemList.front().memoryDomain == "system");
    CHECK(ret.resData.itemList.front().usedSize == "1.37 GiB");
}

TEST_CASE("SystemHandler: CheckUpgradeSpace exposes cleanup decision facts", "[system-handler][upgrade]") {
    SystemHandlerMocks mocks;
    auto handler = MakeHandler(mocks);

    REQUIRE_CALL(mocks.systemOpSvc, CheckUpgradeSpace(2048, true, _))
        .SIDE_EFFECT(_3 = service::UpgradeSpaceStatus{false, 5120, 4096, 2048, 1024, 3})
        .RETURN(util::ErrorEnum::Success);

    System::MsgCheckUpgradeSpaceRecv data{};
    data.packageSizeBytes  = 2048;
    data.cleanupEventMedia = true;
    std::error_condition errc;
    const auto result = handler.Handle(std::move(data), errc);

    CHECK(!errc);
    CHECK_FALSE(result.resData.sufficient);
    CHECK(result.resData.requiredBytes == 5120);
    CHECK(result.resData.availableBytes == 4096);
    CHECK(result.resData.eventMediaBytes == 2048);
    CHECK(result.resData.deletedMediaBytes == 1024);
    CHECK(result.resData.deletedMediaFiles == 3);
}

TEST_CASE("SystemHandler: QueryPictureQuality", "[system-handler]") {
    SystemHandlerMocks mocks;
    auto handler = MakeHandler(mocks);

    cosmo::CfgAlarmParamOverviewInfo quality;
    REQUIRE_CALL(mocks.configReadSvc, GetPictureQuality()).RETURN(quality);

    System::MsgQueryPictureQualityRecv data{};
    std::error_condition errc;
    auto ret = handler.Handle(std::move(data), errc);
    REQUIRE(!errc);
}

TEST_CASE("SystemHandler: SetPictureQuality", "[system-handler]") {
    SystemHandlerMocks mocks;
    auto handler = MakeHandler(mocks);

    REQUIRE_CALL(mocks.configWriteSvc, SetPictureQuality(_)).RETURN(cosmo::util::ErrorEnum::Success);

    System::MsgSetPictureQualityRecv data{};
    std::error_condition errc;
    auto ret = handler.Handle(std::move(data), errc);
    REQUIRE(!errc);
}

TEST_CASE("SystemHandler: ResetPictureQuality", "[system-handler]") {
    SystemHandlerMocks mocks;
    auto handler = MakeHandler(mocks);

    REQUIRE_CALL(mocks.configWriteSvc, ResetPictureQuality()).RETURN(cosmo::util::ErrorEnum::Success);

    System::MsgResetPictureQualityRecv data{};
    std::error_condition errc;
    auto ret = handler.Handle(std::move(data), errc);
    REQUIRE(!errc);
}

TEST_CASE("SystemHandler: QueryDebugMode", "[system-handler]") {
    SystemHandlerMocks mocks;
    auto handler = MakeHandler(mocks);

    REQUIRE_CALL(mocks.configReadSvc, GetDebugMode()).RETURN(false);

    System::MsgQueryDebugModeRecv data{};
    std::error_condition errc;
    auto ret = handler.Handle(std::move(data), errc);
    REQUIRE(!errc);
}

TEST_CASE("SystemHandler: ModifyDebugMode", "[system-handler]") {
    SystemHandlerMocks mocks;
    auto handler = MakeHandler(mocks);

    REQUIRE_CALL(mocks.configWriteSvc, SetDebugMode(true));

    System::MsgModifyDebugModeRecv data{};
    data.debugModeOpen = true;
    std::error_condition errc;
    auto ret = handler.Handle(std::move(data), errc);
    REQUIRE(!errc);
}

TEST_CASE("SystemHandler: QueryAlarmVideoDuration", "[system-handler]") {
    SystemHandlerMocks mocks;
    auto handler = MakeHandler(mocks);

    cosmo::CfgAlarmParamVideoRecordInfo duration;
    REQUIRE_CALL(mocks.configReadSvc, GetAlarmVideoDuration()).RETURN(duration);

    System::MsgQueryAlarmVideoDurationRecv data{};
    std::error_condition errc;
    auto ret = handler.Handle(std::move(data), errc);
    REQUIRE(!errc);
}

TEST_CASE("SystemHandler: QueryDevRebootParam", "[system-handler]") {
    SystemHandlerMocks mocks;
    auto handler = MakeHandler(mocks);

    cosmo::CfgRebootParamInfo reboot;
    REQUIRE_CALL(mocks.configReadSvc, GetRebootParam()).RETURN(reboot);

    System::MsgQueryDevRestartParamRecv data{};
    std::error_condition errc;
    auto ret = handler.Handle(std::move(data), errc);
    REQUIRE(!errc);
}

TEST_CASE("SystemHandler: ModifyDevRestartParam accepts strict HH:MM", "[system-handler]") {
    SystemHandlerMocks mocks;
    auto handler = MakeHandler(mocks);

    REQUIRE_CALL(mocks.configWriteSvc, SetRebootParam(_))
        .WITH(_1.isTimingRestart)
        .WITH(_1.weekDay == 3)
        .WITH(_1.restartTimeSec == 9 * 3600 + 5 * 60)
        .RETURN(util::ErrorEnum::Success);

    System::MsgModifyDevRestartParamRecv data{};
    data.isTimingRestart = 1;
    data.weekDay         = 3;
    data.restartTime     = std::string("09:05");
    std::error_condition errc;

    (void)handler.Handle(std::move(data), errc);

    REQUIRE(!errc);
}

TEST_CASE("SystemHandler: ModifyDevRestartParam rejects malformed HH:MM", "[system-handler]") {
    SystemHandlerMocks mocks;
    auto handler = MakeHandler(mocks);

    for (const auto* invalid : {"", "9:05", "09:5", "09:05 ", "09-05", "0a:05", "24:00", "23:60"}) {
        INFO("restartTime=" << invalid);
        System::MsgModifyDevRestartParamRecv data{};
        data.restartTime          = std::string(invalid);
        std::error_condition errc = util::ErrorEnum::Success;

        (void)handler.Handle(std::move(data), errc);

        CHECK(errc == util::ErrorEnum::ParameterException);
    }
}

TEST_CASE("SystemHandler: QuerySystemLogo", "[system-handler]") {
    SystemHandlerMocks mocks;
    auto handler = MakeHandler(mocks);

    cosmo::service::SystemLogoInfo logo;
    REQUIRE_CALL(mocks.configReadSvc, GetSystemLogo()).RETURN(logo);

    System::MsgQuerySystemLogoRecv data{};
    std::error_condition errc;
    auto ret = handler.Handle(std::move(data), errc);
    REQUIRE(!errc);
}

TEST_CASE("SystemHandler: QueryRunModeParam", "[system-handler]") {
    SystemHandlerMocks mocks;
    auto handler = MakeHandler(mocks);

    REQUIRE_CALL(mocks.configReadSvc, GetRunMode()).RETURN(cosmo::RunMode::RunModeStandAlone);

    System::MsgQueryRunModeParamRecv data{};
    std::error_condition errc;
    auto ret = handler.Handle(std::move(data), errc);
    REQUIRE(!errc);
}

TEST_CASE("SystemHandler: QueryHttpInterfaceParam", "[system-handler]") {
    SystemHandlerMocks mocks;
    auto handler = MakeHandler(mocks);

    cosmo::service::HttpPushParam httpParam;
    REQUIRE_CALL(mocks.configNetSvc, GetHttpInterfaceParam()).RETURN(httpParam);

    System::MsgQueryHttpInterfaceParamRecv data{};
    std::error_condition errc;
    auto ret = handler.Handle(std::move(data), errc);
    REQUIRE(!errc);
}

TEST_CASE("SystemHandler: QueryMqttAdapterParam", "[system-handler]") {
    SystemHandlerMocks mocks;
    auto handler = MakeHandler(mocks);

    cosmo::service::MqttParam mqttParam;
    REQUIRE_CALL(mocks.configNetSvc, GetMqttParam()).RETURN(mqttParam);

    System::MsgQueryMqttAdapterParamRecv data{};
    std::error_condition errc;
    auto ret = handler.Handle(std::move(data), errc);
    REQUIRE(!errc);
}
