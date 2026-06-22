// FaceManager_Export — Face Manager_ Export implementation.

#include <algorithm>
#include <filesystem>
#include <fstream>

#include "flow/face/FaceManager.h"
#include "service/detail/ServiceRegistry.h"
#include "service/face/IPersonDaoService.h"
#include "util/Exception.h"
#include "util/Log.h"
#include "util/PathUtil.h"
#include "util/UuidUtil.h"

namespace fs = std::filesystem;

namespace cosmo {

namespace {

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

std::string FaceManager::SetQueryCond(const MsgQueryFacesR& query_cond) {
    std::lock_guard<std::shared_mutex> lock(mtx_);
    query_cond_.second = std::make_unique<MsgQueryFacesR>(query_cond);
    query_cond_.first  = util::GenerateUUID();
    return query_cond_.first;
}

util::ErrorEnum FaceManager::ExportPersonToPath(const std::string& query_id,
                                                const std::vector<std::string>& person_id_list,
                                                const std::string& path) {
    MsgQueryFacesR query_cond{};
    {
        std::shared_lock<std::shared_mutex> lock(mtx_);
        if (person_id_list.empty() && query_id != query_cond_.first) {
            throw util::ErrorMessage(util::ErrorEnum::NoSuchId, "Please query before exporting");
        }
        query_cond = *query_cond_.second;
    }

    query_cond.pageNum  = 1;
    query_cond.pageSize = 100;

    size_t total_count = std::numeric_limits<size_t>::max();

    std::ofstream out_file(path);
    if (out_file.is_open()) {
        auto http_dir = "http://";
        WriteHeader(out_file);

        size_t idx = 0;
        for (; static_cast<size_t>(query_cond.pageNum - 1) * query_cond.pageSize < total_count;
             ++query_cond.pageNum) {
            auto& dao_svc = service::ServiceRegistry::Instance().Get<service::IPersonDaoService>();
            auto result   = ConvertFacePersonResult(dao_svc.QueryPersons(ToDbFacePersonCond(query_cond)));
            total_count   = result.resData.totalCount;

            if (person_id_list.empty()) {
                for (auto& res_person : result.resData.personList) {
                    WritePersonData(out_file, ++idx, MsgQueryFacesSendPerson(res_person), http_dir);
                }
            } else {
                for (auto& res_person : result.resData.personList) {
                    auto it = find(person_id_list.begin(), person_id_list.end(), res_person.id);
                    if (it != person_id_list.end()) {
                        WritePersonData(out_file, ++idx, MsgQueryFacesSendPerson(res_person), http_dir);
                    }
                }
            }
        }
        return util::ErrorEnum::Success;
    }
    LOG_ERRO("{} open failed.", path);
    return util::ErrorEnum::FileOpenFailed;
}

std::ostream& FaceManager::WriteHeader(std::ostream& os) {
    unsigned char szUTF_8BOM[] = {0xEF, 0xBB, 0xBF, 0};
    os << szUTF_8BOM;
    os << "\"序号\",\"姓名\",\"人员编号\",\"脸库\",\"照片地址\"\n";
    return os;
}

std::ostream& FaceManager::WritePersonData(std::ostream& os, size_t index,
                                           const MsgQueryFacesSendPerson& res_person,
                                           const std::string& http_dir) {
    os << "\"" << index << "\t\",\"" << res_person.name << "\t\",\"" << res_person.serialNumber << "\t\",\"";

    for (size_t i = 0; i < res_person.faceLibIdList.size(); ++i) {
        if (i != 0) {
            os << ";";
        }
        os << res_person.faceLibIdList[i].name;
    }
    os << "\t\",\"";

    for (size_t i = 0; i < res_person.pictureList.size(); ++i) {
        if (i != 0) {
            os << ";";
        }
        os << http_dir
           << (fs::path(cosmo::path::GetFaceLibPhotoDir()) / res_person.pictureList[i].id).concat(".jpg");
    }
    os << "\t\"\n";
    return os;
}

}  // namespace cosmo
