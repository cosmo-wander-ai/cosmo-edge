#pragma once

/*
 * @Author: shiwei
 * @Date: 2020-04-01 14:45:43
 * @LastEditors: shiwei
 * @LastEditTime: 2021-04-16 11:41:53
 * @Description: Person
 */

#include <map>
#include <memory>
#include <shared_mutex>
#include <string>
#include <vector>

#include "flow/face/FaceLib.h"
#include "flow/face/FacePic.h"

namespace cosmo {
class Person : public std::enable_shared_from_this<Person> {
public:
    // Create new person
    explicit Person(const std::string &personId);
    ~Person();

    // Person ID
    const std::string &GetId() const;
    // Person name
    const std::string &GetName() const;
    void SetName(const std::string &name);
    // Number
    std::string GetSerialNumber() const;
    void SetSerialNumber(const std::string &number);
    // Update time
    int64_t GetUpdateTime() const;
    void SetUpdateTime(int64_t updateTime);
    // Creation time
    int64_t GetCreateTime() const;
    void SetCreateTime(int64_t createTime);

    // Extra attributes
    void SetAttribute(const std::string &key, const std::string &value);
    std::string GetAttribute(const std::string &key) const;

    // Add person to face library
    void AddFaceLib(FaceLibPtr faceLib);
    // Delete person from face library
    std::error_condition RemoveFaceLib(FaceLibPtr faceLib);
    // Get face library the person belongs to
    std::vector<FaceLibPtr> GetFaceLibs() const;
    std::vector<std::string> GetFaceLibId() const;

    // Add photo (returns true on success, false if limit reached)
    void AddPicture(FacePicPtr pic);
    // Delete a photo
    std::error_condition RemovePicture(const std::string &faceId);
    // Get all photos
    std::vector<FacePicPtr> GetPictures() const;
    // Get photo count
    size_t GetPictureCount() const;

    // Check if person is in face library
    bool IsInFaceLibs(const std::vector<std::string> &facelibs);

private:
    std::string m_personId;
    std::string m_personName;
    int64_t m_updateTime;
    int64_t m_createTime;
    std::map<std::string, std::string> m_attribute;
    std::vector<FaceLibWPtr> m_faceLibs;
    std::vector<FacePicWPtr> m_facePics;
    mutable std::shared_mutex m_mtx;

private:
    // std::shared_ptr<db::PersonDao> m_dbPerson;
};

using PersonPtr = std::shared_ptr<Person>;
}  // namespace cosmo
