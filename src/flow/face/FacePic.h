#pragma once

/*
 * Face attributes
 */

//#include <UnifiedImage.h>

#include <memory>
#include <shared_mutex>
#include <string>
#include <vector>

#include "infer/AiCommon.h"

namespace cosmo {
class Person;
class FacePic {
public:
    // Read data from database
    FacePic(const std::string &id, const std::string &path, AiFeature feature);
    FacePic(FacePic &&) noexcept;
    ~FacePic();

    // Photo ID
    [[nodiscard]] const std::string &GetId() const;

    // Get feature value (from DB or extracted when adding photo)
    [[nodiscard]] const AiFeature &GetFeature() const;

    // Delete stored photo and DB info
    void RemovePictureFile();

    // Get person the photo belongs to
    [[nodiscard]] std::shared_ptr<Person> GetPerson();
    void SetPerson(std::shared_ptr<Person> person);

private:
    // Read database
    bool LoadData();

private:
    std::string pic_id_;
    std::string pic_path_;
    AiFeature feature_;
    std::weak_ptr<Person> person_;
};

using FacePicPtr  = std::shared_ptr<FacePic>;
using FacePicWPtr = std::weak_ptr<FacePic>;

inline const std::string &FacePic::GetId() const {
    return pic_id_;
}

// Called frequently
inline const AiFeature &FacePic::GetFeature() const {
    return feature_;
}

}  // namespace cosmo
