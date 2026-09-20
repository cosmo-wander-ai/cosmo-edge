// Algorithm Detection and Task Types definitions

#pragma once

#include <memory>
#include <string>
#include <vector>

#include "flow/common/AlgAlarmTypes.h"
#include "flow/common/AlgDataType.h"
#include "infer/AiCommon.h"
#include "media/NativeVideoBuffer.h"
#include "media/VideoFrame.h"
#include "media/VideoPacket.h"
#include "util/Rect.h"
#include "util/VideoInfo.h"

namespace cosmo {

struct AlgChannelDataOrig {
    float fps{0.0};
    VideoPacketPtr packet;
};

struct AlgChannelDataDec {
    VideoFramePtr frame;  // AI processed frame, might be YUV or BGR depending on platform
    media::NativeVideoBufferPtr native_buffer;  // Optional borrowed hardware inference source.
    int64_t reportTimeStamp{0};
};

struct DataDetTrackClassify {
    bool bHaveArea{false};
    bool bHaveShieldedArea{false};
    int64_t streamIndex{0};
    int64_t frameIndex{0};
    int64_t timestamp{0};
    int picWidth{media::kVideoDefaultWidth};
    int picHeight{media::kVideoDefaultHeight};
    AlgDataType dataType{AlgDataType::ChannelDataDetect};

    OnEventsReportType reportType{OnEventsReportType::Realtime};
    AlgTaskDataFaceLogicAreaInfo areaInfo;  // Effective only when OnEventsReportTypeTrigger

    std::vector<AiDetectRstEl> targets;
    bool targetHaveMultRelated{false};
    std::vector<AiGroupEl> groupTargets;
    // False means an upstream inference failed, not that every target disappeared.
    bool observation_complete{true};
    std::string recognition_context;  // Face sets and threshold used for these observations.
    std::string logic_context;        // Judgment node and parameter revision; empty before judgment.
};
using DataDetTrackClassifyPtr = std::shared_ptr<DataDetTrackClassify>;

struct AlgChannelDataDetect {
    std::vector<std::string> lables;
    std::string atomicCode;
    std::vector<std::string> atomicCodes;
    DataDetTrackClassifyPtr detRet{nullptr};
};

struct AlgTaskDataTrack {
    DataDetTrackClassifyPtr trackRst{nullptr};
};

struct AlgTaskDataClassify {
    DataDetTrackClassifyPtr classifyRst{nullptr};
};

struct AlgTaskDataFaceLogic {
    DataDetTrackClassifyPtr faceLogicRst{nullptr};
};

struct AlgTaskDataLandmark {
    DataDetTrackClassifyPtr landmarkRst{nullptr};
};

struct AlgTaskDataFriendDistance {
    DataDetTrackClassifyPtr friendsRst{nullptr};
};

struct AlgTaskDataAssoTarget {
    DataDetTrackClassifyPtr assoRst{nullptr};
};

struct AlgTaskDataClassifyMultPic {
    VideoFramePtr baseFrame;
    DataDetTrackClassifyPtr classifyRst{nullptr};
};

struct AlgTaskDataRecogThings {
    std::string areaId;
    std::string areaName;
    util::Box box;
    bool bLogicResult{false};  // Reused by filtering module. true: moving, false: stationary
    float average{-1.0};       // Filtering result
    AIMotionState motionStatus{AIMotionState::UNCERTAIN};  // Filtering motion state
    AiDetectMatchHighScoreInfo matchInfo;  // Highest score match info during Reid algorithm comparison
};

struct AlgTaskDataRecog {
    std::vector<AlgTaskDataRecogThings> areas;
};

struct AlgTaskDataAiFilter {
    bool bInputArea{false};
    std::vector<AlgTaskDataRecogThings> areas;   // Effective only when bInputArea is true
    DataDetTrackClassifyPtr targetRst{nullptr};  // Effective when bInputArea is false
};

struct AlgTaskDataAiVideoQuality {
    DataDetTrackClassifyPtr targetRst{nullptr};  // Effective when bInputArea is false
};

struct AlgTaskDataFilter {
    DataFilterPtr filterRst{nullptr};
};

struct AlgTaskDataAlarm {
    DataAlarmPtr alarmData{nullptr};
};

}  // namespace cosmo
