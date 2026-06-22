// Face registration library — database-backed face record management.

#include "flow/face/FaceRegLib.h"

#include <SQLiteCpp/SQLiteCpp.h>

#include <unordered_map>

#include "db/PersonDao.h"
#include "service/detail/ServiceRegistry.h"
#include "service/infra/IDbService.h"
#include "util/Exec.h"
#include "util/Log.h"
namespace cosmo {
FaceRegLib::FaceRegLib()
    : m_dbPersonEvent(std::make_shared<db::PersonDao>(
          *service::ServiceRegistry::Instance().Get<service::IDbService>().GetDb())) {
    m_dbPersonEvent->CreateTable();
}

FaceRegLib::~FaceRegLib() {
    LOG_WARN("{}", "FaceRegLib::~FaceRegLib()");
}

bool FaceRegLib::Insert(FaceRegRecordUnit& unit) {
    auto data = RegDataToEventData(unit);
    return m_dbPersonEvent->AddPerson(data);
}

MsgQueryFacesS FaceRegLib::Query(const MsgQueryFacesR& condition) {
    db::FacePersonQueryCondition cond{};
    for (auto& id : condition.faceLibIdList)
        cond.face_lib_id_list.push_back(id);
    cond.person_id     = condition.personId;
    cond.person_name   = condition.personName;
    cond.serial_number = condition.serialNumber;
    cond.page_num      = condition.pageNum;
    cond.page_size     = condition.pageSize;
    cond.query_id      = condition.queryId;
    auto dbr           = m_dbPersonEvent->QueryPersons(cond);
    MsgQueryFacesS result{};
    result.resData.queryId    = dbr.query_id;
    result.resData.totalCount = dbr.total_count;
    for (auto& r : dbr.person_list) {
        MsgQueryFacesS::Person p{};
        p.id              = r.id;
        p.name            = r.name;
        p.createTimestamp = r.create_timestamp;
        p.updateTimestamp = r.update_timestamp;
        p.serialNumber    = r.serial_number;
        for (auto& fl : r.face_lib_list) {
            MsgQueryFacesS::FaceLib lib{};
            lib.id   = fl.id;
            lib.name = fl.name;
            p.faceLibIdList.push_back(std::move(lib));
        }
        for (auto& pic : r.picture_list) {
            MsgQueryFacesS::Picture pp{};
            pp.id  = pic.id;
            pp.url = pic.url;
            p.pictureList.push_back(std::move(pp));
        }
        result.resData.personList.push_back(std::move(p));
    }
    return result;
}

MsgQueryFaceLibInfoS FaceRegLib::Query(const MsgQueryFaceLibInfoR& condition) {
    db::FaceLibQueryCondition cond{};
    cond.camera_id     = condition.cameraId;
    cond.task_type     = condition.taskType;
    cond.face_lib_name = condition.faceLibName;
    cond.face_lib_id   = condition.faceLibId;
    cond.page_num      = condition.pageNum;
    cond.page_size     = condition.pageSize;
    auto dbr           = m_dbPersonEvent->QueryFaceLib(cond);
    MsgQueryFaceLibInfoS result{};
    result.resData.searchAll    = dbr.search_all;
    result.resData.faceLibCount = dbr.face_lib_count;
    for (auto& r : dbr.face_lib_list) {
        MsgQueryFaceLibInfoS::FaceLib lib{};
        lib.id                = r.id;
        lib.name              = r.name;
        lib.type              = r.type;
        lib.threshold         = r.threshold;
        lib.maxFaceNumber     = r.max_face_number;
        lib.strangerAlarm     = r.stranger_alarm;
        lib.strangerThreshold = r.stranger_threshold;
        lib.personNumber      = r.person_number;
        lib.faceNumber        = r.face_number;
        lib.createTimestamp   = r.create_timestamp;
        lib.updateTimestamp   = r.update_timestamp;
        result.resData.faceLibList.push_back(std::move(lib));
    }
    return result;
}

AiFeature FaceRegLib::QueryFaceFeature(const std::string& faceId) {
    AiFeature faceFeature;
    auto temp           = m_dbPersonEvent->QueryFaceFeature(faceId);
    faceFeature.feature = temp;
    return faceFeature;
}

bool FaceRegLib::AddFaceLib(const MsgBaseFaceLibInfo& data) {
    db::LibInfo info{data.id,
                     data.name,
                     data.type,
                     data.threshold,
                     data.maxFaceNumber,
                     data.strangerAlarm,
                     data.strangerThreshold};
    return m_dbPersonEvent->AddFaceLib(info);
}

bool FaceRegLib::UpdateFaceLib(const MsgBaseFaceLibInfo& data) {
    db::LibInfo info{data.id,
                     data.name,
                     data.type,
                     data.threshold,
                     data.maxFaceNumber,
                     data.strangerAlarm,
                     data.strangerThreshold};
    return m_dbPersonEvent->UpdateFaceLib(info);
}

bool FaceRegLib::AddPerson(const MsgConditionLib& person,
                           const std::vector<std::pair<std::string, std::vector<float>>>& face) {
    db::PersonCondition pc{};
    pc.person_operation = person.personOperation;
    for (auto& id : person.faceLibId)
        pc.face_lib_id.push_back(id);
    pc.person_id   = person.personId;
    pc.person_name = person.personName;
    for (auto& id : person.retainPictureId)
        pc.retain_picture_id.push_back(id);
    pc.serial_number = person.serialNumber;
    pc.create_time   = person.createTime;
    pc.update_time   = person.updateTime;
    return m_dbPersonEvent->AddPerson(pc, face);
}

bool FaceRegLib::UpdatePerson(const MsgConditionLib& person,
                              const std::vector<std::pair<std::string, std::vector<float>>>& face) {
    db::PersonCondition pc{};
    pc.person_operation = person.personOperation;
    for (auto& id : person.faceLibId)
        pc.face_lib_id.push_back(id);
    pc.person_id   = person.personId;
    pc.person_name = person.personName;
    for (auto& id : person.retainPictureId)
        pc.retain_picture_id.push_back(id);
    pc.serial_number = person.serialNumber;
    pc.create_time   = person.createTime;
    pc.update_time   = person.updateTime;
    return m_dbPersonEvent->UpdatePerson(pc, face);
}

void FaceRegLib::Begine() {
    m_dbPersonEvent->Begin();
    return;
}

void FaceRegLib::Commit() {
    m_dbPersonEvent->Commit();
    return;
}

void FaceRegLib::Rollback() {
    m_dbPersonEvent->Rollback();
    return;
}

bool FaceRegLib::RemoveFaceLib(const std::string& faceLibId) {
    return m_dbPersonEvent->RemoveFaceLib(faceLibId);
}

bool FaceRegLib::RemovePerson(const std::string& personId) {
    return m_dbPersonEvent->RemovePerson(personId);
}

bool FaceRegLib::ClearFaceLib(const std::string& faceLibId) {
    return m_dbPersonEvent->ClearFaceLib(faceLibId);
}

db::FaceRegRecordUnit FaceRegLib::RegDataToEventData(FaceRegRecordUnit& unit) {
    db::FaceRegRecordUnit data{};
    data.face_update_time = unit.faceUpdateTime;
    data.face_create_time = unit.faceCreateTime;
    data.face_name        = unit.serialName;
    data.serial_name      = unit.faceName;
    data.id               = unit.id;
    for (size_t i = 0; i < unit.facelibID.size(); i++) {
        data.face_lib_id.push_back(unit.facelibID[i]);
    }
    for (size_t i = 0; i < unit.facePicInfos.size(); i++) {
        db::FacePicInfo temp;
        temp.id      = unit.facePicInfos[i].id;
        temp.feature = unit.facePicInfos[i].feature.feature;
        data.face_pic_infos.push_back(temp);
    }

    return data;
}
}  // namespace cosmo