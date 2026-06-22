// FaceDto — Value-type DTOs for FaceLib, Person, and FacePic entities.

#include "service/face/dto/FaceDto.h"

#include "flow/face/FaceLib.h"
#include "flow/face/FacePic.h"
#include "flow/face/Person.h"

namespace cosmo::service {

FaceLibView FaceLibView::From(const cosmo::FaceLibPtr& lib) {
    return {lib->GetId(),          lib->GetName(),       lib->GetFaceCount(), lib->GetFaceMaxCount(),
            lib->GetPersonCount(), lib->GetCreateTime(), lib->GetData()};
}

FacePicView FacePicView::From(const cosmo::FacePicPtr& pic) {
    return {pic->GetId(), pic->GetFeature()};
}

PersonView PersonView::From(const cosmo::PersonPtr& p) {
    return {p->GetId(),         p->GetName(),       p->GetSerialNumber(),
            p->GetCreateTime(), p->GetUpdateTime(), p->GetPictureCount()};
}

PersonDetailView PersonDetailView::From(const cosmo::PersonPtr& p) {
    PersonDetailView detail;
    detail.basic      = PersonView::From(p);
    detail.faceLibIds = p->GetFaceLibId();
    for (auto&& pic : p->GetPictures()) {
        detail.pictures.push_back(FacePicView::From(pic));
    }
    for (auto&& lib : p->GetFaceLibs()) {
        detail.faceLibs.push_back(FaceLibView::From(lib));
    }
    return detail;
}

}  // namespace cosmo::service
