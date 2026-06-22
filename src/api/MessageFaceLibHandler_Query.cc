// MessageFaceLibHandler_Query — Message Face Lib Handler_ Query implementation.

#include <algorithm>
#include <filesystem>
#include <numeric>

#include "api/MessageFaceLibHandler.h"
#include "service/face/IFaceLibRepo.h"
#include "service/face/IFaceLibService.h"
#include "service/face/IPersonRepo.h"
#include "service/face/dto/FaceDto.h"
#include "util/ErrorCode.h"
#include "util/Exception.h"
#include "util/PathUtil.h"

namespace fs = std::filesystem;

namespace cosmo {

namespace {
    constexpr int kDefaultPageSize = 1000;
}

// Face lib info query
Lib::MsgQueryFaceLibInfoSend MessageFaceLibHandler::Handle(Lib::MsgQueryFaceLibInfoRecv&& data,
                                                           std::error_condition& /*errc*/) {
    if (data.pageNum < 1 || data.pageSize < 0) {
        data.pageNum  = 1;
        data.pageSize = kDefaultPageSize;
    }
    Lib::MsgQueryFaceLibInfoSend retData{};
    if (data.cameraId.empty()) {
        auto faceLibList = lib_repo_.GetAllFaceLibs();

        // Convert to DTO views for filtering and sorting
        std::vector<service::FaceLibView> views;
        views.reserve(faceLibList.size());
        for (const auto& lib : faceLibList) {
            views.push_back(service::FaceLibView::From(lib));
        }

        // Filter by faceLibId
        if (!data.faceLibId.empty()) {
            views.erase(std::remove_if(views.begin(), views.end(),
                                       [&](const service::FaceLibView& v) {
                                           return v.id != data.faceLibId.ToRefString();
                                       }),
                        views.end());
        }
        // Filter by faceLibName
        if (!data.faceLibName.empty()) {
            views.erase(std::remove_if(views.begin(), views.end(),
                                       [&](const service::FaceLibView& v) {
                                           return v.name.find(data.faceLibName.ToRefString()) ==
                                                  std::string::npos;
                                       }),
                        views.end());
        }

        // Sort by creation time (descending)
        std::sort(views.begin(), views.end(),
                  [](const auto& a, const auto& b) { return a.createTime > b.createTime; });

        retData.resData.faceLibCount = views.size();

        for (size_t i = (data.pageNum - 1) * data.pageSize;
             i < views.size() && i < static_cast<size_t>(data.pageNum * data.pageSize); ++i) {
            const auto& v = views[i];
            Lib::MsgQueryFaceLibInfoSend::FaceLib lib{};
            lib                 = v.data;
            lib.faceNumber      = v.faceCount;
            lib.personNumber    = v.personCount;
            lib.createTimestamp = v.createTime;
            lib.updateTimestamp = v.createTime;
            retData.resData.faceLibList.push_back(std::move(lib));
        }

    } else {
        // reserved for camera-based query
    }

    return retData;
}

// Face lib person query
Lib::MsgQueryFacesSend MessageFaceLibHandler::Handle(Lib::MsgQueryFacesRecv&& data,
                                                     std::error_condition& /*errc*/) {
    if (data.pageNum < 1 || data.pageSize < 1) {
        throw util::ErrorMessage(util::ErrorEnum::ParameterException, "Pagination cannot be less than 1");
    }

    Lib::MsgQueryFacesSend result{};
    auto personList = person_repo_.GetAllPerson();

    // Build PersonDetailView snapshots for filtering/sorting/rendering
    std::vector<service::PersonDetailView> details;
    details.reserve(personList.size());
    for (const auto& p : personList) {
        details.push_back(service::PersonDetailView::From(p));
    }

    // Filter — erase non-matching entries
    auto filterDetails = [&](auto pred) {
        size_t write = 0;
        for (size_t read = 0; read < details.size(); ++read) {
            if (!pred(read)) {
                if (write != read) {
                    details[write] = std::move(details[read]);
                }
                ++write;
            }
        }
        details.resize(write);
    };

    if (!data.faceLibIdList.empty()) {
        filterDetails([&](size_t i) {
            const auto& libIds = details[i].faceLibIds;
            return std::search(libIds.begin(), libIds.end(), data.faceLibIdList.begin(),
                               data.faceLibIdList.end()) == libIds.end();
        });
    }
    if (!data.personId.empty()) {
        filterDetails([&](size_t i) { return details[i].basic.id != data.personId.ToRefString(); });
    }
    if (!data.personName.empty()) {
        filterDetails(
            [&](size_t i) { return details[i].basic.name.find(data.personName) == std::string::npos; });
    }
    if (!data.serialNumber.empty()) {
        filterDetails([&](size_t i) {
            return details[i].basic.serialNumber.find(data.serialNumber) == std::string::npos;
        });
    }

    // Sort by creation time (descending) using indices to keep views aligned
    std::vector<size_t> indices(details.size());
    std::iota(indices.begin(), indices.end(), 0);
    std::sort(indices.begin(), indices.end(), [&details](size_t a, size_t b) {
        return details[a].basic.createTime > details[b].basic.createTime;
    });

    result.resData.totalCount = details.size();

    for (size_t idx = (data.pageNum - 1) * data.pageSize;
         idx < static_cast<size_t>(data.pageNum * data.pageSize) && idx < indices.size(); ++idx) {
        size_t i           = indices[idx];
        const auto& detail = details[i];

        Lib::MsgQueryFacesSend::Person tempPerson{};
        tempPerson.id              = detail.basic.id;
        tempPerson.name            = detail.basic.name;
        tempPerson.serialNumber    = detail.basic.serialNumber;
        tempPerson.createTimestamp = detail.basic.createTime;
        tempPerson.updateTimestamp = detail.basic.updateTime;

        for (const auto& picView : detail.pictures) {
            Lib::MsgQueryFacesSend::Picture tempFace{};
            tempFace.id  = picView.id;
            tempFace.url = cosmo::path::GetWebDir(
                (fs::path(cosmo::path::GetFaceLibPhotoDir()) / tempFace.id).concat(".jpg"));
            tempPerson.pictureList.push_back(std::move(tempFace));
        }

        for (const auto& libView : detail.faceLibs) {
            Lib::MsgQueryFacesSend::FaceLib tempFaceLib{};
            tempFaceLib.id   = libView.id;
            tempFaceLib.name = libView.name;
            tempPerson.faceLibIdList.push_back(std::move(tempFaceLib));
        }

        result.resData.personList.push_back(std::move(tempPerson));
    }

    result.resData.queryId = face_lib_svc_.SetQueryCond(data);
    return result;
}

}  // namespace cosmo
