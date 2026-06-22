// FaceManager — Face Manager implementation.

#include "flow/face/FaceManager.h"

#include <algorithm>
#include <filesystem>
#include <iterator>

#include "flow/face/FaceRegLib.h"
#include "service/detail/ServiceRegistry.h"
#include "service/face/IPersonDaoService.h"
#include "util/DurationLogger.h"
#include "util/Log.h"
#include "util/PathUtil.h"

namespace fs = std::filesystem;
namespace cosmo {

// ---------------------------------------------------------------------------
// Helper: convert db:: result types to Msg* types at the DAO boundary
// ---------------------------------------------------------------------------
namespace {

    MsgQueryFaceLibInfoS ConvertFaceLibResult(const db::FaceLibQueryResult& dbr) {
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

    MsgQueryFacesS ConvertFacePersonResult(const db::FacePersonQueryResult& dbr) {
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

    db::FaceLibQueryCondition ToDbFaceLibCond(const MsgQueryFaceLibInfoR& src) {
        db::FaceLibQueryCondition c{};
        c.camera_id     = src.cameraId;
        c.task_type     = src.taskType;
        c.face_lib_name = src.faceLibName;
        c.face_lib_id   = src.faceLibId;
        c.page_num      = src.pageNum;
        c.page_size     = src.pageSize;
        return c;
    }

    db::FacePersonQueryCondition ToDbFacePersonCond(const MsgQueryFacesR& src) {
        db::FacePersonQueryCondition c{};
        for (auto& id : src.faceLibIdList)
            c.face_lib_id_list.push_back(id);
        c.person_id     = src.personId;
        c.person_name   = src.personName;
        c.serial_number = src.serialNumber;
        c.page_num      = src.pageNum;
        c.page_size     = src.pageSize;
        c.query_id      = src.queryId;
        return c;
    }

}  // namespace

void FaceManager::Load() {
    MsgQueryFaceLibInfoR face_lib_cond{};
    MsgQueryFacesR face_cond{};

    face_lib_cond.pageSize = 1000;
    auto& dao_svc          = service::ServiceRegistry::Instance().Get<service::IPersonDaoService>();
    auto result_face_lib   = ConvertFaceLibResult(dao_svc.QueryFaceLib(ToDbFaceLibCond(face_lib_cond)));
    for (auto& res_face_lib : result_face_lib.resData.faceLibList) {
        auto face_lib = std::make_shared<FaceLib>();
        face_lib->SetData(static_cast<FaceLib::DataType>(res_face_lib));
        face_lib->SetCreateTime(res_face_lib.updateTimestamp);
        face_libs_.push_back(face_lib);
    }
    LOG_INFO("Loaded {} faceLibs", face_libs_.size());

    size_t total_count = std::numeric_limits<size_t>::max();

    face_cond.pageNum  = 1;
    face_cond.pageSize = 10000;

    for (; static_cast<size_t>(face_cond.pageNum - 1) * face_cond.pageSize < total_count;
         ++face_cond.pageNum) {
        util::DurationLogger logger_query_person("queryPerson");
        auto result_person = ConvertFacePersonResult(dao_svc.QueryPersons(ToDbFacePersonCond(face_cond)));
        logger_query_person.Print();
        total_count = result_person.resData.totalCount;
        for (auto& res_person : result_person.resData.personList) {
            auto it = person_map_.find(res_person.id);
            if (it != person_map_.end()) {
                continue;
            }

            auto person = std::make_shared<Person>(res_person.id);
            person->SetName(res_person.name);
            person->SetSerialNumber(res_person.serialNumber);
            person->SetUpdateTime(res_person.updateTimestamp);
            person->SetCreateTime(res_person.createTimestamp);

            for (auto& q_face_lib : res_person.faceLibIdList) {
                for (auto& p_face_lib : face_libs_) {
                    if (p_face_lib->GetId() == q_face_lib.id) {
                        person->AddFaceLib(p_face_lib);
                    }
                }
            }

            // Defensive check: skip person if no faceLib matched
            if (person->GetFaceLibs().empty()) {
                LOG_WARN(
                    "Person [{}] name=[{}] has {} faceLibIdList entries but none matched memory "
                    "faceLibs, skipping",
                    res_person.id, res_person.name, res_person.faceLibIdList.size());
                continue;
            }

            for (auto& face_info : res_person.pictureList) {
                AiFeature feature;
                feature.feature = dao_svc.QueryFaceFeature(face_info.id);
                auto face_pic =
                    std::make_shared<FacePic>(face_info.id, cosmo::path::GetFaceLibPhotoDir(), feature);
                person->AddPicture(face_pic);
            }
            person_map_[res_person.id] = person;
        }
    }
    LOG_INFO("person size: {}", person_map_.size());
}

}  // namespace cosmo
