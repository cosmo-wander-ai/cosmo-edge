#pragma once

/*
 * @Author: zhangxiaobo
 * @Date: 2025-04-08 17:30:13
 * @LastEditors: zhangxiaobo
 * @LastEditTime: 2025-05-21 15:55:44
 * @Description: Face record
 */

#include <map>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>

#include "db/DbTypes.h"
#include "infer/AiCommon.h"
#include "util/MsgBaseTypes.h"
#include "util/dto/EventMsgTypes.h"
#include "util/dto/FilterTypes.h"
#include "util/dto/OverviewTypes.h"

namespace cosmo::db {
class PersonDao;
}

namespace cosmo {
struct FacePicInfo {
    std::string id;
    AiFeature feature;
};

struct FaceRegRecordUnit {
    int64_t faceUpdateTime;
    int64_t faceCreateTime;
    std::string id;
    std::string faceName;
    std::string serialName;
    std::vector<std::string> facelibID;
    std::vector<FacePicInfo> facePicInfos;
};

/// Face registration library — database-backed face record management.
/// Lifecycle managed by FaceManager (no longer a singleton).
class FaceRegLib {
public:
    FaceRegLib();
    ~FaceRegLib();

    bool Insert(FaceRegRecordUnit &unit);

    // Person query
    MsgQueryFacesS Query(const MsgQueryFacesR &condition);
    // Face library query
    MsgQueryFaceLibInfoS Query(const MsgQueryFaceLibInfoR &condition);

    AiFeature QueryFaceFeature(const std::string &faceId);

    bool AddFaceLib(const MsgBaseFaceLibInfo &data);

    bool UpdateFaceLib(const MsgBaseFaceLibInfo &data);

    bool AddPerson(const MsgConditionLib &person,
                   const std::vector<std::pair<std::string, std::vector<float>>> &face);

    bool UpdatePerson(const MsgConditionLib &person,
                      const std::vector<std::pair<std::string, std::vector<float>>> &face);

    void Begine();

    void Commit();

    void Rollback();

    bool RemoveFaceLib(const std::string &faceLibId);

    bool RemovePerson(const std::string &personId);

    bool ClearFaceLib(const std::string &faceLibId);

private:
    db::FaceRegRecordUnit RegDataToEventData(FaceRegRecordUnit &unit);

private:
    std::shared_mutex m_mtx;
    std::shared_ptr<db::PersonDao> m_dbPersonEvent;
};
}  // namespace cosmo
