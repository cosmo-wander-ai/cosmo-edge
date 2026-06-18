// Action code constants — algorithm/action identifiers shared across layers.
// Canonical location: util/dto/ActionCodes.h
//
// These constants define the unique IDs and display names for all atomic,
// business, and picture algorithm actions in the pipeline system.
// Both service and flow layers reference these identifiers.

#pragma once

#include <string_view>

namespace cosmo {

// ── Video atomic actions ──
inline constexpr std::string_view AADetect_Code         = "AA_00001";
inline constexpr std::string_view AADetect_Name         = "原子检测";
inline constexpr std::string_view AATrack_Code          = "AA_00003";
inline constexpr std::string_view AATrack_Name          = "原子追踪";
inline constexpr std::string_view AAClassify_Code       = "AA_00002";
inline constexpr std::string_view AAClassify_Name       = "原子分类";
inline constexpr std::string_view AALandmark_Code       = "AA_00004";
inline constexpr std::string_view AALandmark_Name       = "原子Landmark";
inline constexpr std::string_view AARecognizer_Code     = "AA_00005";
inline constexpr std::string_view AARecognizer_Name     = "原子特征提取";
inline constexpr std::string_view AAPersonFace_Code     = "AA_00006";
inline constexpr std::string_view AAPersonFace_Name     = "人体人脸检测";
inline constexpr std::string_view AAFilter_Code         = "AA_00007";
inline constexpr std::string_view AAFilter_Name         = "滤波";
inline constexpr std::string_view AACluster_Code        = "AA_00008";
inline constexpr std::string_view AACluster_Name        = "聚类";
inline constexpr std::string_view AAFightClassify_Code  = "AA_00009";
inline constexpr std::string_view AAFightClassify_Name  = "打架分类";
inline constexpr std::string_view AAVideoDiagnosis_Code = "AA_00010";
inline constexpr std::string_view AAVideoDiagnosis_Name = "视频诊断";

inline constexpr std::string_view AAOcr_Code = "AA_00011";
inline constexpr std::string_view AAOcr_Name = "文字识别";

inline constexpr std::string_view AAIrCheck_Code = "AA_00012";
inline constexpr std::string_view AAIrCheck_Name = "图片颜色模式";

// ── Large model actions ──
inline constexpr std::string_view DADinoDetect_Code  = "DA_00001";
inline constexpr std::string_view DADinoDetect_Name  = "检测视觉大模型";
inline constexpr std::string_view DASam2Segment_Code = "DA_00002";
inline constexpr std::string_view DASam2Segment_Name = "分割视觉大模型";
inline constexpr std::string_view DAQwen3VL_Code     = "DA_00003";
inline constexpr std::string_view DAQwen3VL_Name     = "语言视觉大模型";

// ── Video classify variants ──
inline constexpr std::string_view AAClassifyGroup_Code   = "AA_10002";
inline constexpr std::string_view AAClassifyGroup_Name   = "分组分类";
inline constexpr std::string_view AAClassifyArea_Code    = "AA_20002";
inline constexpr std::string_view AAClassifyArea_Name    = "区域分类";
inline constexpr std::string_view AAClassifyAttr_Code    = "AA_30002";
inline constexpr std::string_view AAClassifyAttr_Name    = "属性分类";
inline constexpr std::string_view AAClassifyMultPic_Code = "AA_40002";
inline constexpr std::string_view AAClassifyMultPic_Name = "相机移动分类";

// ── Video business actions ──
inline constexpr std::string_view BAStreamChannel_Code = "BA_00001";
inline constexpr std::string_view BAStreamChannel_Name = "业务流通道";
inline constexpr std::string_view BAStreamChannel_Desc = "ChannelAction";
inline constexpr std::string_view BAFilter_Code        = "BA_00002";
inline constexpr std::string_view BAFilter_Name        = "业务过滤";

inline constexpr std::string_view BALogicalJudgment_Code = "BA_90001";
inline constexpr std::string_view BALogicalJudgment_Name = "逻辑判断";

inline constexpr std::string_view BAActionBranch_Code = "BA_90002";
inline constexpr std::string_view BAActionBranch_Name = "分支判断";

inline constexpr std::string_view BASensitivity_Code         = "BA_00003";
inline constexpr std::string_view BAFixCountSensitivity_Code = "BA_10003";
inline constexpr std::string_view BASensitivity_Name         = "灵敏度计算";

inline constexpr std::string_view BAPositiveSaveSensitivity_Code = "BA_20003";
inline constexpr std::string_view BAPositiveSaveSensitivity_Name = "灵敏度计算-周期内灵敏度都不达标才上报";

inline constexpr std::string_view BATaskAlarm_Code = "BA_00004";
inline constexpr std::string_view BATaskAlarm_Name = "告警";

inline constexpr std::string_view BATaskFaceAlarm_Code = "BA_10004";
inline constexpr std::string_view BATaskFaceAlarm_Name = "行为类的人脸比对告警";

inline constexpr std::string_view BATaskCollect_Code = "BA_10000";
inline constexpr std::string_view BATaskCollect_Name = "采集";

inline constexpr std::string_view BAAreaAlarm_Code = "BA_00005";
inline constexpr std::string_view BAAreaAlarm_Name = "区域判断";

inline constexpr std::string_view BAFaceLogic_Code = "BA_00006";
inline constexpr std::string_view BAFaceLogic_Name = "人脸抓拍逻辑";

inline constexpr std::string_view BAFriendDistance_Code = "BA_00007";
inline constexpr std::string_view BAFriendDistance_Name = "队友距离判断";

inline constexpr std::string_view BAFilterLogic_Code = "BA_00008";
inline constexpr std::string_view BAFilterLogic_Name = "滤波状态判断";

inline constexpr std::string_view BAAssoTarget_Code = "BA_00009";
inline constexpr std::string_view BAAssoTarget_Name = "目标关联";

inline constexpr std::string_view BATargetChooseBest_Code = "BA_00010";
inline constexpr std::string_view BATargetChooseBest_Name = "目标选优";

inline constexpr std::string_view GADetectTrack_Code = "GA001";
inline constexpr std::string_view GADetectTrack_Name = "组合检测追踪";

// ── Picture atomic actions ──
inline constexpr std::string_view PADetect_Code = "PA_00001";
inline constexpr std::string_view PADetect_Name = "原子检测";

inline constexpr std::string_view PAClassify_Code = "PA_00002";
inline constexpr std::string_view PAClassify_Name = "原子分类";

inline constexpr std::string_view PALandmark_Code = "PA_00004";
inline constexpr std::string_view PALandmark_Name = "原子Landmark";

inline constexpr std::string_view PARecognizer_Code = "PA_00005";
inline constexpr std::string_view PARecognizer_Name = "原子特征提取";

inline constexpr std::string_view PALogicalJudgment_Code = "PB_90001";
inline constexpr std::string_view PALogicalJudgment_Name = "逻辑判断";

inline constexpr std::string_view PDADino_Code    = "PDA_00001";
inline constexpr std::string_view PDASam_Code     = "PDA_00002";
inline constexpr std::string_view PDAQwen3VL_Code = "PDA_00003";

}  // namespace cosmo
