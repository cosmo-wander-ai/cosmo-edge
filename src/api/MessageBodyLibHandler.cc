// MessageBodyLibHandler — Message Body Lib Handler implementation.

#include "api/MessageBodyLibHandler.h"

#include <algorithm>
#include <filesystem>

#include "db/TransactionGuard.h"
#include "service/camera/ICameraTaskConfig.h"
#include "service/face/IBodyLibService.h"
#include "service/face/IPersonRecogDaoService.h"
#include "service/media/IVideoFrameCodec.h"
#include "util/CipherUtil.h"
#include "util/ErrorCode.h"
#include "util/FileUtil.h"
#include "util/Keys.h"
#include "util/PathUtil.h"
#include "util/StringUtil.h"
#include "util/UuidUtil.h"

namespace cosmo {

static constexpr const char* kTag     = "BodyLibHandler";
static constexpr int kDefaultPageNum  = 1;
static constexpr int kDefaultPageSize = 10;

MessageBodyLibHandler::MessageBodyLibHandler(service::IPersonRecogDaoService& dao_svc,
                                             service::IBodyLibService& body_lib_svc,
                                             service::ICameraTaskConfig& camera_task_config,
                                             service::IVideoFrameCodec& video_codec)
    : dao_svc_(dao_svc),
      body_lib_svc_(body_lib_svc),
      camera_task_config_(camera_task_config),
      video_codec_(video_codec) {}

void MessageBodyLibHandler::InvalidateBodyCache(const std::string& lib_id) {
    body_lib_svc_.InvalidateCache(lib_id);
}

BodyLib::MsgModifyPersonLibSend MessageBodyLibHandler::Handle(BodyLib::MsgModifyPersonLibRecv&& data,
                                                              std::error_condition& errc) {
    BodyLib::MsgModifyPersonLibSend ret{};
    switch (static_cast<Operation>(data.personLibOperation)) {
        case Operation::Add: {
            if (data.personLib.name.empty()) {
                throw util::ErrorMessage(util::ErrorEnum::ParameterException);
            }
            data.personLib.id = util::GenerateUUID();
            db::LibInfo info{data.personLib.id,
                             data.personLib.name,
                             data.personLib.type,
                             data.personLib.threshold,
                             data.personLib.maxPersonNumber,
                             data.personLib.strangerAlarm,
                             data.personLib.strangerThreshold};
            db::TransactionGuard txn(dao_svc_);
            if (!dao_svc_.AddPersonLib(info)) {
                throw util::ErrorMessage(util::ErrorEnum::DatabaseFailed);
            }
            txn.Commit();
            ret.resData.personLibId = data.personLib.id;
            break;
        }
        case Operation::Update: {
            db::LibInfo info{data.personLib.id,
                             data.personLib.name,
                             data.personLib.type,
                             data.personLib.threshold,
                             data.personLib.maxPersonNumber,
                             data.personLib.strangerAlarm,
                             data.personLib.strangerThreshold};
            db::TransactionGuard txn(dao_svc_);
            if (!dao_svc_.UpdatePersonLib(info)) {
                throw util::ErrorMessage(util::ErrorEnum::DatabaseFailed);
            }
            txn.Commit();
            ret.resData.personLibId = data.personLib.id;
            break;
        }
        default:
            errc = util::ErrorEnum::UnknownOperation;
            break;
    }
    // Invalidate body cache for the modified library
    if (!ret.resData.personLibId.empty()) {
        InvalidateBodyCache(ret.resData.personLibId);
    }
    return ret;
}

BodyLib::MsgDeletePersonLibSend MessageBodyLibHandler::Handle(BodyLib::MsgDeletePersonLibRecv&& data,
                                                              std::error_condition& /*errc*/) {
    BodyLib::MsgDeletePersonLibSend ret{};
    for (auto& id : data.personLibIdList) {
        if (!dao_svc_.RemovePersonLib(id.ToString())) {
            MsgResultPersonLibInfo fail{};
            fail.failedPersonLibId = id;
            fail.resCode           = static_cast<uint32_t>(util::ErrorEnum::DatabaseFailed);
            fail.resMsg            = std::error_condition(util::ErrorEnum::DatabaseFailed).message();
            ret.resData.failedPersonLibList.push_back(std::move(fail));
        }
    }
    // Invalidate body cache for deleted libraries
    for (auto& id : data.personLibIdList) {
        InvalidateBodyCache(id);
    }
    return ret;
}

BodyLib::MsgQueryPersonLibInfoSend MessageBodyLibHandler::Handle(BodyLib::MsgQueryPersonLibInfoRecv&& data,
                                                                 std::error_condition& errc) {
    if (data.pageNum < 1 || data.pageSize < 0) {
        data.pageNum  = kDefaultPageNum;
        data.pageSize = kDefaultPageSize;
    }
    BodyLib::MsgQueryPersonLibInfoSend ret{};
    db::PersonRecogLibQueryCondition cond{};
    cond.camera_id             = data.cameraId;
    cond.task_type             = data.taskType;
    cond.person_lib_type       = data.personLibType;
    cond.person_lib_name       = data.personLibName;
    cond.person_lib_id         = data.personLibId;
    cond.page_num              = data.pageNum;
    cond.page_size             = data.pageSize;
    auto dbResult              = dao_svc_.QueryPersonLib(cond);
    ret.resData.searchAll      = dbResult.search_all;
    ret.resData.personLibCount = dbResult.person_lib_count;
    for (auto& r : dbResult.person_lib_list) {
        MsgQueryPersonLibInfoS::PersonFeatureLib lib{};
        lib.id                = r.id;
        lib.name              = r.name;
        lib.type              = r.type;
        lib.threshold         = r.threshold;
        lib.maxPersonNumber   = r.max_person_number;
        lib.strangerAlarm     = r.stranger_alarm;
        lib.strangerThreshold = r.stranger_threshold;
        lib.personNumber      = r.person_number;
        lib.createTimestamp   = r.create_timestamp;
        lib.updateTimestamp   = r.update_timestamp;
        ret.resData.personLibList.push_back(std::move(lib));
    }
    if (ret.resData.personLibCount <= 0 && !data.personLibId.empty()) {
        errc = util::ErrorEnum::NoSuchId;
    }
    return ret;
}

BodyLib::MsgQueryPersonPicturesSend MessageBodyLibHandler::Handle(BodyLib::MsgQueryPersonPicturesRecv&& data,
                                                                  std::error_condition& /*errc*/) {
    if (data.pageNum < 1 || data.pageSize < 1) {
        throw util::ErrorMessage(util::ErrorEnum::ParameterException, "Pagination cannot be less than 1");
    }
    BodyLib::MsgQueryPersonPicturesSend ret{};
    db::PersonRecogQueryCondition cond{};
    cond.person_lib_id_list = data.personLibIdList;
    cond.person_lib_type    = data.personLibType;
    cond.person_id          = data.personId;
    cond.picture_name       = data.pictureName;
    cond.page_num           = data.pageNum;
    cond.page_size          = data.pageSize;
    cond.query_id           = data.queryId;
    auto dbResult           = dao_svc_.QueryPersons(cond);
    ret.resData.queryId     = dbResult.query_id;
    ret.resData.totalCount  = dbResult.total_count;
    for (auto& r : dbResult.person_list) {
        MsgQueryPersonPicturesS::Person person{};
        person.id              = r.id;
        person.pictureName     = r.picture_name;
        person.createTimestamp = r.create_timestamp;
        person.updateTimestamp = r.update_timestamp;
        person.feature         = std::move(r.feature);
        person.personLib.id    = r.person_lib.id;
        person.personLib.name  = r.person_lib.name;
        // Complete image URL to web path
        std::string imgFile = person.pictureName;
        if (imgFile.empty()) {
            // Fallback: old data may not have pictureName, use personId + ".jpg"
            imgFile = person.id + ".jpg";
        }
        person.pictureUrl = cosmo::path::GetWebDir(
            (std::filesystem::path(cosmo::path::GetPersonLibPhotoDir()) / imgFile).string());
        ret.resData.personList.push_back(std::move(person));
    }
    return ret;
}

BodyLib::MsgAddLibPersonSend MessageBodyLibHandler::Handle(BodyLib::MsgAddLibPersonRecv&& data,
                                                           std::error_condition& /*errc*/) {
    BodyLib::MsgAddLibPersonSend ret{};
    if (static_cast<Operation>(data.personOperation) != Operation::Add) {
        throw util::ErrorMessage(util::ErrorEnum::OperationNotSupport);
    }
    if (data.personList.empty()) {
        throw util::ErrorMessage(util::ErrorEnum::PersonElementNotEmpty,
                                 "Workwear count for single photo cannot be empty");
    }

    for (auto& personSrc : data.personList) {
        if (personSrc.pictureUrl.empty()) {
            continue;
        }
        // pictureUrl may be web path (/weblibPic/…) or local relative path
        std::string fileName = std::filesystem::path(personSrc.pictureUrl.ToString()).filename().string();
        std::string srcPath =
            (std::filesystem::path(cosmo::path::GetPersonLibPhotoDir()) / fileName).string();
        if (!util::FileExist(srcPath)) {
            // Fallback: try old BaseDir + pictureUrl method
            srcPath =
                (std::filesystem::path(cosmo::path::GetBaseDir()) / personSrc.pictureUrl.ToString()).string();
        }
        if (!util::FileExist(srcPath)) {
            LOG_WARN("{} AddLibPerson skip: file not found, pictureUrl:{} resolved:{}", kTag,
                     personSrc.pictureUrl, srcPath);
            continue;
        }

        std::string personId = util::GenerateUUID();

        // Read image and extract body feature
        std::vector<float> finalFeature;
        try {
            auto picBin    = util::ReadFileBin(srcPath);
            auto imageData = video_codec_.DecodeJpeg(picBin);
            if (imageData) {
                finalFeature = body_lib_svc_.ExtractBodyFeature(imageData);
            } else {
                LOG_WARN("{} Decode Jpeg failed for {}", kTag, srcPath);
            }
        } catch (const std::exception& e) {
            LOG_WARN("{} Feature extraction exception: {}", kTag, e.what());
        }

        std::string dstFileName = personId + ".jpg";
        if (!dao_svc_.AddPerson(personId, data.personLibId.ToString(), dstFileName, finalFeature)) {
            continue;
        }

        std::string dstPath =
            (std::filesystem::path(cosmo::path::GetPersonLibPhotoDir()) / (personId + ".jpg")).string();
        std::error_code ec;
        std::filesystem::copy_file(srcPath, dstPath, std::filesystem::copy_options::overwrite_existing, ec);
        if (ec) {
            // DB insert succeeded; only log image copy failure
            LOG_WARN("{} copy person image failed:{} -> {}, err:{}", kTag, srcPath, dstPath, ec.message());
        }
        ret.resData.personId.push_back(personId);
    }
    // Invalidate body cache for the modified library
    if (!ret.resData.personId.empty()) {
        InvalidateBodyCache(data.personLibId);
    }
    return ret;
}

BodyLib::MsgBindTaskPersonLibSend MessageBodyLibHandler::Handle(BodyLib::MsgBindTaskPersonLibRecv&& data,
                                                                std::error_condition& errc) {
    BodyLib::MsgBindTaskPersonLibSend ret{};
    std::vector<std::string> bindLibs;

    if (data.searchAll || data.personLibId.empty()) {
        bindLibs = dao_svc_.GetAllPersonLibs();
    } else {
        auto allLibs = dao_svc_.GetAllPersonLibs();
        for (auto& id : data.personLibId) {
            auto sid = id.ToString();
            auto it  = std::find(allLibs.begin(), allLibs.end(), sid);
            if (it != allLibs.end()) {
                bindLibs.push_back(sid);
            } else {
                MsgResultPersonLibInfo info{};
                info.failedPersonLibId = sid;
                info.resCode           = static_cast<uint32_t>(util::ErrorEnum::NoSuchId);
                info.resMsg            = "Bound body library does not exist";
                ret.resData.failedPersonLibList.push_back(std::move(info));
            }
        }
    }

    errc = camera_task_config_.BindTaskLibPara(data.cameraId, data.algorithmCode, bindLibs,
                                               std::string(key::target::PARAM_WORKCLOTHES_SET));
    return ret;
}

BodyLib::MsgDeleteLibPersonSend MessageBodyLibHandler::Handle(BodyLib::MsgDeleteLibPersonRecv&& data,
                                                              std::error_condition& errc) {
    BodyLib::MsgDeleteLibPersonSend ret{};
    if (data.removeAll) {
        if (!data.personLibId.empty()) {
            if (!dao_svc_.ClearPersonLib(data.personLibId)) {
                errc = util::ErrorEnum::DatabaseFailed;
            }
        } else {
            for (const auto& id : dao_svc_.GetAllPersonLibs()) {
                dao_svc_.ClearPersonLib(id);
            }
        }
        return ret;
    }
    for (auto& personId : data.personIdList) {
        if (!dao_svc_.RemovePerson(personId)) {
            MsgResultInfo fail{};
            fail.id      = personId;
            fail.resCode = static_cast<uint32_t>(util::ErrorEnum::DatabaseFailed);
            fail.resMsg  = std::error_condition(util::ErrorEnum::DatabaseFailed).message();
            ret.failedList.push_back(std::move(fail));
        }
    }
    // Invalidate body cache for affected libraries
    if (!data.personLibId.empty()) {
        InvalidateBodyCache(data.personLibId);
    } else {
        body_lib_svc_.InvalidateAll();
    }
    return ret;
}

BodyLib::MsgDetectPersonSend MessageBodyLibHandler::Handle(BodyLib::MsgDetectPersonRecv&& data,
                                                           std::error_condition& /*errc*/) {
    BodyLib::MsgDetectPersonSend ret{};
    if (data.imageBase64.empty()) {
        throw util::ErrorMessage(util::ErrorEnum::PersonElementNotEmpty, "Photo to detect is empty");
    }
    auto picBin = util::DecBase64Vec(data.imageBase64);
    auto frame  = video_codec_.DecodeJpeg(picBin);
    if (!frame) {
        throw util::ErrorMessage(util::ErrorEnum::ImageDecodeFailed, "Image decode failed");
    }
    auto jpeg = video_codec_.EncodeJpeg(frame);
    if (jpeg.empty()) {
        throw util::ErrorMessage(util::ErrorEnum::DecodeFailed, "Workwear photo encoding failed");
    }
    std::string jpgName = util::GenerateUUID() + ".jpg";
    std::string jpgPath = (std::filesystem::path(cosmo::path::GetPersonLibPhotoDir()) / jpgName).string();
    if (!util::WriteFile(jpgPath, reinterpret_cast<const uint8_t*>(jpeg.data()),
                         static_cast<int>(jpeg.size()))) {
        throw util::ErrorMessage(util::ErrorEnum::Failed, "Write file failed");
    }
    ret.resData.pictureUrl = cosmo::path::GetWebDir(jpgPath);
    return ret;
}

BodyLib::MsgGetPersonPictureSend MessageBodyLibHandler::Handle(BodyLib::MsgGetPersonPictureRecv&& data,
                                                               std::error_condition& errc) {
    BodyLib::MsgGetPersonPictureSend ret{};
    auto picture = camera_task_config_.CaptureImage(data.cameraId);
    if (!picture) {
        errc = util::ErrorEnum::ImageCatchFailed;
        return ret;
    }
    auto jpeg = video_codec_.EncodeJpeg(picture);
    if (jpeg.empty()) {
        errc = util::ErrorEnum::DecodeFailed;
        return ret;
    }
    std::string jpgName = util::GenerateUUID() + ".jpg";
    std::string jpgPath = (std::filesystem::path(cosmo::path::GetPersonLibPhotoDir()) / jpgName).string();
    if (!util::WriteFile(jpgPath, reinterpret_cast<const uint8_t*>(jpeg.data()),
                         static_cast<int>(jpeg.size()))) {
        errc = util::ErrorEnum::Failed;
        return ret;
    }
    ret.resData.pictureUrl = cosmo::path::GetWebDir(jpgPath);
    return ret;
}
}  // namespace cosmo
