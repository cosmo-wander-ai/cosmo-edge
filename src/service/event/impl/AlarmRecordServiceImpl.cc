// AlarmRecordServiceImpl — Alarm record service — manages task event, face event, and

#include "service/event/impl/AlarmRecordServiceImpl.h"

#include <SQLiteCpp/Exception.h>

#include <filesystem>
#include <thread>

#include "service/detail/ServiceRegistry.h"
#include "service/event/AlarmExport.h"
#include "service/infra/IDbService.h"
#include "util/Exec.h"
#include "util/JsonStructUtil.h"
#include "util/Log.h"
#include "util/PathUtil.h"
#include "util/TimingConstants.h"

namespace {

// ── DTO → DAO conversions ────────────────────────────────────────────

cosmo::db::QueryTaskEventCondition ToDao(const cosmo::service::AlarmQueryCondition& dto) {
    cosmo::db::QueryTaskEventCondition dao;
    dao.time_begin            = dto.timeBegin;
    dao.time_end              = dto.timeEnd;
    dao.camera_name           = dto.cameraName;
    dao.area_id               = dto.areaId;
    dao.track_id              = dto.trackId;
    dao.id                    = dto.id;
    dao.algorithm_codes       = dto.algorithmCodes;
    dao.categories            = dto.categorys;
    dao.report_status         = dto.reportStatus;
    dao.is_export_total_count = dto.bExportTotalCount;
    dao.page_num              = dto.pageNum;
    dao.page_size             = dto.pageSize;
    dao.lib_person_name       = dto.libPersionName;
    dao.lib_person_number     = dto.libPersionNumber;
    dao.lib_faces_id          = dto.libFacesID;
    dao.prop_color            = dto.propColor;
    dao.prop_related_color    = dto.propRelatedColor;
    dao.prop_type             = dto.propType;
    dao.prop_direction        = dto.propDirection;
    return dao;
}

cosmo::db::PassengerFlowCondition ToDao(const cosmo::service::FlowQueryCondition& dto) {
    cosmo::db::PassengerFlowCondition dao;
    dao.camera_id      = dto.cameraId;
    dao.algorithm_code = dto.algorithm_code;
    dao.type           = static_cast<cosmo::db::PassengerFlowConditionType>(dto.type);
    dao.start_hour     = dto.startHour;
    dao.end_hour       = dto.endHour;
    dao.reported       = dto.reported;
    return dao;
}

// ── DAO → DTO conversions ────────────────────────────────────────────

cosmo::service::AlarmEventRecord ToDto(cosmo::db::TaskEventData&& dao) {
    cosmo::service::AlarmEventRecord dto;
    dto.id               = std::move(dao.id);
    dto.category         = std::move(dao.category);
    dto.algorithm_code   = std::move(dao.algorithm_code);
    dto.timestamp        = dao.timestamp;
    dto.cameraId         = std::move(dao.camera_id);
    dto.cameraOutId      = std::move(dao.camera_out_id);
    dto.cameraName       = std::move(dao.camera_name);
    dto.areaId           = std::move(dao.area_id);
    dto.areaName         = std::move(dao.area_name);
    dto.diskId           = std::move(dao.disk_id);
    dto.reportStatus     = dao.report_status;
    dto.trackId          = std::move(dao.track_id);
    dto.detectedPicUrl   = std::move(dao.detected_pic_url);
    dto.fullPicUrl       = std::move(dao.full_pic_url);
    dto.origPicUrl       = std::move(dao.orig_pic_url);
    dto.videoFlag        = dao.video_flag;
    dto.tarX             = dao.tar_x;
    dto.tarY             = dao.tar_y;
    dto.tarWidth         = dao.tar_width;
    dto.tarHeight        = dao.tar_height;
    dto.eventFrame       = dao.event_frame;
    dto.extraFiles       = std::move(dao.extra_files);
    dto.property         = std::move(dao.property);
    dto.libPersionName   = std::move(dao.lib_person_name);
    dto.libPersionNumber = std::move(dao.lib_person_number);
    dto.libFacesID       = std::move(dao.lib_faces_id);
    dto.propStr          = std::move(dao.prop_str);
    dto.propColor        = std::move(dao.prop_color);
    dto.propRelatedColor = std::move(dao.prop_related_color);
    dto.propType         = std::move(dao.prop_type);
    dto.propDirection    = std::move(dao.prop_direction);
    return dto;
}

cosmo::service::AlarmQueryResult ToDto(cosmo::db::TaskEventsResult&& dao) {
    cosmo::service::AlarmQueryResult dto;
    dto.totalCount      = dao.total_count;
    dto.exportResultUrl = std::move(dao.export_result_url);
    dto.behaviorList.reserve(dao.behavior_list.size());
    for (auto& item : dao.behavior_list) {
        dto.behaviorList.push_back(ToDto(std::move(item)));
    }
    return dto;
}

cosmo::service::FlowQueryResult ToDto(cosmo::db::PassengerFlowResult&& dao) {
    cosmo::service::FlowQueryResult dto;
    dto.totalCount = dao.total_count;
    dto.numberList.reserve(dao.number_list.size());
    for (auto& item : dao.number_list) {
        cosmo::service::FlowTimePoint tp;
        tp.hour           = item.hour;
        tp.timeString     = std::move(item.time_string);
        tp.cameraId       = std::move(item.camera_id);
        tp.algorithm_code = std::move(item.algorithm_code);
        tp.enterNumber    = item.enter_number;
        tp.leaveNumber    = item.leave_number;
        dto.numberList.push_back(std::move(tp));
    }
    return dto;
}

}  // anonymous namespace

namespace cosmo::service {

AlarmRecordServiceImpl::AlarmRecordServiceImpl()
    : db_event_(std::make_shared<cosmo::db::TaskEventDao>(
          *service::ServiceRegistry::Instance().Get<service::IDbService>().GetDb())) {
    db_event_->CreateTable();

    db_face_event_ = std::make_shared<cosmo::db::FaceTaskEventDao>(
        *service::ServiceRegistry::Instance().Get<service::IDbService>().GetDb());
    db_face_event_->CreateTable();

    db_pass_flow_event_ = std::make_shared<cosmo::db::PassengerFlowDao>(
        *service::ServiceRegistry::Instance().Get<service::IDbService>().GetDb());
    db_pass_flow_event_->CreateTable();

    LOG_INFO("{}", "AlarmRecordService Init");
}

AlarmRecordServiceImpl::~AlarmRecordServiceImpl() {
    LOG_INFO("{}", "AlarmRecordService Delete");
}

cosmo::db::TaskEventData AlarmRecordServiceImpl::AlarmDataToEventData(cosmo::AlarmRecordUnit& unit) {
    cosmo::db::TaskEventData data;
    data.id                = unit.id;
    data.category          = unit.category;
    data.algorithm_code    = unit.algorithmId;
    data.timestamp         = unit.timestamp;
    data.camera_id         = unit.videoChannelId;
    data.camera_name       = unit.channelName;
    data.area_id           = unit.areaId;
    data.area_name         = unit.areaName;
    data.disk_id           = unit.diskId;
    data.track_id          = unit.trackId;
    data.video_flag        = static_cast<int>(unit.videoFlag);
    data.extra_files       = unit.extraFiles;
    data.property          = unit.property;
    data.event_frame       = unit.eventFrame;
    data.lib_faces_id      = unit.libFacesID;
    data.lib_person_name   = unit.libPersionName;
    data.lib_person_number = unit.libPersionNumber;

    data.prop_str           = unit.propStr;
    data.prop_color         = unit.propColor;
    data.prop_related_color = unit.propRelatedColor;
    data.prop_type          = unit.propType;
    data.prop_direction     = unit.propDirection;

    return data;
}

cosmo::db::FaceTaskEventData AlarmRecordServiceImpl::AlarmDataToFaceEventData(
    cosmo::AlarmRecordUnit& /*unit*/) {
    cosmo::db::FaceTaskEventData data;

    return data;
}

bool AlarmRecordServiceImpl::Insert(cosmo::AlarmRecordUnit& unit) {
    auto data = AlarmDataToEventData(unit);
    try {
        return db_event_->Insert(data);
    } catch (const SQLite::Exception&) {
        std::this_thread::sleep_for(timing::kSlowPollInterval);
        try {
            return db_event_->Insert(data);
        } catch (const SQLite::Exception& e) {
            LOG_WARN("Insert:{} catch error:{}", unit.id, e.what());
        }
    }
    return false;
}

bool AlarmRecordServiceImpl::InsertFace(cosmo::AlarmRecordUnit& unit) {
    auto data = AlarmDataToFaceEventData(unit);
    return db_face_event_->Insert(data);
}

std::vector<cosmo::MsgEventUnit> AlarmRecordServiceImpl::QueryEvents(cosmo::MsgConditionEvent& condition,
                                                                     int64_t& total) {
    std::vector<cosmo::MsgEventUnit> events;
    cosmo::db::QueryTaskEventCondition condition_db;
    condition_db.time_begin      = condition.timeBegin;
    condition_db.time_end        = condition.timeEnd;
    condition_db.algorithm_codes = condition.algorithmCodes;
    condition_db.categories      = condition.categorys;
    condition_db.camera_name     = condition.videoChannelName;
    condition_db.page_num        = condition.pageNum;
    condition_db.page_size       = condition.pageSize;
    condition_db.report_status   = condition.reportStatus;

    condition_db.lib_person_name   = condition.personName;    // Person name
    condition_db.lib_person_number = condition.personCode;    // Person code
    condition_db.lib_faces_id      = condition.matchLibName;  // Matched face lib

    condition_db.prop_color         = condition.propColor;         // Vehicle body color
    condition_db.prop_related_color = condition.propRelatedColor;  // License plate color
    condition_db.prop_type          = condition.propType;          // Vehicle type
    condition_db.prop_direction     = condition.propDirection;     // Vehicle direction

    auto rets = db_event_->Query(condition_db);
    total     = static_cast<int64_t>(rets.total_count);
    for (auto& el : rets.behavior_list) {
        cosmo::MsgEventUnit unit;
        unit.id             = el.id;
        unit.videoChannelId = el.camera_id;
        unit.channelCode    = el.camera_out_id;
        unit.channelName    = el.camera_name;
        unit.timestamp      = el.timestamp;
        unit.category       = el.category;
        unit.algorithmCode  = el.algorithm_code;
        unit.areaId         = el.area_id;
        unit.areaName       = el.area_name;
        unit.reportStatus   = el.report_status;

        std::vector<std::string> files;
        if (!cosmo::util::DecodeJson(el.extra_files, files)) {
            LOG_WARN("Failed to decode extraFiles for event {}", el.id);
        }
        std::string file_path           = cosmo::path::GetEventWebPath(el.timestamp);
        std::string local_file_path     = cosmo::path::GetEventPath(el.timestamp);
        std::string file_path_pre       = (std::filesystem::path(file_path) / el.id).string();
        std::string local_file_path_pre = (std::filesystem::path(local_file_path) / el.id).string();
        for (auto& file : files) {
            if (std::string::npos != file.find("full.")) {
                unit.fullPicture = file_path_pre + file;
            }
            if (std::string::npos != file.find("detect.")) {
                unit.detectedPicture = file_path_pre + file;
            }
        }
        if (static_cast<int>(cosmo::EventRecordFlag::EventRecordVideoFlagHave) == el.video_flag) {
            auto video_path = local_file_path_pre + "_video.mp4";
            if (cosmo::util::FileExist(video_path)) {
                unit.video           = file_path_pre + "_video.mp4";
                unit.videostructured = file_path_pre + ".json";
            }
        }
        unit.property = el.property;
        events.push_back(unit);
    }
    return events;
}

AlarmQueryResult AlarmRecordServiceImpl::QueryAlarmRecords(const AlarmQueryCondition& condition, int order) {
    auto dao_condition = ToDao(condition);
    return ToDto(db_event_->Query(dao_condition, order));
}

std::vector<cosmo::MsgEventUnit> AlarmRecordServiceImpl::QueryFace(cosmo::MsgConditionEvent& condition,
                                                                   int64_t& total) {
    std::vector<cosmo::MsgEventUnit> events;
    cosmo::db::QueryFaceTaskEventCondition condition_db;
    condition_db.time_begin  = condition.timeBegin;
    condition_db.time_end    = condition.timeEnd;
    condition_db.camera_name = condition.videoChannelName;
    condition_db.page_num    = condition.pageNum;
    condition_db.page_size   = condition.pageSize;
    auto rets                = db_face_event_->Query(condition_db);
    total                    = static_cast<int64_t>(rets.total_count);
    for (auto& el : rets.behavior_list) {
        cosmo::MsgEventUnit unit;
        unit.id             = el.id;
        unit.videoChannelId = el.camera_id;
        unit.channelCode    = el.camera_out_id;
        unit.channelName    = el.camera_name;
        unit.timestamp      = el.timestamp;
        unit.category       = el.category;
        unit.algorithmCode  = el.algorithm_code;
        unit.areaId         = el.area_id;
        unit.areaName       = el.area_name;

        std::vector<std::string> files;
        if (!cosmo::util::DecodeJson(el.extra_files, files)) {
            LOG_WARN("Failed to decode extraFiles for event {}", el.id);
        }
        std::string file_path     = cosmo::path::GetEventWebPath(el.timestamp);
        std::string file_path_pre = (std::filesystem::path(file_path) / el.id).string();
        for (auto& file : files) {
            if (std::string::npos != file.find("full.")) {
                unit.fullPicture = file_path_pre + file;
            }
            if (std::string::npos != file.find("detect.")) {
                unit.detectedPicture = file_path_pre + file;
            }
        }
        if (static_cast<int>(cosmo::EventRecordFlag::EventRecordVideoFlagHave) == el.video_flag) {
            unit.video           = file_path_pre + "_video.mp4";
            unit.videostructured = file_path_pre + ".json";
        }

        unit.property = el.property;
        events.push_back(unit);
    }
    return events;
}

void AlarmRecordServiceImpl::RemoveItems(const std::vector<std::string>& list) {
    db_event_->RemoveItems(list);
}

bool AlarmRecordServiceImpl::UpdateAlarmReportStatus(const std::string& record_id, bool reported) {
    try {
        return db_event_->UpdateRecordReportStatus(record_id, reported);
    } catch (const SQLite::Exception&) {
        std::this_thread::sleep_for(timing::kSlowPollInterval);
        try {
            return db_event_->UpdateRecordReportStatus(record_id, reported);
        } catch (const SQLite::Exception& e) {
            LOG_WARN("Update:{} catch error:{}", record_id, e.what());
        }
    }
    return false;
}

bool AlarmRecordServiceImpl::InsertPassFlow(const std::string& camera_id, const std::string& algorithm_id,
                                            uint64_t hour, int enter_num, int leave_num) {
    try {
        return db_pass_flow_event_->AddNumber(camera_id, algorithm_id, hour, enter_num, leave_num);
    } catch (const SQLite::Exception&) {
        std::this_thread::sleep_for(timing::kSlowPollInterval);
        try {
            return db_pass_flow_event_->AddNumber(camera_id, algorithm_id, hour, enter_num, leave_num);
        } catch (const SQLite::Exception& e) {
            LOG_WARN("Insert:{}/{} catch error:{}", camera_id, algorithm_id, e.what());
        }
    }
    return false;
}

FlowQueryResult AlarmRecordServiceImpl::QueryPassengerFlow(const FlowQueryCondition& condition) {
    auto dao_condition = ToDao(condition);
    return ToDto(db_pass_flow_event_->Query(dao_condition, 0, 1));  // defaulting maxSize to 0 and order to 1
}

}  // namespace cosmo::service
