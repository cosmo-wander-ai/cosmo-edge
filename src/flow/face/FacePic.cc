// FacePic — Called frequently

#include "flow/face/FacePic.h"

#include <filesystem>

//#include <DirectoryManager.h>
#include "util/Log.h"

namespace cosmo {

FacePic::FacePic(const std::string& id, const std::string& path, AiFeature feature)
    : pic_id_(id), pic_path_(path), feature_(std::move(feature)) {}

FacePic::FacePic(FacePic&&) noexcept = default;

FacePic::~FacePic() {
    if (!person_.lock()) {
        RemovePictureFile();
    }
}

void FacePic::RemovePictureFile() {
    auto pic_name = (std::filesystem::path(pic_path_) / (pic_id_ + ".jpg")).string();
    remove(pic_name.c_str());
    LOG_INFO("remove {}", pic_name);
}

std::shared_ptr<Person> FacePic::GetPerson() {
    return person_.lock();
}

void FacePic::SetPerson(std::shared_ptr<Person> person) {
    person_ = person;
}

}  // namespace cosmo
