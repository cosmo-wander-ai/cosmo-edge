// MessageThingsLibHandler — Message Things Lib Handler implementation.

#include "api/MessageThingsLibHandler.h"

#include <algorithm>
#include <filesystem>
#include <fstream>

#include "db/TransactionGuard.h"
#include "service/camera/ICameraTaskConfig.h"
#include "service/face/IArticlesReidDaoService.h"
#include "service/media/IVideoFrameCodec.h"
#include "util/CipherUtil.h"
#include "util/ErrorCode.h"
#include "util/FileUtil.h"
#include "util/Keys.h"
#include "util/PathUtil.h"
#include "util/StringUtil.h"
#include "util/UuidUtil.h"

namespace cosmo {

namespace {

    // Build web-accessible URL from a photo directory and relative filename.
    std::string BuildThingsPictureUrl(const std::string& relative_path) {
        if (relative_path.empty() || relative_path[0] == '/') {
            return relative_path;
        }
        return cosmo::path::GetWebDir(
            (std::filesystem::path(cosmo::path::GetArticlesReidLibPhotoDir()) / relative_path).string());
    }

}  // namespace

MessageThingsLibHandler::MessageThingsLibHandler(service::IArticlesReidDaoService& dao_svc,
                                                 service::ICameraTaskConfig& camera_task_config,
                                                 service::IVideoFrameCodec& video_codec)
    : dao_svc_(dao_svc), camera_task_config_(camera_task_config), video_codec_(video_codec) {}

ThingsLib::MsgModifyThingsLibSend MessageThingsLibHandler::Handle(ThingsLib::MsgModifyThingsLibRecv&& data,
                                                                  std::error_condition& errc) {
    ThingsLib::MsgModifyThingsLibSend ret{};
    switch (static_cast<Operation>(data.thingsLibOperation)) {
        case Operation::Add: {
            if (data.thingsLib.name.empty()) {
                throw util::ErrorMessage(util::ErrorEnum::ParameterException);
            }
            data.thingsLib.id = util::GenerateUUID();
            db::LibInfo info{data.thingsLib.id,
                             data.thingsLib.name,
                             data.thingsLib.type,
                             data.thingsLib.threshold,
                             data.thingsLib.maxThingsNumber,
                             data.thingsLib.strangerAlarm,
                             data.thingsLib.strangerThreshold};
            db::TransactionGuard guard(dao_svc_);
            if (!dao_svc_.AddArticlesReidLib(info)) {
                throw util::ErrorMessage(util::ErrorEnum::DatabaseFailed);
            }
            guard.Commit();
            ret.resData.thingsLibId = data.thingsLib.id;
            break;
        }
        case Operation::Update: {
            db::LibInfo info{data.thingsLib.id,
                             data.thingsLib.name,
                             data.thingsLib.type,
                             data.thingsLib.threshold,
                             data.thingsLib.maxThingsNumber,
                             data.thingsLib.strangerAlarm,
                             data.thingsLib.strangerThreshold};
            db::TransactionGuard guard(dao_svc_);
            if (!dao_svc_.UpdateArticlesReidLib(info)) {
                throw util::ErrorMessage(util::ErrorEnum::DatabaseFailed);
            }
            guard.Commit();
            ret.resData.thingsLibId = data.thingsLib.id;
            break;
        }
        default:
            errc = util::ErrorEnum::UnknownOperation;
            break;
    }
    return ret;
}

ThingsLib::MsgDeleteThingsLibSend MessageThingsLibHandler::Handle(ThingsLib::MsgDeleteThingsLibRecv&& data,
                                                                  std::error_condition& /*errc*/) {
    ThingsLib::MsgDeleteThingsLibSend ret{};
    for (auto& id : data.thingsLibIdList) {
        if (!dao_svc_.RemoveArticlesReidLib(id.ToString())) {
            MsgResultThingsLibInfo fail{};
            fail.failedThingsLibId = id;
            fail.resCode           = static_cast<uint32_t>(util::ErrorEnum::DatabaseFailed);
            fail.resMsg            = std::error_condition(util::ErrorEnum::DatabaseFailed).message();
            ret.resData.failedThingsLibList.push_back(std::move(fail));
        }
    }
    return ret;
}

ThingsLib::MsgQueryThingsLibInfoSend MessageThingsLibHandler::Handle(
    ThingsLib::MsgQueryThingsLibInfoRecv&& data, std::error_condition& errc) {
    if (data.pageNum < 1 || data.pageSize < 1) {
        throw util::ErrorMessage(util::ErrorEnum::ParameterException, "Pagination cannot be less than 1");
    }
    ThingsLib::MsgQueryThingsLibInfoSend ret{};
    db::ThingsLibQueryCondition cond{};
    cond.camera_id             = data.cameraId;
    cond.task_type             = data.taskType;
    cond.things_lib_type       = data.thingsLibType;
    cond.things_lib_name       = data.thingsLibName;
    cond.things_lib_id         = data.thingsLibId;
    cond.page_num              = data.pageNum;
    cond.page_size             = data.pageSize;
    auto dbResult              = dao_svc_.QueryThingsLib(cond);
    ret.resData.searchAll      = dbResult.search_all;
    ret.resData.thingsLibCount = dbResult.things_lib_count;
    for (auto& r : dbResult.things_lib_list) {
        MsgQueryThingsLibInfoS::ThingsFeatureLib lib{};
        lib.id                = r.id;
        lib.name              = r.name;
        lib.type              = r.type;
        lib.threshold         = r.threshold;
        lib.maxThingsNumber   = r.max_things_number;
        lib.strangerAlarm     = r.stranger_alarm;
        lib.strangerThreshold = r.stranger_threshold;
        lib.thingsNumber      = r.things_number;
        lib.createTimestamp   = r.create_timestamp;
        lib.updateTimestamp   = r.update_timestamp;
        ret.resData.thingsLibList.push_back(std::move(lib));
    }
    if (ret.resData.thingsLibCount <= 0 && !data.thingsLibId.empty()) {
        errc = util::ErrorEnum::NoSuchId;
    }
    return ret;
}

ThingsLib::MsgQueryThingsPicturesSend MessageThingsLibHandler::Handle(
    ThingsLib::MsgQueryThingsPicturesRecv&& data, std::error_condition& /*errc*/) {
    if (data.pageNum < 1 || data.pageSize < 1) {
        throw util::ErrorMessage(util::ErrorEnum::ParameterException, "Pagination cannot be less than 1");
    }
    ThingsLib::MsgQueryThingsPicturesSend ret{};
    db::ThingsQueryCondition cond{};
    cond.things_lib_id_list = data.thingsLibIdList;
    cond.things_lib_type    = data.thingsLibType;
    cond.things_id          = data.thingsId;
    cond.picture_name       = data.pictureName;
    cond.page_num           = data.pageNum;
    cond.page_size          = data.pageSize;
    cond.query_id           = data.queryId;
    auto dbResult           = dao_svc_.QueryThings(cond);
    ret.resData.queryId     = dbResult.query_id;
    ret.resData.totalCount  = dbResult.total_count;
    for (auto& r : dbResult.things_list) {
        MsgQueryThingsPicturesS::ArticlesReid item{};
        item.id              = r.id;
        item.pictureName     = r.picture_name;
        item.pictureUrl      = r.picture_url;
        item.createTimestamp = r.create_timestamp;
        item.updateTimestamp = r.update_timestamp;
        item.feature         = std::move(r.feature);
        item.thingsLib.id    = r.things_lib.id;
        item.thingsLib.name  = r.things_lib.name;
        item.pictureUrl      = BuildThingsPictureUrl(item.pictureUrl);
        ret.resData.thingsList.push_back(std::move(item));
    }
    return ret;
}

ThingsLib::MsgGetThingsPictureSend MessageThingsLibHandler::Handle(ThingsLib::MsgGetThingsPictureRecv&& data,
                                                                   std::error_condition& errc) {
    ThingsLib::MsgGetThingsPictureSend ret{};
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
    std::string jpgPath =
        (std::filesystem::path(cosmo::path::GetArticlesReidLibPhotoDir()) / jpgName).string();
    if (!util::WriteFile(jpgPath, reinterpret_cast<const uint8_t*>(jpeg.data()),
                         static_cast<int>(jpeg.size()))) {
        errc = util::ErrorEnum::Failed;
        return ret;
    }
    ret.resData.pictureUrl = cosmo::path::GetWebDir(jpgPath);
    return ret;
}

ThingsLib::MsgAddLibThingsSend MessageThingsLibHandler::Handle(ThingsLib::MsgAddLibThingsRecv&& data,
                                                               std::error_condition& /*errc*/) {
    ThingsLib::MsgAddLibThingsSend ret{};
    if (static_cast<Operation>(data.thingsOperation) != Operation::Add) {
        throw util::ErrorMessage(util::ErrorEnum::OperationNotSupport);
    }
    if (data.thingsList.empty()) {
        throw util::ErrorMessage(util::ErrorEnum::ParameterException, "Object list cannot be empty");
    }

    for (auto& thing : data.thingsList) {
        std::string thingId = util::GenerateUUID();
        std::vector<uint8_t> picBin;
        if (!thing.pictureBase64.empty()) {
            picBin = util::DecBase64Vec(thing.pictureBase64);
        } else if (!thing.pictureUrl.empty()) {
            std::string srcPath =
                (std::filesystem::path(cosmo::path::GetBaseDir()) / thing.pictureUrl.ToString()).string();
            if (util::FileExist(srcPath)) {
                std::ifstream ifs(srcPath, std::ios::binary);
                picBin.assign(std::istreambuf_iterator<char>(ifs), std::istreambuf_iterator<char>());
            } else {
                LOG_WARN("AddLibThings skip: source file not found: {}", srcPath);
                continue;
            }
        }
        auto frame = video_codec_.DecodeJpeg(picBin);
        if (!frame) {
            LOG_WARN("AddLibThings skip: JPEG decode failed for thing: {}", thing.pictureName);
            continue;
        }
        auto jpeg = video_codec_.EncodeJpeg(frame);
        if (jpeg.empty()) {
            LOG_WARN("AddLibThings skip: JPEG encode failed for thing: {}", thing.pictureName);
            continue;
        }
        std::string jpgPath =
            (std::filesystem::path(cosmo::path::GetArticlesReidLibPhotoDir()) / (thingId + ".jpg")).string();
        if (!util::WriteFile(jpgPath, reinterpret_cast<const uint8_t*>(jpeg.data()),
                             static_cast<int>(jpeg.size()))) {
            LOG_WARN("AddLibThings skip: write file failed: {}", jpgPath);
            continue;
        }
        // Enter database with empty features first, complete object feature extraction pipeline later
        if (!dao_svc_.AddArticlesReid(thingId, data.thingsLibId.ToString(), thing.pictureName, {})) {
            LOG_WARN("AddLibThings skip: database insert failed for thingId: {}", thingId);
            continue;
        }
        ret.resData.thingsId.push_back(thingId);
    }
    return ret;
}

ThingsLib::MsgBindTaskThingsLibSend MessageThingsLibHandler::Handle(
    ThingsLib::MsgBindTaskThingsLibRecv&& data, std::error_condition& errc) {
    ThingsLib::MsgBindTaskThingsLibSend ret{};
    std::vector<std::string> bindLibs;

    if (data.searchAll || data.thingsLibId.empty()) {
        bindLibs = dao_svc_.GetAllArticlesReidLibs();
    } else {
        auto allLibs = dao_svc_.GetAllArticlesReidLibs();
        for (auto& id : data.thingsLibId) {
            auto sid = id.ToString();
            auto it  = std::find(allLibs.begin(), allLibs.end(), sid);
            if (it != allLibs.end()) {
                bindLibs.push_back(sid);
            } else {
                MsgResultThingsLibInfo info{};
                info.failedThingsLibId = sid;
                info.resCode           = static_cast<uint32_t>(util::ErrorEnum::NoSuchId);
                info.resMsg            = "Bound object library does not exist";
                ret.resData.failedThingsLibList.push_back(std::move(info));
            }
        }
    }

    errc = camera_task_config_.BindTaskLibPara(data.cameraId, data.algorithmCode, bindLibs,
                                               std::string(key::target::PARAM_COMMODITY_SET));
    return ret;
}

ThingsLib::MsgDeleteLibThingsSend MessageThingsLibHandler::Handle(ThingsLib::MsgDeleteLibThingsRecv&& data,
                                                                  std::error_condition& errc) {
    ThingsLib::MsgDeleteLibThingsSend ret{};
    if (data.removeAll) {
        if (!data.thingsLibId.empty()) {
            if (!dao_svc_.ClearArticlesReidLib(data.thingsLibId)) {
                errc = util::ErrorEnum::DatabaseFailed;
            }
        } else {
            for (const auto& id : dao_svc_.GetAllArticlesReidLibs()) {
                if (!dao_svc_.ClearArticlesReidLib(id)) {
                    LOG_WARN("ClearArticlesReidLib failed for lib: {}", id);
                }
            }
        }
        return ret;
    }
    for (auto& thingsId : data.thingsIdList) {
        if (!dao_svc_.RemoveArticlesReid(thingsId)) {
            MsgResultInfo fail{};
            fail.id      = thingsId;
            fail.resCode = static_cast<uint32_t>(util::ErrorEnum::DatabaseFailed);
            fail.resMsg  = std::error_condition(util::ErrorEnum::DatabaseFailed).message();
            ret.failedList.push_back(std::move(fail));
        }
    }
    return ret;
}
}  // namespace cosmo
