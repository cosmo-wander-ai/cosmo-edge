// MessageFaceLibHandler — Message Face Lib Handler implementation.

#include "api/MessageFaceLibHandler.h"

#include <algorithm>

#include "db/TransactionGuard.h"
#include "service/face/IFaceLibRepo.h"
#include "service/face/IPersonDaoService.h"
#include "service/face/IPersonRepo.h"
#include "util/ErrorCode.h"

namespace cosmo {

static constexpr const char* kTag = "FaceLibHandler";

MessageFaceLibHandler::MessageFaceLibHandler(service::IFaceLibRepo& lib_repo,
                                             service::IPersonRepo& person_repo,
                                             service::IFaceFeature& face_feature,
                                             service::IPersonDaoService& dao_svc,
                                             service::IFaceLibService& face_lib_svc,
                                             service::IVideoFrameCodec& codec)
    : lib_repo_(lib_repo),
      person_repo_(person_repo),
      face_feature_(face_feature),
      dao_svc_(dao_svc),
      face_lib_svc_(face_lib_svc),
      codec_(codec) {}

// Face lib delete
Lib::MsgDeleteFaceLibSend MessageFaceLibHandler::Handle(Lib::MsgDeleteFaceLibRecv&& data,
                                                        std::error_condition& /*errc*/) {
    Lib::MsgDeleteFaceLibSend retData{};

    std::vector<std::string> vec;
    vec.reserve(data.faceLibIdList.size());
    std::transform(data.faceLibIdList.begin(), data.faceLibIdList.end(), std::back_inserter(vec),
                   [](const auto& ref) { return ref.ToString(); });

    for (auto& ref : lib_repo_.RemoveFaceLib(vec)) {
        if (static_cast<util::ErrorEnum>(ref.resCode) == util::ErrorEnum::Success) {
            dao_svc_.RemoveFaceLib(ref.failedFaceLibId);
        } else {
            retData.resData.failedFaceLibList.push_back(ref);
        }
    }
    return retData;
}

// Face lib person delete
Lib::MsgDeletePersonSend MessageFaceLibHandler::Handle(Lib::MsgDeletePersonRecv&& data,
                                                       std::error_condition& errc) {
    Lib::MsgDeletePersonSend retData{};
    if (data.removeAll) {
        errc = person_repo_.RemoveAllPerson(data.faceLibId);
        if (!data.faceLibId.empty()) {
            dao_svc_.ClearFaceLib(data.faceLibId);
        } else {
            // Query all face lib IDs via the DAO service
            db::FaceLibQueryCondition cond{};
            constexpr int kMaxQueryPageSize = 10000;
            cond.page_size                  = kMaxQueryPageSize;
            auto db_result                  = dao_svc_.QueryFaceLib(cond);
            for (auto& face_lib : db_result.face_lib_list) {
                dao_svc_.ClearFaceLib(face_lib.id);
            }
        }
    } else {
        for (auto& res : person_repo_.RemovePerson(lib_repo_.GetFaceLib(data.faceLibId), data.personIdList)) {
            dao_svc_.RemovePerson(res.id);
            if (static_cast<util::ErrorEnum>(res.resCode) != util::ErrorEnum::Success) {
                retData.failedList.push_back(res);
            }
        }
    }
    return retData;
}

}  // namespace cosmo