// Forward declarations for cosmo types used across service, flow, and infer layers.
// Include this in headers where types are only used by reference/pointer
// in pure virtual declarations or member pointers.

#pragma once

#include <memory>

namespace cosmo {

// ---- Algorithm Types ----
struct ActionAlg;
using ActionAlgPtr = std::shared_ptr<ActionAlg>;

// ---- Camera Related ----
struct MsgCameraAttr;
struct MsgCameraInfo;

// ---- Task Related ----
struct MsgTaskConfig;
struct MsgTaskArea;
struct MsgTarget;
struct MsgTaskCreateRecv;
struct MsgTaskCreateSend;
struct MsgTaskCancleRecv;
struct MsgTaskCancleSend;
struct MsgOverviewMem;
struct MsgPTaskDetectPicRecv;
struct MsgPTaskDetectPicSend;
struct MsgPTaskCreateRecv;
struct MsgPTaskCreateSend;
struct MsgPTaskCancleRecv;
struct MsgPTaskCancleSend;
struct MsgDetectRecv;
struct MsgDetectSend;

// ---- Schedule ----
struct MsgScheduleTemplate;

// ---- Linkage ----
struct LinkageStrategyOutputUnit;
struct StorageList;

// ---- Event ----
struct MsgConditionEvent;
struct MsgEventUnit;

// ---- Face Library ----
struct MsgBaseFaceLibInfo;
struct MsgResultFaceLibInfo;
struct MsgQueryFacesR;
struct MsgResultInfo;
struct AiFeature;

// ---- System Info ----
struct MsgDiskInfo;
struct MsgGpuInfo;
struct MsgMemoryInfo;
struct MsgNetInfo;

// ---- Dynamic Key-Value ----
struct MsgDynamicKeyValue;

// ---- Detect Msg ----
struct MsgGetFeaturesSend;
struct MsgGetFeaturesFeature;
struct MsgGetFeaturesRecv;
struct MsgGetFeaturesImage;

}  // namespace cosmo
