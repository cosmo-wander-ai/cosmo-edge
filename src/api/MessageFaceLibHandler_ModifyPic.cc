// MessageFaceLibHandler_ModifyPic — Message Face Lib Handler_ Modify Pic implementation.

#include <algorithm>
#include <filesystem>

#include "api/MessageFaceLibHandler.h"
#include "db/TransactionGuard.h"
#include "service/face/IFaceFeature.h"
#include "service/face/IFaceLibRepo.h"
#include "service/face/IPersonDaoService.h"
#include "service/face/IPersonRepo.h"
#include "service/face/dto/FaceDto.h"
#include "service/media/IVideoFrameCodec.h"
#include "util/CipherUtil.h"
#include "util/ErrorCode.h"
#include "util/Exception.h"
#include "util/FileUtil.h"
#include "util/Log.h"
#include "util/PathUtil.h"
#include "util/TimeUtil.h"
#include "util/UuidUtil.h"

namespace fs = std::filesystem;

namespace cosmo {

namespace {
    static constexpr const char* kTag = "FaceLibHandler";

    constexpr size_t kMaxPictureCount   = 3;
    constexpr size_t kMinBase64Length   = 3;
    constexpr float kDefaultFaceQuality = 80.0f;

    db::PersonCondition ToDbPersonCond(const MsgConditionLib& src) {
        db::PersonCondition pc{};
        pc.person_operation = src.personOperation;
        for (auto& id : src.faceLibId)
            pc.face_lib_id.push_back(id);
        pc.person_id   = src.personId;
        pc.person_name = src.personName;
        for (auto& id : src.retainPictureId)
            pc.retain_picture_id.push_back(id);
        pc.serial_number = src.serialNumber;
        pc.create_time   = src.createTime;
        pc.update_time   = src.updateTime;
        return pc;
    }
}  // namespace

// Face lib person add/edit
Lib::MsgModifyFacePicLibSend MessageFaceLibHandler::Handle(Lib::MsgModifyFacePicLibRecv&& data,
                                                           std::error_condition& errc) {
    // Normalize retained picture IDs that may contain full URLs
    for (auto& id : data.retainPictureId) {
        if (id.size() > 36) {
            auto vt = util::Split(id, "/.");
            if (vt.size() > 2 && vt.back() == "jpg") {
                id = std::string(vt[vt.size() - 2]);
            }
        }
    }
    data.updateTime = util::GetMilliseconds();

    Lib::MsgModifyFacePicLibSend retData{};
    retData.resData.personId = data.personId;

    std::vector<std::string> faceLibId;
    faceLibId.reserve(data.faceLibId.size());
    std::copy(data.faceLibId.begin(), data.faceLibId.end(), std::back_inserter(faceLibId));
    auto faceLibs = lib_repo_.GetFaceLibs(std::move(faceLibId));

    if (!person_repo_.IsValidSerialNumber(data.personId, data.serialNumber)) {
        throw util::ErrorMessage(util::ErrorEnum::ExistedPersonSerialNumber,
                                 "Person ID cannot be duplicated");
    }

    // Guard clause: face lib must exist
    if (faceLibs.empty()) {
        throw util::ErrorMessage(util::ErrorEnum::NoSuchId, "Face library ID does not exist");
    }

    PersonPtr person;
    switch (static_cast<Operation>(data.personOperation)) {
        case Operation::Add:
            if (data.personName.empty()) {
                throw util::ErrorMessage(util::ErrorEnum::ParameterException, "Person name cannot be empty");
            }
            person          = person_repo_.CreatePerson();
            data.personId   = person_repo_.GetPersonId(person);
            data.createTime = data.updateTime;
            // NOTE: intentional fallthrough — new person may include face pictures
            [[fallthrough]];
        case Operation::Update:
            HandleModifyFacePicUpdate(data, errc, retData, faceLibs, person);
            break;
        default:
            errc = util::ErrorEnum::UnknownOperation;
            break;
    }

    return retData;
}

void MessageFaceLibHandler::HandleModifyFacePicUpdate(Lib::MsgModifyFacePicLibRecv& data,
                                                      std::error_condition& errc,
                                                      Lib::MsgModifyFacePicLibSend& retData,
                                                      std::vector<FaceLibPtr>& faceLibs, PersonPtr& person) {
    if (!person && !(person = person_repo_.GetPerson(data.personId))) {
        throw util::ErrorMessage(util::ErrorEnum::NoSuchId, "Person ID does not exist");
    }
    auto picSize = data.pictureBase64.size() + data.retainPictureId.size();
    if (picSize > kMaxPictureCount || picSize == 0) {
        throw util::ErrorMessage(util::ErrorEnum::ParameterException, "Photo count must be 1 to 3");
    }
    auto vFacePicNow = person_repo_.GetPersonPictures(person);
    ValidateFaceLibCapacity(faceLibs, person, vFacePicNow, data);

    std::vector<FacePicPtr> vFacePic;
    std::vector<std::string> vRemovePicId;
    std::vector<std::pair<std::string, std::vector<float>>> faceFeature;

    if (!data.retainPictureId.empty() || !data.pictureBase64.empty()) {
        ProcessNewPictures(data, errc, retData, faceFeature, vFacePic);
        if (errc != util::ErrorEnum::Success) {
            return;
        }
        CollectRetainedPictures(vFacePicNow, data.retainPictureId, faceFeature, vFacePic, vRemovePicId);
    }

    // Release face feature model GPU memory after single-person operation
    face_feature_.ReleaseFaceModels();

    CommitPersonChanges(data, faceLibs, person, faceFeature, vFacePic, vRemovePicId, retData);
}

void MessageFaceLibHandler::ValidateFaceLibCapacity(const std::vector<FaceLibPtr>& faceLibs,
                                                    const PersonPtr& person,
                                                    const std::vector<FacePicPtr>& vFacePicNow,
                                                    const Lib::MsgModifyFacePicLibRecv& data) {
    for (const auto& spFaceLib : faceLibs) {
        auto libView   = service::FaceLibView::From(spFaceLib);
        auto faceCount = person_repo_.IsPersonInFaceLibs(person, {libView.id}) ? vFacePicNow.size() : 0;
        if (libView.faceCount + data.retainPictureId.size() + data.pictureBase64.size() - faceCount >
            libView.faceMaxCount) {
            throw util::ErrorMessage(util::ErrorEnum::FaceLibCountOverFlow);
        }
    }
}

void MessageFaceLibHandler::ProcessNewPictures(
    const Lib::MsgModifyFacePicLibRecv& data, std::error_condition& errc,
    Lib::MsgModifyFacePicLibSend& /*retData*/,
    std::vector<std::pair<std::string, std::vector<float>>>& faceFeature, std::vector<FacePicPtr>& vFacePic) {
    for (auto& pic : data.pictureBase64) {
        if (pic.size() < kMinBase64Length) {
            continue;
        }

        auto picId  = util::GenerateUUID();
        auto picBin = util::DecBase64Vec(pic);
        if (picBin.empty()) {
            LOG_WARN("[{}] Base64 decode failed, skipping picture", kTag);
            errc = util::ErrorEnum::InternalError;
            return;
        }

        auto decodedImage = codec_.DecodeJpeg(picBin);
        if (!decodedImage) {
            LOG_WARN("[{}] JPEG decode failed, skipping picture", kTag);
            errc = util::ErrorEnum::InternalError;
            return;
        }

        AiFeature feature;
        VideoFramePtr cutImage;
        float quality = kDefaultFaceQuality;
        errc          = face_feature_.ExtractFaceFeature(decodedImage, quality, feature, cutImage);
        if (errc == util::ErrorEnum::ServiceNotInit) {
            throw util::ErrorMessage(util::ErrorEnum::ServiceNotInit,
                                     "Service initializing, please try again later");
        }
        if (errc != util::ErrorEnum::Success) {
            LOG_WARN("[{}] Face feature extraction failed", kTag);
            return;
        }

        auto origJpeg = codec_.EncodeJpeg(cutImage);
        if (origJpeg.empty()) {
            LOG_ERRO("[{}] Encode Memory Jpeg failed", kTag);
            throw util::ErrorMessage(util::ErrorEnum::InternalError, "JPEG encoding failed");
        }

        std::string photoPath = (fs::path(cosmo::path::GetFaceLibPhotoDir()) / picId).concat(".jpg");
        if (!util::WriteFile(photoPath, reinterpret_cast<const std::uint8_t*>(origJpeg.data()),
                             static_cast<int>(origJpeg.size()))) {
            LOG_ERRO("[{}] write file error", kTag);
            continue;
        }
        faceFeature.emplace_back(picId, feature.feature);
        vFacePic.push_back(person_repo_.CreateFacePic(picId, cosmo::path::GetFaceLibPhotoDir(), feature));
    }
}

void MessageFaceLibHandler::CollectRetainedPictures(
    const std::vector<FacePicPtr>& vFacePicNow, const std::vector<std::string>& retainPictureId,
    std::vector<std::pair<std::string, std::vector<float>>>& faceFeature, std::vector<FacePicPtr>& vFacePic,
    std::vector<std::string>& vRemovePicId) {
    for (const auto& pic : vFacePicNow) {
        auto picView = service::FacePicView::From(pic);
        LOG_INFO("[{}] {}", kTag, picView.id);
        auto it = find(retainPictureId.begin(), retainPictureId.end(), picView.id);
        if (it == retainPictureId.end()) {
            vRemovePicId.push_back(picView.id);
            continue;
        }
        faceFeature.emplace_back(picView.id, picView.feature.feature);
        vFacePic.push_back(pic);
    }
}

void MessageFaceLibHandler::CommitPersonChanges(
    Lib::MsgModifyFacePicLibRecv& data, std::vector<FaceLibPtr>& faceLibs, PersonPtr& person,
    const std::vector<std::pair<std::string, std::vector<float>>>& faceFeature,
    const std::vector<FacePicPtr>& vFacePic, const std::vector<std::string>& vRemovePicId,
    Lib::MsgModifyFacePicLibSend& retData) {
    db::TransactionGuard<service::IPersonDaoService> guard(dao_svc_);
    auto pc   = ToDbPersonCond(data);
    bool dbOk = (static_cast<Operation>(data.personOperation) == Operation::Add)
                    ? dao_svc_.AddPerson(pc, faceFeature)
                    : dao_svc_.UpdatePerson(pc, faceFeature);
    if (!dbOk) {
        throw util::ErrorMessage(util::ErrorEnum::DatabaseFailed);
    }

    if (person_repo_.GetPersonPictureCount(person) - vRemovePicId.size() + vFacePic.size() == 0) {
        throw util::ErrorMessage(util::ErrorEnum::NoFaceDetected, "Photo detection failed");
    }

    for (const auto& picId : vRemovePicId) {
        person_repo_.RemovePersonPicture(person, picId);
    }

    person_repo_.UpdatePersonMetadata(person, data.personName, data.serialNumber, data.updateTime,
                                      data.updateTime);

    person_repo_.UpdatePerson(faceLibs, person);
    for (const auto& spFacePic : vFacePic) {
        person_repo_.AddPersonPicture(person, spFacePic);
    }
    guard.Commit();
    retData.resData.personId = person_repo_.GetPersonId(person);
}

}  // namespace cosmo
