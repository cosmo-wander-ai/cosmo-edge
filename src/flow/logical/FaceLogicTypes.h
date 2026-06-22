// FaceLogicTypes.h — Private type definitions for FaceLogic.
#pragma once

#include <chrono>
#include <map>
#include <string>

#include "flow/logical/FaceLogic.h"
#include "util/dto/ServerMsgTypes.h"

struct cosmo::FaceLogic::TrackIdData {
    struct FaceLogicPicInfo {
        bool is_init{false};
        std::string area_name;
        VideoFramePtr pic_frame;
        AiDetectRstEl pic_info;
    };
    unsigned track_id{0};
    std::string track_id_uuid;
    bool is_detected{false};
    VideoFramePtr pic_frame;
    AiDetectRstEl det_rst;
    bool has_report{false};
    std::chrono::steady_clock::time_point last_report_timepoint;

    // Maximum quality of the best picture
    float best_pic_max_quality{-1.0};
    // Best quality picture
    FaceLogicPicInfo best_pic;

    // Best picture quality in real-time
    float real_time_pic_max_quality{-1.0};
    // Timepoint for pushing the best real-time picture
    std::chrono::steady_clock::time_point real_time_pic_timepoint;
    // Best quality picture in real-time
    FaceLogicPicInfo real_time_pic;

    // Pictures entering the area
    std::map<std::string, FaceLogicPicInfo> into_area_pics;
};
