// MessageFaceLibHandler_ModifyLib — Message Face Lib Handler_ Modify Lib implementation.

#include "api/MessageFaceLibHandler.h"
#include "db/TransactionGuard.h"
#include "service/face/IFaceLibRepo.h"
#include "service/face/IPersonDaoService.h"
#include "service/face/dto/FaceDto.h"
#include "util/ErrorCode.h"
#include "util/Exception.h"

namespace cosmo {

namespace {
    db::LibInfo ToDbLibInfo(const MsgBaseFaceLibInfo& src) {
        return {src.id, src.name, src.type, src.threshold, src.maxFaceNumber};
    }
}  // namespace

// Face lib add/update
Lib::MsgModifyFaceLibSend MessageFaceLibHandler::Handle(Lib::MsgModifyFaceLibRecv&& data,
                                                        std::error_condition& errc) {
    Lib::MsgModifyFaceLibSend retData{};
    switch (static_cast<Operation>(data.faceLibOperation)) {
        case Operation::Add: {
            auto allFacesLibsInDev = lib_repo_.GetAllFaceLibs();
            if (allFacesLibsInDev.size() >= lib_repo_.GetFaceLibMaxCount()) {
                throw util::ErrorMessage(util::ErrorEnum::FaceLibCountOverFlow);
            }

            if (data.faceLib.name.empty()) {
                throw util::ErrorMessage(util::ErrorEnum::ParameterException);
            }

            std::string newLibId;
            auto faceLib    = lib_repo_.CreateFaceLib(MsgBaseFaceLibInfo(data.faceLib), newLibId);
            data.faceLib.id = newLibId;

            db::TransactionGuard<service::IPersonDaoService> guard(dao_svc_);
            if (!dao_svc_.AddFaceLib(ToDbLibInfo(data.faceLib))) {
                throw util::ErrorMessage(util::ErrorEnum::DatabaseFailed);
            }
            errc = lib_repo_.AddFaceLib(faceLib);
            if (errc == util::ErrorEnum::Success) {
                guard.Commit();
                retData.resData.faceLibId = newLibId;
            }
        } break;
        case Operation::Update: {
            db::TransactionGuard<service::IPersonDaoService> guard(dao_svc_);
            if (!dao_svc_.UpdateFaceLib(ToDbLibInfo(data.faceLib))) {
                throw util::ErrorMessage(util::ErrorEnum::DatabaseFailed);
            }
            errc = lib_repo_.UpdateFaceLib(data.faceLib.id, std::move(data.faceLib));
            if (errc == util::ErrorEnum::Success) {
                guard.Commit();
            }
        } break;
        default:
            errc = util::ErrorEnum::UnknownOperation;
            break;
    }

    return retData;
}

}  // namespace cosmo
