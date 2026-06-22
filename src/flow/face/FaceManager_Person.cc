// FaceManager_Person — Face Manager_ Person implementation.

#include <algorithm>
#include <iterator>

#include "flow/face/FaceManager.h"
#include "util/Log.h"

namespace cosmo {

PersonPtr FaceManager::GetPerson(const std::string& person_id) const {
    std::shared_lock<std::shared_mutex> lock(mtx_);
    auto it = person_map_.find(person_id);
    if (it != person_map_.end()) {
        return it->second;
    }
    return nullptr;
}

std::vector<PersonPtr> FaceManager::GetAllPerson() const {
    std::shared_lock<std::shared_mutex> lock(mtx_);
    std::vector<PersonPtr> result;
    result.reserve(person_map_.size());
    std::transform(person_map_.begin(), person_map_.end(), std::back_inserter(result),
                   [](const auto& p) { return p.second; });
    return result;
}

void FaceManager::AddPerson(FaceLibPtr face_lib, PersonPtr person) {
    std::lock_guard<std::shared_mutex> lock(mtx_);
    auto it = person_map_.find(person->GetId());
    if (it == person_map_.end()) {
        person_map_.emplace(person->GetId(), person);
    }
    person->AddFaceLib(face_lib);
}

void FaceManager::UpdatePerson(std::vector<FaceLibPtr> face_lib_list, PersonPtr person) {
    RemovePerson(nullptr, person->GetId());
    for (auto& p_face_lib : face_lib_list) {
        LOG_INFO("faceLibId: {}", p_face_lib->GetId());
        AddPerson(p_face_lib, person);
    }
}

util::ErrorEnum FaceManager::RemovePerson(FaceLibPtr face_lib, const std::string& person_id) {
    std::lock_guard<std::shared_mutex> lock(mtx_);
    auto it_person = person_map_.find(person_id);
    if (it_person != person_map_.end()) {
        util::ErrorEnum errc = util::ErrorEnum::Success;
        if (auto p_person = it_person->second) {
            if (!face_lib) {
                for (auto p_face_lib : p_person->GetFaceLibs()) {
                    p_person->RemoveFaceLib(p_face_lib);
                }
            } else {
                p_person->RemoveFaceLib(face_lib);
            }

            if (p_person->GetFaceLibs().empty()) {
                person_map_.erase(it_person);
            }
        }
        return errc;
    }

    return util::ErrorEnum::NoSuchId;
}

std::vector<MsgResultInfo> FaceManager::RemovePerson(FaceLibPtr face_lib,
                                                     const std::vector<std::string>& person_id_list) {
    std::vector<MsgResultInfo> result;
    for (auto& id : person_id_list) {
        auto errc = RemovePerson(face_lib, id);
        MsgResultInfo info{};
        info.id      = id;
        info.resCode = static_cast<int>(errc);
        info.resMsg  = make_error_condition(errc).message();
        result.push_back(std::move(info));
    }
    return result;
}

util::ErrorEnum FaceManager::RemoveAllPerson(const std::string& face_lib_id) {
    std::vector<std::string> person_id_list;
    person_id_list.reserve(128);
    for (auto& p : person_map_) {
        if (face_lib_id.empty() || p.second->IsInFaceLibs({face_lib_id})) {
            person_id_list.push_back(p.first);
        }
    }
    RemovePerson(GetFaceLib(face_lib_id), person_id_list);
    return util::ErrorEnum::Success;
}

bool FaceManager::IsValidSerialNumber(const std::string& person_id, const std::string& serial_number) {
    if (!serial_number.empty()) {
        std::lock_guard<std::shared_mutex> lock(mtx_);
        for (auto& wp : person_map_) {
            if (wp.first != person_id) {
                if (auto person = wp.second) {
                    if (person->GetAttribute("serialNumber") == serial_number) {
                        return false;
                    }
                }
            }
        }
    }
    return true;
}

}  // namespace cosmo
