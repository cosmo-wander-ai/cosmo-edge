// Person — Person implementation.

#include "flow/face/Person.h"

#include "flow/face/FaceLib.h"
//#include <ErrorCondition.h>
#include <algorithm>

#include "util/Exception.h"
#include "util/UuidUtil.h"

namespace cosmo {

Person::Person(const std::string &personId)
    : m_personId(std::move(personId)), m_updateTime(0), m_createTime(0) {
    if (m_personId.empty()) {
        m_personId = util::GenerateUUID();
    }
}

Person::~Person() {}

void Person::SetName(const std::string &name) {
    std::lock_guard<std::shared_mutex> lock(m_mtx);
    m_personName = std::move(name);
}

std::string Person::GetSerialNumber() const {
    return GetAttribute("serialNumber");
}

void Person::SetSerialNumber(const std::string &number) {
    SetAttribute("serialNumber", number);
}

void Person::SetAttribute(const std::string &key, const std::string &value) {
    std::lock_guard<std::shared_mutex> lock(m_mtx);
    m_attribute[key] = value;
}

std::string Person::GetAttribute(const std::string &key) const {
    std::shared_lock<std::shared_mutex> lock(m_mtx);
    auto it = m_attribute.find(key);
    if (it != m_attribute.end()) {
        return it->second;
    }
    return {};
}

void Person::AddPicture(FacePicPtr pic) {
    std::lock_guard<std::shared_mutex> lock(m_mtx);
    if (m_faceLibs.empty()) {
        // Cannot call AddPicture before AddFaceLib
        throw util::ErrorMessage(util::ErrorEnum::Failed);
    }

    for (auto &wFaceLib : m_faceLibs) {
        if (auto faceLib = wFaceLib.lock()) {
            faceLib->AddFacePic(pic);
        }
    }
    pic->SetPerson(this->shared_from_this());
    auto it = find_if(m_facePics.begin(), m_facePics.end(), [&pic](FacePicWPtr &wPic) {
        if (auto pPic = wPic.lock()) {
            return pPic->GetId() == pic->GetId();
        }
        return false;
    });
    if (it == m_facePics.end()) {
        m_facePics.emplace_back(pic);
    }
}

std::error_condition Person::RemovePicture(const std::string &faceId) {
    std::lock_guard<std::shared_mutex> lock(m_mtx);
    auto it = find_if(m_facePics.begin(), m_facePics.end(), [&faceId](FacePicWPtr &wPic) {
        if (auto pic = wPic.lock()) {
            return faceId == pic->GetId();
        }
        return false;
    });
    if (it != m_facePics.end()) {
        for (auto &faceLib : m_faceLibs) {
            if (auto sp = faceLib.lock()) {
                sp->RemoveFacePic(faceId);
            }
        }
        if (auto pic = it->lock()) {
            pic->SetPerson(nullptr);
        }
        m_facePics.erase(it);
        return util::ErrorEnum::Success;
    }
    return util::ErrorEnum::NoSuchId;
}

std::vector<FacePicPtr> Person::GetPictures() const {
    std::shared_lock<std::shared_mutex> lock(m_mtx);
    std::vector<FacePicPtr> picList;
    picList.reserve(m_facePics.size());
    for (auto &wPic : m_facePics) {
        if (auto pic = wPic.lock()) {
            picList.push_back(pic);
        }
    }
    return picList;
}

size_t Person::GetPictureCount() const {
    std::shared_lock<std::shared_mutex> lock(m_mtx);
    return m_facePics.size();
}

void Person::AddFaceLib(FaceLibPtr faceLib) {
    std::lock_guard<std::shared_mutex> lock(m_mtx);
    m_faceLibs.emplace_back(faceLib);
}

std::error_condition Person::RemoveFaceLib(FaceLibPtr faceLib) {
    std::lock_guard<std::shared_mutex> lock(m_mtx);

    // Delete stored face library
    auto it = find_if(m_faceLibs.begin(), m_faceLibs.end(), [&faceLib](std::weak_ptr<FaceLib> wptr) {
        if (auto sp = wptr.lock()) {
            return sp->GetId() == faceLib->GetId();
        }
        return false;
    });
    if (it == m_faceLibs.end()) {
        return util::ErrorEnum::NoSuchId;
    }
    m_faceLibs.erase(it);

    for (auto &wFace : m_facePics) {
        if (auto face = wFace.lock()) {
            if (m_faceLibs.empty()) {
                // Remove photo association
                face->SetPerson(nullptr);
            }
            // Delete face from face library
            faceLib->RemoveFacePic(face->GetId());
        }
    }

    return util::ErrorEnum::Success;
}

std::vector<FaceLibPtr> Person::GetFaceLibs() const {
    std::shared_lock<std::shared_mutex> lock(m_mtx);
    std::vector<FaceLibPtr> faceLibs;
    for (auto wpLib : m_faceLibs) {
        if (auto spLib = wpLib.lock()) {
            faceLibs.push_back(spLib);
        }
    }
    return faceLibs;
}

std::vector<std::string> Person::GetFaceLibId() const {
    std::shared_lock<std::shared_mutex> lock(m_mtx);
    std::vector<std::string> idList;
    for (auto wpLib : m_faceLibs) {
        if (auto spLib = wpLib.lock()) {
            idList.push_back(spLib->GetId());
        }
    }
    return idList;
}

bool Person::IsInFaceLibs(const std::vector<std::string> &faceLibIds) {
    std::shared_lock<std::shared_mutex> lock(m_mtx);
    for (auto wpLib : m_faceLibs) {
        if (auto spLib = wpLib.lock()) {
            auto it = find(faceLibIds.begin(), faceLibIds.end(), spLib->GetId());
            if (it != faceLibIds.end()) {
                return true;
            }
        }
    }
    return false;
}

const std::string &Person::GetId() const {
    std::shared_lock<std::shared_mutex> lock(m_mtx);
    return m_personId;
}

const std::string &Person::GetName() const {
    std::shared_lock<std::shared_mutex> lock(m_mtx);
    return m_personName;
}

int64_t Person::GetUpdateTime() const {
    std::lock_guard<std::shared_mutex> lock(m_mtx);
    return m_updateTime;
}

void Person::SetUpdateTime(int64_t updateTime) {
    std::lock_guard<std::shared_mutex> lock(m_mtx);
    m_updateTime = updateTime;
}

int64_t Person::GetCreateTime() const {
    std::lock_guard<std::shared_mutex> lock(m_mtx);
    return m_createTime;
}

void Person::SetCreateTime(int64_t createTime) {
    std::lock_guard<std::shared_mutex> lock(m_mtx);
    m_createTime = createTime;
}

}  // namespace cosmo
