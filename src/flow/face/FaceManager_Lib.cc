// FaceManager_Lib — Face Manager_ Lib implementation.

#include <algorithm>

#include "flow/face/FaceManager.h"
#include "util/Exception.h"

namespace cosmo {

std::vector<FaceLibPtr> FaceManager::GetFaceLibs(std::vector<std::string> lib_ids) const {
    std::shared_lock<std::shared_mutex> lock(mtx_);
    std::vector<FaceLibPtr> libs;

    for (auto& id : lib_ids) {
        auto it = find_if(face_libs_.begin(), face_libs_.end(),
                          [&id](FaceLibPtr lib) { return lib->GetId() == id; });
        if (it != face_libs_.end() && *it) {
            libs.push_back(*it);
        }
    }
    return libs;
}

bool FaceManager::IsValidLibName(const std::string& face_lib_id, const std::string& face_lib_name) const {
    std::shared_lock<std::shared_mutex> lock(mtx_);

    auto it = find_if(face_libs_.begin(), face_libs_.end(), [&face_lib_id, &face_lib_name](FaceLibPtr lib) {
        return lib->GetName() == face_lib_name && face_lib_id != lib->GetId();
    });
    return it == face_libs_.end();
}

util::ErrorEnum FaceManager::AddFaceLib(FaceLibPtr face_lib) {
    std::lock_guard<std::shared_mutex> lock(mtx_);
    auto it = find_if(face_libs_.begin(), face_libs_.end(), [&face_lib](const FaceLibPtr& lib) {
        return face_lib->GetId() == lib->GetId() || face_lib->GetName() == lib->GetName();
    });
    if (it != face_libs_.end()) {
        return (*it)->GetId() == face_lib->GetId() ? util::ErrorEnum::ExistedId
                                                   : util::ErrorEnum::ExistedName;
    }
    face_libs_.emplace_back(face_lib);
    return util::ErrorEnum::Success;
}

util::ErrorEnum FaceManager::UpdateFaceLib(const std::string& face_lib_id,
                                           MsgBaseFaceLibInfo&& face_lib_info) {
    if (auto face_lib = GetFaceLib(face_lib_id)) {
        if (!face_lib_info.name.empty()) {
            if (!IsValidLibName(face_lib_id, face_lib_info.name)) {
                throw util::ErrorMessage(util::ErrorEnum::ExistedName);
            }
        }
        if (static_cast<size_t>(face_lib_info.maxFaceNumber) < face_lib->GetFaceCount()) {
            throw util::ErrorMessage(
                util::ErrorEnum::ParameterException,
                "Face library capacity cannot be less than the number of existing faces");
        }

        face_lib->SetData(std::move(face_lib_info));
        return util::ErrorEnum::Success;
    }
    return util::ErrorEnum::NoSuchId;
}

util::ErrorEnum FaceManager::RemoveFaceLib(const std::string& lib_id) {
    std::lock_guard<std::shared_mutex> lock(mtx_);
    auto it = find_if(face_libs_.begin(), face_libs_.end(),
                      [&lib_id](const FaceLibPtr& face_lib) { return lib_id == face_lib->GetId(); });
    if (it != face_libs_.end()) {
        if ((*it)->GetPersonCount() > 0) {
            return util::ErrorEnum::FaceLibNotEmpty;
        }
        face_libs_.erase(it);
        return util::ErrorEnum::Success;
    }
    return util::ErrorEnum::NoSuchId;
}

std::vector<MsgResultFaceLibInfo> FaceManager::RemoveFaceLib(const std::vector<std::string>& lib_id_list) {
    std::vector<MsgResultFaceLibInfo> res;
    for (auto& id : lib_id_list) {
        auto errc = RemoveFaceLib(id);
        MsgResultFaceLibInfo info{};
        info.failedFaceLibId = id;
        info.resCode         = static_cast<int>(errc);
        info.resMsg          = make_error_condition(errc).message();
        res.push_back(std::move(info));
    }
    return res;
}

FaceLibPtr FaceManager::GetFaceLib(const std::string& lib_id) const {
    std::shared_lock<std::shared_mutex> lock(mtx_);
    auto it = find_if(face_libs_.begin(), face_libs_.end(),
                      [&lib_id](FaceLibPtr face_lib) { return lib_id == face_lib->GetId(); });
    if (it != face_libs_.end()) {
        return (*it);
    }
    return nullptr;
}

std::vector<FaceLibPtr> FaceManager::GetAllFaceLibs() const {
    std::shared_lock<std::shared_mutex> lock(mtx_);
    return face_libs_;
}

size_t FaceManager::GetFaceLibMaxCount() const {
    std::shared_lock<std::shared_mutex> lock(mtx_);
    return max_face_lib_number_;
}

}  // namespace cosmo
