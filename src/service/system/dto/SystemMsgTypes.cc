// SystemMsgTypes — System info types — MsgMemoryInfo, MsgGpuInfo, MsgDiskInfo, MsgNetInfo, MsgHw...

#include "SystemMsgTypes.h"

#include <nlohmann/json.hpp>

#include "util/JsonFieldOpt.h"
#include "util/LimitedTypeJson.h"

// Auto-generated JSON serialization
namespace cosmo {
void from_json(const nlohmann::json& j, DeviceInfo& v) {
    JSON_OPT(j, v, key);
    JSON_OPT(j, v, name);
    JSON_OPT(j, v, value);
}

void to_json(nlohmann::json& j, const DeviceInfo& v) {
    j["key"]   = v.key;
    j["name"]  = v.name;
    j["value"] = v.value;
}

void from_json(const nlohmann::json& j, MsgMemoryInfo& v) {
    JSON_OPT(j, v, memtotal);
    JSON_OPT(j, v, memavailable);
}

void to_json(nlohmann::json& j, const MsgMemoryInfo& v) {
    j["memtotal"]     = v.memtotal;
    j["memavailable"] = v.memavailable;
}

void from_json(const nlohmann::json& j, MsgGpuDevUsage& v) {
    JSON_OPT(j, v, gpuusage);
    JSON_OPT(j, v, gpumemusage);
    JSON_OPT(j, v, gpumem);
    JSON_OPT(j, v, gpumemtotal);
    JSON_OPT(j, v, gpumemavailable);
}

void to_json(nlohmann::json& j, const MsgGpuDevUsage& v) {
    j["gpuusage"]        = v.gpuusage;
    j["gpumemusage"]     = v.gpumemusage;
    j["gpumem"]          = v.gpumem;
    j["gpumemtotal"]     = v.gpumemtotal;
    j["gpumemavailable"] = v.gpumemavailable;
}

void from_json(const nlohmann::json& j, MsgGpuInfo& v) {
    JSON_OPT(j, v, gpuusage);
    JSON_OPT(j, v, gpuusageAvailable);
    JSON_OPT(j, v, utilizationMetric);
    JSON_OPT(j, v, coreUtilizations);
    JSON_OPT(j, v, memoryDomain);
    JSON_OPT(j, v, gpumemusage);
    JSON_OPT(j, v, gpumemtotal);
    JSON_OPT(j, v, gpumemavailable);
    JSON_OPT(j, v, gpudevusage);
    JSON_OPT(j, v, gpuCapacity);
    JSON_OPT(j, v, activePreviewPublishers);
    JSON_OPT(j, v, activePreviewStreams);
    JSON_OPT(j, v, activeRawPreviewStreams);
    JSON_OPT(j, v, activeAlgorithmPreviewStreams);
    JSON_OPT(j, v, previewStreamStarts);
    JSON_OPT(j, v, previewStreamStops);
    JSON_OPT(j, v, previewStreamFailures);
    JSON_OPT(j, v, osdFrames);
    JSON_OPT(j, v, osdMs);
    JSON_OPT(j, v, publishedFrames);
    JSON_OPT(j, v, publishMs);
    JSON_OPT(j, v, firstFrames);
    JSON_OPT(j, v, firstFrameMs);
    JSON_OPT(j, v, firstFrameMaxMs);
    JSON_OPT(j, v, videoEncoderAvailable);
    JSON_OPT(j, v, videoEncoderBackend);
    JSON_OPT(j, v, videoEncoderImplementation);
    JSON_OPT(j, v, videoEncoderDetail);
    JSON_OPT(j, v, videoDecoderAvailable);
    JSON_OPT(j, v, videoDecoderBackend);
    JSON_OPT(j, v, videoDecoderImplementation);
    JSON_OPT(j, v, videoDecoderDetail);
    JSON_OPT(j, v, rgaFrames);
    JSON_OPT(j, v, rgaMs);
    JSON_OPT(j, v, rgaFailures);
    JSON_OPT(j, v, mppEncodedFrames);
    JSON_OPT(j, v, mppEncodeMs);
    JSON_OPT(j, v, mppEncodeFailures);
    JSON_OPT(j, v, mppDecodedFrames);
    JSON_OPT(j, v, mppDecodeMs);
    JSON_OPT(j, v, mppDecodeFailures);
    JSON_OPT(j, v, mppDecodeFallbacks);
    JSON_OPT(j, v, mppCopyOutFrames);
    JSON_OPT(j, v, mppCopyOutMs);
    JSON_OPT(j, v, mppCopyOutFailures);
    JSON_OPT(j, v, mppRgaCopyOutFrames);
    JSON_OPT(j, v, mppRgaCopyOutFailures);
    JSON_OPT(j, v, mppCpuCopyOutFallbacks);
    JSON_OPT(j, v, mppRgaCopyInFrames);
    JSON_OPT(j, v, mppRgaCopyInFailures);
    JSON_OPT(j, v, mppCpuCopyInFallbacks);
    JSON_OPT(j, v, mppEarlyDroppedFrames);
    JSON_OPT(j, v, colorConvertFrames);
    JSON_OPT(j, v, colorConvertMs);
    JSON_OPT(j, v, blobConvertFrames);
    JSON_OPT(j, v, blobConvertMs);
    JSON_OPT(j, v, graphForwardFrames);
    JSON_OPT(j, v, graphForwardMs);
    JSON_OPT(j, v, graphForwardFailures);
    JSON_OPT(j, v, resultParseFrames);
    JSON_OPT(j, v, resultParseMs);
    JSON_OPT(j, v, resultParseFailures);
    JSON_OPT(j, v, rknnForwards);
    JSON_OPT(j, v, rknnForwardMs);
    JSON_OPT(j, v, rknnForwardFailures);
    JSON_OPT(j, v, rknnPrepareCalls);
    JSON_OPT(j, v, rknnPrepareMs);
    JSON_OPT(j, v, rknnInputsSetCalls);
    JSON_OPT(j, v, rknnInputsSetMs);
    JSON_OPT(j, v, rknnRunCalls);
    JSON_OPT(j, v, rknnRunMs);
    JSON_OPT(j, v, rknnOutputsGetCalls);
    JSON_OPT(j, v, rknnOutputsGetMs);
    JSON_OPT(j, v, rknnOutputsReleaseCalls);
    JSON_OPT(j, v, rknnOutputsReleaseMs);
    JSON_OPT(j, v, rknnOutputTransformCalls);
    JSON_OPT(j, v, rknnOutputTransformMs);
    JSON_OPT(j, v, rknnMutexWaitCalls);
    JSON_OPT(j, v, rknnMutexWaitMs);
    JSON_OPT(j, v, rknnDetectorForwards);
    JSON_OPT(j, v, rknnDetectorForwardMs);
    JSON_OPT(j, v, rknnDetectorForwardFailures);
    JSON_OPT(j, v, rknnDetectorPrepareCalls);
    JSON_OPT(j, v, rknnDetectorPrepareMs);
    JSON_OPT(j, v, rknnDetectorInputsSetCalls);
    JSON_OPT(j, v, rknnDetectorInputsSetMs);
    JSON_OPT(j, v, rknnDetectorRunCalls);
    JSON_OPT(j, v, rknnDetectorRunMs);
    JSON_OPT(j, v, rknnDetectorOutputsGetCalls);
    JSON_OPT(j, v, rknnDetectorOutputsGetMs);
    JSON_OPT(j, v, rknnDetectorOutputsReleaseCalls);
    JSON_OPT(j, v, rknnDetectorOutputsReleaseMs);
    JSON_OPT(j, v, rknnDetectorOutputTransformCalls);
    JSON_OPT(j, v, rknnDetectorOutputTransformMs);
    JSON_OPT(j, v, rknnDetectorMutexWaitCalls);
    JSON_OPT(j, v, rknnDetectorMutexWaitMs);
    JSON_OPT(j, v, rknnPreprocessFastHits);
    JSON_OPT(j, v, rknnRgaFillCalls);
    JSON_OPT(j, v, rknnRgaFillMs);
    JSON_OPT(j, v, rknnRgaResizeColorCalls);
    JSON_OPT(j, v, rknnRgaResizeColorMs);
    JSON_OPT(j, v, rknnRgaCropResizeCalls);
    JSON_OPT(j, v, rknnRgaCropResizeMs);
    JSON_OPT(j, v, rknnRgaCropResizeFailures);
    JSON_OPT(j, v, rknnRgaCropDmaBufFrames);
    JSON_OPT(j, v, rknnRgaCropHostFallbacks);
    JSON_OPT(j, v, rknnRgaFailures);
    JSON_OPT(j, v, rknnCpuResizeFallbackCalls);
    JSON_OPT(j, v, rknnCpuResizeFallbackMs);
    JSON_OPT(j, v, rknnCpuCropResizeFallbackCalls);
    JSON_OPT(j, v, rknnCpuCropResizeFallbackMs);
    JSON_OPT(j, v, rknnCpuNormalizeFallbackCalls);
    JSON_OPT(j, v, rknnCpuNormalizeFallbackMs);
    JSON_OPT(j, v, rknnNativeInputMapCalls);
    JSON_OPT(j, v, rknnNativeInputMapMs);
    JSON_OPT(j, v, rknnNativeInt8Inputs);
    JSON_OPT(j, v, rknnFloatInputs);
    JSON_OPT(j, v, rknnUint8ContractInputs);
    JSON_OPT(j, v, rknnInputCompatibilityFallbacks);
    JSON_OPT(j, v, rknnBoundInputBindAttempts);
    JSON_OPT(j, v, rknnBoundInputBindFailures);
    JSON_OPT(j, v, rknnBoundInputCopyCalls);
    JSON_OPT(j, v, rknnBoundInputCopyMs);
    JSON_OPT(j, v, rknnBoundInputCopyBytes);
    JSON_OPT(j, v, rknnBoundInputCopyFailures);
    JSON_OPT(j, v, rknnBoundInputSyncCalls);
    JSON_OPT(j, v, rknnBoundInputSyncMs);
    JSON_OPT(j, v, rknnBoundInputSyncFailures);
    JSON_OPT(j, v, rknnBoundInputFrames);
    JSON_OPT(j, v, rknnRgaBoundInputBindAttempts);
    JSON_OPT(j, v, rknnRgaBoundInputBindFailures);
    JSON_OPT(j, v, rknnRgaBoundInputImportCalls);
    JSON_OPT(j, v, rknnRgaBoundInputImportMs);
    JSON_OPT(j, v, rknnRgaBoundInputImportFailures);
    JSON_OPT(j, v, rknnRgaBoundInputFrames);
    JSON_OPT(j, v, rknnRgaBoundUint8Frames);
    JSON_OPT(j, v, rknnRgaBoundNativeInt8Frames);
    JSON_OPT(j, v, rknnRgaBoundRequantizeCalls);
    JSON_OPT(j, v, rknnRgaBoundRequantizeMs);
    JSON_OPT(j, v, rknnRgaBoundRequantizeFailures);
    JSON_OPT(j, v, rknnRgaBoundInputNormalizeBypasses);
    JSON_OPT(j, v, rknnMppDmaBufImportCalls);
    JSON_OPT(j, v, rknnMppDmaBufImportMs);
    JSON_OPT(j, v, rknnMppDmaBufImportFailures);
    JSON_OPT(j, v, rknnRgaSmallFrameResizeCalls);
    JSON_OPT(j, v, rknnRgaSmallFrameResizeMs);
    JSON_OPT(j, v, rknnRgaSmallFrameCscCalls);
    JSON_OPT(j, v, rknnRgaSmallFrameCscMs);
    JSON_OPT(j, v, rknnMppDmaBufFrames);
    JSON_OPT(j, v, rknnMppDmaBufFallbacks);
    JSON_OPT(j, v, rknnMppDmaBufSourceBytes);
    JSON_OPT(j, v, rknnNativeInt8Outputs);
    JSON_OPT(j, v, rknnFloatOutputs);
    JSON_OPT(j, v, rknnOutputCompatibilityFallbacks);
    JSON_OPT(j, v, rknnNativeOutputBytes);
    JSON_OPT(j, v, rknnFloatOutputBytes);
    JSON_OPT(j, v, rknnYolov8DflCalls);
    JSON_OPT(j, v, rknnYolov8DflMs);
    JSON_OPT(j, v, rknnYolov8ClassCalls);
    JSON_OPT(j, v, rknnYolov8ClassMs);
    JSON_OPT(j, v, rknnYolov8DirectCandidateCalls);
    JSON_OPT(j, v, rknnYolov8DirectCandidateFailures);
    JSON_OPT(j, v, rknnYolov8DirectPointsScanned);
    JSON_OPT(j, v, rknnYolov8DirectPointsDecoded);
    JSON_OPT(j, v, rknnYolov8ScoreSumPointsRejected);
    JSON_OPT(j, v, rknnYolov8LogicalFloatBytesAvoided);
    JSON_OPT(j, v, yolov8PostprocessCalls);
    JSON_OPT(j, v, yolov8PostprocessMs);
    JSON_OPT(j, v, yolov8NmsCalls);
    JSON_OPT(j, v, yolov8NmsMs);
}

void to_json(nlohmann::json& j, const MsgGpuInfo& v) {
    j["gpuusage"]                           = v.gpuusage;
    j["gpuusageAvailable"]                  = v.gpuusageAvailable;
    j["utilizationMetric"]                  = v.utilizationMetric;
    j["coreUtilizations"]                   = v.coreUtilizations;
    j["memoryDomain"]                       = v.memoryDomain;
    j["gpumemusage"]                        = v.gpumemusage;
    j["gpumemtotal"]                        = v.gpumemtotal;
    j["gpumemavailable"]                    = v.gpumemavailable;
    j["gpudevusage"]                        = v.gpudevusage;
    j["gpuCapacity"]                        = v.gpuCapacity;
    j["activePreviewPublishers"]            = v.activePreviewPublishers;
    j["activePreviewStreams"]               = v.activePreviewStreams;
    j["activeRawPreviewStreams"]            = v.activeRawPreviewStreams;
    j["activeAlgorithmPreviewStreams"]      = v.activeAlgorithmPreviewStreams;
    j["previewStreamStarts"]                = v.previewStreamStarts;
    j["previewStreamStops"]                 = v.previewStreamStops;
    j["previewStreamFailures"]              = v.previewStreamFailures;
    j["osdFrames"]                          = v.osdFrames;
    j["osdMs"]                              = v.osdMs;
    j["publishedFrames"]                    = v.publishedFrames;
    j["publishMs"]                          = v.publishMs;
    j["firstFrames"]                        = v.firstFrames;
    j["firstFrameMs"]                       = v.firstFrameMs;
    j["firstFrameMaxMs"]                    = v.firstFrameMaxMs;
    j["videoEncoderAvailable"]              = v.videoEncoderAvailable;
    j["videoEncoderBackend"]                = v.videoEncoderBackend;
    j["videoEncoderImplementation"]         = v.videoEncoderImplementation;
    j["videoEncoderDetail"]                 = v.videoEncoderDetail;
    j["videoDecoderAvailable"]              = v.videoDecoderAvailable;
    j["videoDecoderBackend"]                = v.videoDecoderBackend;
    j["videoDecoderImplementation"]         = v.videoDecoderImplementation;
    j["videoDecoderDetail"]                 = v.videoDecoderDetail;
    j["rgaFrames"]                          = v.rgaFrames;
    j["rgaMs"]                              = v.rgaMs;
    j["rgaFailures"]                        = v.rgaFailures;
    j["mppEncodedFrames"]                   = v.mppEncodedFrames;
    j["mppEncodeMs"]                        = v.mppEncodeMs;
    j["mppEncodeFailures"]                  = v.mppEncodeFailures;
    j["mppDecodedFrames"]                   = v.mppDecodedFrames;
    j["mppDecodeMs"]                        = v.mppDecodeMs;
    j["mppDecodeFailures"]                  = v.mppDecodeFailures;
    j["mppDecodeFallbacks"]                 = v.mppDecodeFallbacks;
    j["mppCopyOutFrames"]                   = v.mppCopyOutFrames;
    j["mppCopyOutMs"]                       = v.mppCopyOutMs;
    j["mppCopyOutFailures"]                 = v.mppCopyOutFailures;
    j["mppRgaCopyOutFrames"]                = v.mppRgaCopyOutFrames;
    j["mppRgaCopyOutFailures"]              = v.mppRgaCopyOutFailures;
    j["mppCpuCopyOutFallbacks"]             = v.mppCpuCopyOutFallbacks;
    j["mppRgaCopyInFrames"]                 = v.mppRgaCopyInFrames;
    j["mppRgaCopyInFailures"]               = v.mppRgaCopyInFailures;
    j["mppCpuCopyInFallbacks"]              = v.mppCpuCopyInFallbacks;
    j["mppEarlyDroppedFrames"]              = v.mppEarlyDroppedFrames;
    j["colorConvertFrames"]                 = v.colorConvertFrames;
    j["colorConvertMs"]                     = v.colorConvertMs;
    j["blobConvertFrames"]                  = v.blobConvertFrames;
    j["blobConvertMs"]                      = v.blobConvertMs;
    j["graphForwardFrames"]                 = v.graphForwardFrames;
    j["graphForwardMs"]                     = v.graphForwardMs;
    j["graphForwardFailures"]               = v.graphForwardFailures;
    j["resultParseFrames"]                  = v.resultParseFrames;
    j["resultParseMs"]                      = v.resultParseMs;
    j["resultParseFailures"]                = v.resultParseFailures;
    j["rknnForwards"]                       = v.rknnForwards;
    j["rknnForwardMs"]                      = v.rknnForwardMs;
    j["rknnForwardFailures"]                = v.rknnForwardFailures;
    j["rknnPrepareCalls"]                   = v.rknnPrepareCalls;
    j["rknnPrepareMs"]                      = v.rknnPrepareMs;
    j["rknnInputsSetCalls"]                 = v.rknnInputsSetCalls;
    j["rknnInputsSetMs"]                    = v.rknnInputsSetMs;
    j["rknnRunCalls"]                       = v.rknnRunCalls;
    j["rknnRunMs"]                          = v.rknnRunMs;
    j["rknnOutputsGetCalls"]                = v.rknnOutputsGetCalls;
    j["rknnOutputsGetMs"]                   = v.rknnOutputsGetMs;
    j["rknnOutputsReleaseCalls"]            = v.rknnOutputsReleaseCalls;
    j["rknnOutputsReleaseMs"]               = v.rknnOutputsReleaseMs;
    j["rknnOutputTransformCalls"]           = v.rknnOutputTransformCalls;
    j["rknnOutputTransformMs"]              = v.rknnOutputTransformMs;
    j["rknnMutexWaitCalls"]                 = v.rknnMutexWaitCalls;
    j["rknnMutexWaitMs"]                    = v.rknnMutexWaitMs;
    j["rknnDetectorForwards"]               = v.rknnDetectorForwards;
    j["rknnDetectorForwardMs"]              = v.rknnDetectorForwardMs;
    j["rknnDetectorForwardFailures"]        = v.rknnDetectorForwardFailures;
    j["rknnDetectorPrepareCalls"]           = v.rknnDetectorPrepareCalls;
    j["rknnDetectorPrepareMs"]              = v.rknnDetectorPrepareMs;
    j["rknnDetectorInputsSetCalls"]         = v.rknnDetectorInputsSetCalls;
    j["rknnDetectorInputsSetMs"]            = v.rknnDetectorInputsSetMs;
    j["rknnDetectorRunCalls"]               = v.rknnDetectorRunCalls;
    j["rknnDetectorRunMs"]                  = v.rknnDetectorRunMs;
    j["rknnDetectorOutputsGetCalls"]        = v.rknnDetectorOutputsGetCalls;
    j["rknnDetectorOutputsGetMs"]           = v.rknnDetectorOutputsGetMs;
    j["rknnDetectorOutputsReleaseCalls"]    = v.rknnDetectorOutputsReleaseCalls;
    j["rknnDetectorOutputsReleaseMs"]       = v.rknnDetectorOutputsReleaseMs;
    j["rknnDetectorOutputTransformCalls"]   = v.rknnDetectorOutputTransformCalls;
    j["rknnDetectorOutputTransformMs"]      = v.rknnDetectorOutputTransformMs;
    j["rknnDetectorMutexWaitCalls"]         = v.rknnDetectorMutexWaitCalls;
    j["rknnDetectorMutexWaitMs"]            = v.rknnDetectorMutexWaitMs;
    j["rknnPreprocessFastHits"]             = v.rknnPreprocessFastHits;
    j["rknnRgaFillCalls"]                   = v.rknnRgaFillCalls;
    j["rknnRgaFillMs"]                      = v.rknnRgaFillMs;
    j["rknnRgaResizeColorCalls"]            = v.rknnRgaResizeColorCalls;
    j["rknnRgaResizeColorMs"]               = v.rknnRgaResizeColorMs;
    j["rknnRgaCropResizeCalls"]             = v.rknnRgaCropResizeCalls;
    j["rknnRgaCropResizeMs"]                = v.rknnRgaCropResizeMs;
    j["rknnRgaCropResizeFailures"]          = v.rknnRgaCropResizeFailures;
    j["rknnRgaCropDmaBufFrames"]            = v.rknnRgaCropDmaBufFrames;
    j["rknnRgaCropHostFallbacks"]           = v.rknnRgaCropHostFallbacks;
    j["rknnRgaFailures"]                    = v.rknnRgaFailures;
    j["rknnCpuResizeFallbackCalls"]         = v.rknnCpuResizeFallbackCalls;
    j["rknnCpuResizeFallbackMs"]            = v.rknnCpuResizeFallbackMs;
    j["rknnCpuCropResizeFallbackCalls"]     = v.rknnCpuCropResizeFallbackCalls;
    j["rknnCpuCropResizeFallbackMs"]        = v.rknnCpuCropResizeFallbackMs;
    j["rknnCpuNormalizeFallbackCalls"]      = v.rknnCpuNormalizeFallbackCalls;
    j["rknnCpuNormalizeFallbackMs"]         = v.rknnCpuNormalizeFallbackMs;
    j["rknnNativeInputMapCalls"]            = v.rknnNativeInputMapCalls;
    j["rknnNativeInputMapMs"]               = v.rknnNativeInputMapMs;
    j["rknnNativeInt8Inputs"]               = v.rknnNativeInt8Inputs;
    j["rknnFloatInputs"]                    = v.rknnFloatInputs;
    j["rknnUint8ContractInputs"]            = v.rknnUint8ContractInputs;
    j["rknnInputCompatibilityFallbacks"]    = v.rknnInputCompatibilityFallbacks;
    j["rknnBoundInputBindAttempts"]         = v.rknnBoundInputBindAttempts;
    j["rknnBoundInputBindFailures"]         = v.rknnBoundInputBindFailures;
    j["rknnBoundInputCopyCalls"]            = v.rknnBoundInputCopyCalls;
    j["rknnBoundInputCopyMs"]               = v.rknnBoundInputCopyMs;
    j["rknnBoundInputCopyBytes"]            = v.rknnBoundInputCopyBytes;
    j["rknnBoundInputCopyFailures"]         = v.rknnBoundInputCopyFailures;
    j["rknnBoundInputSyncCalls"]            = v.rknnBoundInputSyncCalls;
    j["rknnBoundInputSyncMs"]               = v.rknnBoundInputSyncMs;
    j["rknnBoundInputSyncFailures"]         = v.rknnBoundInputSyncFailures;
    j["rknnBoundInputFrames"]               = v.rknnBoundInputFrames;
    j["rknnRgaBoundInputBindAttempts"]      = v.rknnRgaBoundInputBindAttempts;
    j["rknnRgaBoundInputBindFailures"]      = v.rknnRgaBoundInputBindFailures;
    j["rknnRgaBoundInputImportCalls"]       = v.rknnRgaBoundInputImportCalls;
    j["rknnRgaBoundInputImportMs"]          = v.rknnRgaBoundInputImportMs;
    j["rknnRgaBoundInputImportFailures"]    = v.rknnRgaBoundInputImportFailures;
    j["rknnRgaBoundInputFrames"]            = v.rknnRgaBoundInputFrames;
    j["rknnRgaBoundUint8Frames"]            = v.rknnRgaBoundUint8Frames;
    j["rknnRgaBoundNativeInt8Frames"]       = v.rknnRgaBoundNativeInt8Frames;
    j["rknnRgaBoundRequantizeCalls"]        = v.rknnRgaBoundRequantizeCalls;
    j["rknnRgaBoundRequantizeMs"]           = v.rknnRgaBoundRequantizeMs;
    j["rknnRgaBoundRequantizeFailures"]     = v.rknnRgaBoundRequantizeFailures;
    j["rknnRgaBoundInputNormalizeBypasses"] = v.rknnRgaBoundInputNormalizeBypasses;
    j["rknnMppDmaBufImportCalls"]           = v.rknnMppDmaBufImportCalls;
    j["rknnMppDmaBufImportMs"]              = v.rknnMppDmaBufImportMs;
    j["rknnMppDmaBufImportFailures"]        = v.rknnMppDmaBufImportFailures;
    j["rknnRgaSmallFrameResizeCalls"]       = v.rknnRgaSmallFrameResizeCalls;
    j["rknnRgaSmallFrameResizeMs"]          = v.rknnRgaSmallFrameResizeMs;
    j["rknnRgaSmallFrameCscCalls"]          = v.rknnRgaSmallFrameCscCalls;
    j["rknnRgaSmallFrameCscMs"]             = v.rknnRgaSmallFrameCscMs;
    j["rknnMppDmaBufFrames"]                = v.rknnMppDmaBufFrames;
    j["rknnMppDmaBufFallbacks"]             = v.rknnMppDmaBufFallbacks;
    j["rknnMppDmaBufSourceBytes"]           = v.rknnMppDmaBufSourceBytes;
    j["rknnNativeInt8Outputs"]              = v.rknnNativeInt8Outputs;
    j["rknnFloatOutputs"]                   = v.rknnFloatOutputs;
    j["rknnOutputCompatibilityFallbacks"]   = v.rknnOutputCompatibilityFallbacks;
    j["rknnNativeOutputBytes"]              = v.rknnNativeOutputBytes;
    j["rknnFloatOutputBytes"]               = v.rknnFloatOutputBytes;
    j["rknnYolov8DflCalls"]                 = v.rknnYolov8DflCalls;
    j["rknnYolov8DflMs"]                    = v.rknnYolov8DflMs;
    j["rknnYolov8ClassCalls"]               = v.rknnYolov8ClassCalls;
    j["rknnYolov8ClassMs"]                  = v.rknnYolov8ClassMs;
    j["rknnYolov8DirectCandidateCalls"]     = v.rknnYolov8DirectCandidateCalls;
    j["rknnYolov8DirectCandidateFailures"]  = v.rknnYolov8DirectCandidateFailures;
    j["rknnYolov8DirectPointsScanned"]      = v.rknnYolov8DirectPointsScanned;
    j["rknnYolov8DirectPointsDecoded"]      = v.rknnYolov8DirectPointsDecoded;
    j["rknnYolov8ScoreSumPointsRejected"]   = v.rknnYolov8ScoreSumPointsRejected;
    j["rknnYolov8LogicalFloatBytesAvoided"] = v.rknnYolov8LogicalFloatBytesAvoided;
    j["yolov8PostprocessCalls"]             = v.yolov8PostprocessCalls;
    j["yolov8PostprocessMs"]                = v.yolov8PostprocessMs;
    j["yolov8NmsCalls"]                     = v.yolov8NmsCalls;
    j["yolov8NmsMs"]                        = v.yolov8NmsMs;
}

void from_json(const nlohmann::json& j, MsgDiskInfo& v) {
    JSON_OPT(j, v, disktotal);
    JSON_OPT(j, v, diskavailable);
}

void to_json(nlohmann::json& j, const MsgDiskInfo& v) {
    j["disktotal"]     = v.disktotal;
    j["diskavailable"] = v.diskavailable;
}

void from_json(const nlohmann::json& j, MsgNetInfo& v) {
    JSON_OPT(j, v, networkupperrate);
    JSON_OPT(j, v, networkdownwardrate);
}

void to_json(nlohmann::json& j, const MsgNetInfo& v) {
    j["networkupperrate"]    = v.networkupperrate;
    j["networkdownwardrate"] = v.networkdownwardrate;
}

void from_json(const nlohmann::json& j, MsgHwInfo& v) {
    JSON_OPT(j, v, cpuusage);
    JSON_OPT(j, v, memoryinfo);
    JSON_OPT(j, v, gpuinfo);
    JSON_OPT(j, v, diskinfo);
    JSON_OPT(j, v, netinfo);
}

void to_json(nlohmann::json& j, const MsgHwInfo& v) {
    j["cpuusage"]   = v.cpuusage;
    j["memoryinfo"] = v.memoryinfo;
    j["gpuinfo"]    = v.gpuinfo;
    j["diskinfo"]   = v.diskinfo;
    j["netinfo"]    = v.netinfo;
}

void from_json(const nlohmann::json& j, ActionStatus& v) {
    JSON_OPT(j, v, statusCode);
    JSON_OPT(j, v, statusDesc);
    JSON_OPT(j, v, statusDescKey);
    JSON_OPT(j, v, actionId);
    JSON_OPT(j, v, name);
    JSON_OPT(j, v, holdCount);
    JSON_OPT(j, v, alarmCount);
    JSON_OPT(j, v, insertCount);
    JSON_OPT(j, v, processCount);
    JSON_OPT(j, v, discardCount);
    JSON_OPT(j, v, periodMs);
    JSON_OPT(j, v, insertCountPeriod);
    JSON_OPT(j, v, processCountPeriod);
    JSON_OPT(j, v, discardCountPeriod);
}

void to_json(nlohmann::json& j, const ActionStatus& v) {
    j["statusCode"] = v.statusCode;
    j["statusDesc"] = v.statusDesc;
    if (!v.statusDescKey.empty())
        j["statusDescKey"] = v.statusDescKey;
    j["actionId"]           = v.actionId;
    j["name"]               = v.name;
    j["holdCount"]          = v.holdCount;
    j["alarmCount"]         = v.alarmCount;
    j["insertCount"]        = v.insertCount;
    j["processCount"]       = v.processCount;
    j["discardCount"]       = v.discardCount;
    j["periodMs"]           = v.periodMs;
    j["insertCountPeriod"]  = v.insertCountPeriod;
    j["processCountPeriod"] = v.processCountPeriod;
    j["discardCountPeriod"] = v.discardCountPeriod;
}

}  // namespace cosmo
