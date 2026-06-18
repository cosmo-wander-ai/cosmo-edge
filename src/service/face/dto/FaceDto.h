/// @file FaceDto.h
/// @brief Value-type DTOs for FaceLib, Person, and FacePic entities.
///        Replaces the opaque IFaceAccessor pattern — callers operate on
///        concrete value objects instead of opaque shared pointers.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "service/face/dto/FaceLibFwd.h"
#include "util/AiTypes.h"
#include "util/dto/EventMsgTypes.h"

namespace cosmo::service {

/// Read-only snapshot of a face library's properties.
struct FaceLibView {
    std::string id;
    std::string name;
    size_t faceCount{0};
    size_t faceMaxCount{0};
    size_t personCount{0};
    int64_t createTime{0};
    cosmo::MsgBaseFaceLibInfo data;

    /// Build from a FaceLib domain object.
    static FaceLibView From(const cosmo::FaceLibPtr& lib);
};

/// Read-only snapshot of a face picture's properties.
struct FacePicView {
    std::string id;
    cosmo::AiFeature feature;

    /// Build from a FacePic domain object.
    static FacePicView From(const cosmo::FacePicPtr& pic);
};

/// Read-only snapshot of a person's properties.
struct PersonView {
    std::string id;
    std::string name;
    std::string serialNumber;
    int64_t createTime{0};
    int64_t updateTime{0};
    size_t pictureCount{0};

    /// Build from a Person domain object.
    static PersonView From(const cosmo::PersonPtr& p);
};

/// Extended person snapshot including picture and library membership details.
/// Used by query handlers that need to display full person information.
struct PersonDetailView {
    PersonView basic;
    std::vector<std::string> faceLibIds;
    std::vector<FacePicView> pictures;
    std::vector<FaceLibView> faceLibs;

    /// Build from a Person domain object (includes pictures and face libs).
    static PersonDetailView From(const cosmo::PersonPtr& p);
};

}  // namespace cosmo::service
