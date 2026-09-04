#pragma once

#include <string_view>

namespace cosmo::key {

inline constexpr std::string_view DEBUG_STRING{"Cosmo-Debug"};
inline constexpr std::string_view DEFAULT_AREA{"-9,9,9-=;'.;"};
inline constexpr std::string_view DEFAULT_AREA_NAME{"NOAREA"};
inline constexpr std::string_view CHANNEL_URL{"url"};
inline constexpr std::string_view CHANNEL_SOURCE_REPEAT{"param.videoRepeatCount"};
inline constexpr std::string_view CHANNEL_SOURCE_FPS{"param.videoReadFps"};
inline constexpr std::string_view FPS{"fps"};
inline constexpr std::string_view ATOM_CODE{"atomicCode"};
inline constexpr std::string_view PARAM{"param"};
inline constexpr std::string_view FILTER{"filter"};
inline constexpr std::string_view SIZE{"size"};
inline constexpr std::string_view SIDE{"side"};
inline constexpr std::string_view CONFIDENCE{"confidence"};
inline constexpr std::string_view CONFIDENCE_CONFIG{"confidenceConfig"};
inline constexpr std::string_view DET_POSITION{"detPostion"};
inline constexpr std::string_view MOTION_STATUS{"motionStatus"};
inline constexpr std::string_view SHAPE_CHANGE_STATUS{"shapeChangeStatus"};
inline constexpr std::string_view MOTION{"motion"};
inline constexpr std::string_view FRAMES{"frames"};
inline constexpr std::string_view DYNAMIC_MATCH{"trackDynamicMatch"};
inline constexpr std::string_view SHAPE_CHANGE_THRESHOLD{"shapeChangeThreshold"};
inline constexpr std::string_view MOVE{"move"};
inline constexpr std::string_view STATIC{"static"};
inline constexpr std::string_view FEATURE_INPUT{"featureInput"};
inline constexpr std::string_view MATCH_FLAG{"matchFlag"};
inline constexpr std::string_view INPUT_AREA_TYPE{"inputAreaType"};
inline constexpr std::string_view TARGET_COUNT_CHANGE{"targetCountChange"};
inline constexpr std::string_view MAX{"max"};
inline constexpr std::string_view MIN{"min"};
inline constexpr std::string_view AI_PARAM{"aiParam"};
inline constexpr std::string_view CUSTOM{"custom"};
inline constexpr std::string_view SENSITIVITY{"sensitivity"};
inline constexpr std::string_view DETECTION_DURATION_MS{"detectionDurationMs"};
inline constexpr std::string_view DETECTION_DURATION{"detectionDuration"};
inline constexpr std::string_view SEN_HIT_COUNT{"senHitCount"};
inline constexpr std::string_view SEN_TOTAL_COUNT{"senTotalCount"};
inline constexpr std::string_view FILTER_TO_DEN{"filterToDen"};
inline constexpr std::string_view LINES{"lines"};
inline constexpr std::string_view BREAK_LINE_TYPE{"breakLineType"};
inline constexpr std::string_view TRIPPING_WIRE_TYPE{"trippingWireType"};
inline constexpr std::string_view TARGET_CALC_TYPE{"targetCalcType"};
inline constexpr std::string_view RETRO_DIRECT{"retroDirect"};
inline constexpr std::string_view RETRO_DISTANCE{"retroDistance"};
inline constexpr std::string_view DIRECTION_TYPE{"directionType"};

namespace alg {
    inline constexpr std::string_view ACTION{"configObject"};
    inline constexpr std::string_view ACTION_ROOT_VALUE{"-1"};
}  // namespace alg

namespace pos_sen {
    inline constexpr std::string_view DURATION_TIME{"posSenDurationTime"};
    inline constexpr std::string_view DURATION_TIME_TYPE{"posSenDurationTimeType"};
    inline constexpr std::string_view HIT_COUNT{"posSenHitCount"};
    inline constexpr std::string_view TOTAL_COUNT{"posSenTotalCount"};
    inline constexpr std::string_view REAL_TIME_ENABLE{"PosSenRealTimeEnable"};
}  // namespace pos_sen

namespace alarm {
    inline constexpr std::string_view INTERVAL{"alarmInterval"};
    inline constexpr std::string_view TARGET_INTERVAL{"targetAlarmInterval"};
    inline constexpr std::string_view TARGET_COUNT{"targetAlarmCount"};
    inline constexpr std::string_view RESTRAIN_SWITCH{"restrainSwitch"};
    inline constexpr std::string_view OVERLAP_RATE{"overlapRate"};
    inline constexpr std::string_view RESTRAIN_TIME{"restrainTime"};
    inline constexpr std::string_view OVERLAY_TRAJECTORY{"overlayTrajectory"};
    inline constexpr std::string_view TASK_ID{"taskId"};
    inline constexpr std::string_view VIDEO_CHANNEL_ID{"videoChannelId"};
    inline constexpr std::string_view POLLING_VIDEO_CHANNEL_ID{"param.videoChannelId"};
    inline constexpr std::string_view RT_EVENT_RECORD{"realtimeEventRecordType"};
    inline constexpr std::string_view TR_EVENT_RECORD{"triggerEventRecordType"};
    inline constexpr std::string_view PROPERTY{"alarmProperty"};
}  // namespace alarm

namespace target {
    inline constexpr std::string_view FACE_SET{"faceSet"};
    inline constexpr std::string_view LIMIT_SCORE{"limitScore"};
    inline constexpr std::string_view MATCH_FLAG{"matchFlag"};
    inline constexpr std::string_view STRANGER_CONFIRM_COUNT{"strangerConfirmCount"};
    inline constexpr std::string_view PARAM_WORKCLOTHES_SET{"param.workClothesSet"};
    inline constexpr std::string_view PARAM_COMMODITY_SET{"param.commoditySet"};
}  // namespace target

namespace area {
    inline constexpr std::string_view ALARM_TYPE{"areaAlarmType"};
    inline constexpr std::string_view LIMIT_TARGET_COUNT{"areaLimitTargetCount"};
    inline constexpr std::string_view LIMIT_TARGET_TYPE{"areaLimitTargetType"};
    inline constexpr std::string_view DURATION{"areaDuration"};
    inline constexpr std::string_view LIMIT_DURATION{"areaLimitDuration"};
    inline constexpr std::string_view CALC_DURATION{"areaCalcDuration"};
    inline constexpr std::string_view DURATION_TIME_TYPE{"areaDurationTimeType"};
    inline constexpr std::string_view LIMIT_DURATION_TIME_TYPE{"areaLimitDurationTimeType"};
    inline constexpr std::string_view CALC_DURATION_TIME_TYPE{"areaCalcDurationTimeType"};
    inline constexpr std::string_view BREAK_AREA_TYPE{"breakAreaType"};
    inline constexpr std::string_view COUNT_BREAK_AREA_TYPE{"countBreakAreaType"};
    inline constexpr std::string_view DETECT_BREAK_AREA_TYPE{"detectBreakAreaType"};
    inline constexpr std::string_view DURATION_BREAK_AREA_TYPE{"durationBreakAreaType"};
    inline constexpr std::string_view SENSITIVITY{"areaSensitivity"};
}  // namespace area

namespace llm {
    inline constexpr std::string_view REVIEW{"enableLlmReview"};
    inline constexpr std::string_view ATOMIC_CODE{"llmAtomicCode"};
    inline constexpr std::string_view REVIEW_CONTENT{"llmReviewContent"};
    inline constexpr std::string_view PROVIDER{"llmProvider"};
    inline constexpr std::string_view OPENAI_BASE_URL{"llmOpenaiBaseUrl"};
    inline constexpr std::string_view OPENAI_API_KEY{"llmOpenaiApiKey"};
    inline constexpr std::string_view OPENAI_MODEL{"llmOpenaiModel"};
    inline constexpr std::string_view OPENAI_ENDPOINT{"llmOpenaiEndpoint"};
    inline constexpr std::string_view OPENAI_TIMEOUT_MS{"llmOpenaiTimeoutMs"};
    inline constexpr std::string_view OPENAI_MAX_TOKENS{"llmOpenaiMaxTokens"};
}  // namespace llm

namespace capture {
    inline constexpr std::string_view STRATEGY{"captureStrategy"};
    inline constexpr std::string_view REALTIME_DURATION{"realtimeDuration"};
    inline constexpr std::string_view QUALITY{"captureQuality"};
    inline constexpr std::string_view ANGLES{"captureAngles"};
    inline constexpr std::string_view IN_OUT_IMG{"inOutPicture"};
}  // namespace capture

namespace group {
    inline constexpr std::string_view TYPE{"groupType"};
    inline constexpr std::string_view AREA_TARGET_COUNT{"groupAreaTargetCount"};
}  // namespace group

namespace face {
    inline constexpr std::string_view SCALE_SIDE{"faceScaleSide"};
}  // namespace face

namespace diagnosis {
    inline constexpr std::string_view VIDEO_DIAGNOSIS{"aivideoDiagnosis"};
    inline constexpr std::string_view TYPE{"videoDiagnosisType"};
    inline constexpr std::string_view THRESHOLD{"videoDiagnosisThreshold"};
    inline constexpr std::string_view THRESHOLD_EXT{"videoDiagnosisThresholdExt"};
}  // namespace diagnosis

}  // namespace cosmo::key
