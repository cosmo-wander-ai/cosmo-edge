// System info types — MsgMemoryInfo, MsgGpuInfo, MsgDiskInfo, MsgNetInfo, MsgHwInfo.
// Modular DTO header.

#pragma once

#include "util/MsgBaseTypes.h"

namespace cosmo {

struct DeviceInfo {
    std::string key;
    std::string name;
    std::string value;
    friend void to_json(nlohmann::json& j, const DeviceInfo& v);
    friend void from_json(const nlohmann::json& j, DeviceInfo& v);
};

struct MsgMemoryInfo {
    int64_t memtotal{-1};
    int64_t memavailable{-1};
    friend void to_json(nlohmann::json& j, const MsgMemoryInfo& v);
    friend void from_json(const nlohmann::json& j, MsgMemoryInfo& v);
};

struct MsgGpuDevUsage {
    double gpuusage{0.0};
    double gpumemusage{0.0};
    int64_t gpumem{0};
    int64_t gpumemtotal{0};
    int64_t gpumemavailable{0};
    friend void to_json(nlohmann::json& j, const MsgGpuDevUsage& v);
    friend void from_json(const nlohmann::json& j, MsgGpuDevUsage& v);
};

struct MsgGpuInfo {
    double gpuusage{0.0};
    bool gpuusageAvailable{true};
    std::string utilizationMetric;
    std::vector<double> coreUtilizations;
    std::string memoryDomain;
    double gpumemusage{0.0};
    int64_t gpumemtotal{0};
    int64_t gpumemavailable{0};
    std::string gpuCapacity;
    std::vector<MsgGpuDevUsage> gpudevusage;
    uint64_t activePreviewPublishers{0};
    uint64_t activePreviewStreams{0};
    uint64_t activeRawPreviewStreams{0};
    uint64_t activeAlgorithmPreviewStreams{0};
    uint64_t previewStreamStarts{0};
    uint64_t previewStreamStops{0};
    uint64_t previewStreamFailures{0};
    uint64_t osdFrames{0};
    double osdMs{0.0};
    uint64_t publishedFrames{0};
    double publishMs{0.0};
    uint64_t firstFrames{0};
    double firstFrameMs{0.0};
    double firstFrameMaxMs{0.0};
    bool videoEncoderAvailable{false};
    std::string videoEncoderBackend;
    std::string videoEncoderImplementation;
    std::string videoEncoderDetail;
    bool videoDecoderAvailable{false};
    std::string videoDecoderBackend;
    std::string videoDecoderImplementation;
    std::string videoDecoderDetail;
    uint64_t rgaFrames{0};
    double rgaMs{0.0};
    uint64_t rgaFailures{0};
    uint64_t mppEncodedFrames{0};
    double mppEncodeMs{0.0};
    uint64_t mppEncodeFailures{0};
    uint64_t mppDecodedFrames{0};
    double mppDecodeMs{0.0};
    uint64_t mppDecodeFailures{0};
    uint64_t mppDecodeFallbacks{0};
    uint64_t mppCopyOutFrames{0};
    double mppCopyOutMs{0.0};
    uint64_t mppCopyOutFailures{0};
    uint64_t mppRgaCopyOutFrames{0};
    uint64_t mppRgaCopyOutFailures{0};
    uint64_t mppCpuCopyOutFallbacks{0};
    uint64_t mppRgaCopyInFrames{0};
    uint64_t mppRgaCopyInFailures{0};
    uint64_t mppCpuCopyInFallbacks{0};
    uint64_t mppEarlyDroppedFrames{0};
    uint64_t colorConvertFrames{0};
    double colorConvertMs{0.0};
    uint64_t blobConvertFrames{0};
    double blobConvertMs{0.0};
    uint64_t graphForwardFrames{0};
    double graphForwardMs{0.0};
    uint64_t graphForwardFailures{0};
    uint64_t resultParseFrames{0};
    double resultParseMs{0.0};
    uint64_t resultParseFailures{0};
    uint64_t rknnForwards{0};
    double rknnForwardMs{0.0};
    uint64_t rknnForwardFailures{0};
    uint64_t rknnPrepareCalls{0};
    double rknnPrepareMs{0.0};
    uint64_t rknnInputsSetCalls{0};
    double rknnInputsSetMs{0.0};
    uint64_t rknnRunCalls{0};
    double rknnRunMs{0.0};
    uint64_t rknnOutputsGetCalls{0};
    double rknnOutputsGetMs{0.0};
    uint64_t rknnOutputsReleaseCalls{0};
    double rknnOutputsReleaseMs{0.0};
    uint64_t rknnOutputTransformCalls{0};
    double rknnOutputTransformMs{0.0};
    uint64_t rknnMutexWaitCalls{0};
    double rknnMutexWaitMs{0.0};
    uint64_t rknnDetectorForwards{0};
    double rknnDetectorForwardMs{0.0};
    uint64_t rknnDetectorForwardFailures{0};
    uint64_t rknnDetectorPrepareCalls{0};
    double rknnDetectorPrepareMs{0.0};
    uint64_t rknnDetectorInputsSetCalls{0};
    double rknnDetectorInputsSetMs{0.0};
    uint64_t rknnDetectorRunCalls{0};
    double rknnDetectorRunMs{0.0};
    uint64_t rknnDetectorOutputsGetCalls{0};
    double rknnDetectorOutputsGetMs{0.0};
    uint64_t rknnDetectorOutputsReleaseCalls{0};
    double rknnDetectorOutputsReleaseMs{0.0};
    uint64_t rknnDetectorOutputTransformCalls{0};
    double rknnDetectorOutputTransformMs{0.0};
    uint64_t rknnDetectorMutexWaitCalls{0};
    double rknnDetectorMutexWaitMs{0.0};
    uint64_t rknnPreprocessFastHits{0};
    uint64_t rknnRgaFillCalls{0};
    double rknnRgaFillMs{0.0};
    uint64_t rknnRgaResizeColorCalls{0};
    double rknnRgaResizeColorMs{0.0};
    uint64_t rknnRgaCropResizeCalls{0};
    double rknnRgaCropResizeMs{0.0};
    uint64_t rknnRgaCropResizeFailures{0};
    uint64_t rknnRgaCropDmaBufFrames{0};
    uint64_t rknnRgaCropHostFallbacks{0};
    uint64_t rknnRgaFailures{0};
    uint64_t rknnCpuResizeFallbackCalls{0};
    double rknnCpuResizeFallbackMs{0.0};
    uint64_t rknnCpuCropResizeFallbackCalls{0};
    double rknnCpuCropResizeFallbackMs{0.0};
    uint64_t rknnCpuNormalizeFallbackCalls{0};
    double rknnCpuNormalizeFallbackMs{0.0};
    uint64_t rknnNativeInputMapCalls{0};
    double rknnNativeInputMapMs{0.0};
    uint64_t rknnNativeInt8Inputs{0};
    uint64_t rknnFloatInputs{0};
    uint64_t rknnUint8ContractInputs{0};
    uint64_t rknnInputCompatibilityFallbacks{0};
    uint64_t rknnBoundInputBindAttempts{0};
    uint64_t rknnBoundInputBindFailures{0};
    uint64_t rknnBoundInputCopyCalls{0};
    double rknnBoundInputCopyMs{0.0};
    uint64_t rknnBoundInputCopyBytes{0};
    uint64_t rknnBoundInputCopyFailures{0};
    uint64_t rknnBoundInputSyncCalls{0};
    double rknnBoundInputSyncMs{0.0};
    uint64_t rknnBoundInputSyncFailures{0};
    uint64_t rknnBoundInputFrames{0};
    uint64_t rknnRgaBoundInputBindAttempts{0};
    uint64_t rknnRgaBoundInputBindFailures{0};
    uint64_t rknnRgaBoundInputImportCalls{0};
    double rknnRgaBoundInputImportMs{0.0};
    uint64_t rknnRgaBoundInputImportFailures{0};
    uint64_t rknnRgaBoundInputFrames{0};
    uint64_t rknnRgaBoundUint8Frames{0};
    uint64_t rknnRgaBoundNativeInt8Frames{0};
    uint64_t rknnRgaBoundRequantizeCalls{0};
    double rknnRgaBoundRequantizeMs{0.0};
    uint64_t rknnRgaBoundRequantizeFailures{0};
    uint64_t rknnRgaBoundInputNormalizeBypasses{0};
    uint64_t rknnMppDmaBufImportCalls{0};
    double rknnMppDmaBufImportMs{0.0};
    uint64_t rknnMppDmaBufImportFailures{0};
    // Hybrid native resize + small host RGB conversion; distinct from full-frame host fallback.
    uint64_t rknnRgaSmallFrameResizeCalls{0};
    double rknnRgaSmallFrameResizeMs{0.0};
    uint64_t rknnRgaSmallFrameCscCalls{0};
    double rknnRgaSmallFrameCscMs{0.0};
    uint64_t rknnMppDmaBufFrames{0};
    uint64_t rknnMppDmaBufFallbacks{0};
    uint64_t rknnMppDmaBufSourceBytes{0};
    uint64_t rknnNativeInt8Outputs{0};
    uint64_t rknnFloatOutputs{0};
    uint64_t rknnOutputCompatibilityFallbacks{0};
    uint64_t rknnNativeOutputBytes{0};
    uint64_t rknnFloatOutputBytes{0};
    uint64_t rknnYolov8DflCalls{0};
    double rknnYolov8DflMs{0.0};
    uint64_t rknnYolov8ClassCalls{0};
    double rknnYolov8ClassMs{0.0};
    uint64_t rknnYolov8DirectCandidateCalls{0};
    uint64_t rknnYolov8DirectCandidateFailures{0};
    uint64_t rknnYolov8DirectPointsScanned{0};
    uint64_t rknnYolov8DirectPointsDecoded{0};
    uint64_t rknnYolov8ScoreSumPointsRejected{0};
    uint64_t rknnYolov8LogicalFloatBytesAvoided{0};
    uint64_t yolov8PostprocessCalls{0};
    double yolov8PostprocessMs{0.0};
    uint64_t yolov8NmsCalls{0};
    double yolov8NmsMs{0.0};
    friend void to_json(nlohmann::json& j, const MsgGpuInfo& v);
    friend void from_json(const nlohmann::json& j, MsgGpuInfo& v);
};

struct MsgDiskInfo {
    int64_t disktotal{-1};
    int64_t diskavailable{-1};
    friend void to_json(nlohmann::json& j, const MsgDiskInfo& v);
    friend void from_json(const nlohmann::json& j, MsgDiskInfo& v);
};

struct MsgNetInfo {
    int64_t networkupperrate{0};
    int64_t networkdownwardrate{0};
    friend void to_json(nlohmann::json& j, const MsgNetInfo& v);
    friend void from_json(const nlohmann::json& j, MsgNetInfo& v);
};
struct MsgHwInfo {
    double cpuusage;
    MsgMemoryInfo memoryinfo;
    MsgGpuInfo gpuinfo;
    MsgDiskInfo diskinfo;
    MsgNetInfo netinfo;
    friend void to_json(nlohmann::json& j, const MsgHwInfo& v);
    friend void from_json(const nlohmann::json& j, MsgHwInfo& v);
};

struct ActionStatus {
    std::string statusCode;
    std::string statusDesc;
    std::string statusDescKey;
    std::string actionId;
    std::string name;

    uint64_t holdCount{0};  // Current queue size (pending packets)

    size_t alarmCount{0};      // Alarm count (alarm nodes only)
    uint64_t insertCount{0};   // Total inserted packets
    uint64_t processCount{0};  // Total processed packets
    uint64_t discardCount{0};  // Total discarded packets

    int64_t periodMs{0};
    uint64_t insertCountPeriod{0};   // Inserted packets in current period
    uint64_t processCountPeriod{0};  // Processed packets in current period
    uint64_t discardCountPeriod{0};  // Discarded packets in current period
    friend void to_json(nlohmann::json& j, const ActionStatus& v);
    friend void from_json(const nlohmann::json& j, ActionStatus& v);
};
using ActionStatusPtr = std::shared_ptr<ActionStatus>;

}  // namespace cosmo
