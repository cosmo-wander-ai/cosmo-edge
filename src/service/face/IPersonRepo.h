/// @file IPersonRepo.h
/// @brief Person repository — CRUD operations for person entities.
///        ISP split from IFaceLibService.
///        Consumed by MessageFaceLibHandler (person add/edit/delete),
///        PersonImport.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "service/face/dto/FaceLibFwd.h"
#include "util/AiTypes.h"
#include "util/ErrorCode.h"
#include "util/dto/EventMsgTypes.h"

namespace cosmo::service {

/// Person entity management operations.
///
/// Provides create, read, update, and delete operations for persons,
/// including serial number validation and multi-library membership management.
class IPersonRepo {
public:
    virtual ~IPersonRepo() = default;

    // ── Person CRUD ──

    /// Validate that a serial number is unique (excluding a given person).
    /// @param personId     Person ID to exclude from the uniqueness check.
    /// @param serialNumber Serial number to validate.
    /// @return true if the serial number is valid (unique).
    virtual bool IsValidSerialNumber(const std::string& personId, const std::string& serialNumber) = 0;

    /// Get a person by ID.
    /// @param personId Person identifier.
    /// @return Person pointer, or nullptr if not found.
    virtual cosmo::PersonPtr GetPerson(const std::string& personId) const = 0;

    /// Get all registered persons across all libraries.
    /// @return Vector of all person pointers.
    virtual std::vector<cosmo::PersonPtr> GetAllPerson() const = 0;

    /// Add a person to a face library.
    /// @param faceLib  Target face library.
    /// @param spPerson Person to add.
    virtual void AddPerson(cosmo::FaceLibPtr faceLib, cosmo::PersonPtr spPerson) = 0;

    /// Update a person across multiple face libraries.
    /// @param faceLibList Libraries the person belongs to.
    /// @param spPerson    Updated person data.
    virtual void UpdatePerson(std::vector<cosmo::FaceLibPtr> faceLibList, cosmo::PersonPtr spPerson) = 0;

    /// Remove all persons from a face library.
    /// @param faceLibId Library identifier.
    /// @return ErrorEnum::kSuccess on success.
    virtual cosmo::util::ErrorEnum RemoveAllPerson(const std::string& faceLibId) = 0;

    /// Remove specific persons from a face library.
    /// @param faceLib      Target face library.
    /// @param personIdList List of person IDs to remove.
    /// @return Vector of removal results per person.
    virtual std::vector<cosmo::MsgResultInfo> RemovePerson(cosmo::FaceLibPtr faceLib,
                                                           const std::vector<std::string>& personIdList) = 0;

    // ── Factory & Mutation (eliminates API → flow layer dependency) ──

    /// Create a new person entity with a generated UUID.
    /// @return Newly created person pointer.
    virtual cosmo::PersonPtr CreatePerson() = 0;

    /// Get the ID of a person.
    virtual std::string GetPersonId(const cosmo::PersonPtr& person) const = 0;

    /// Get the number of pictures for a person.
    virtual size_t GetPersonPictureCount(const cosmo::PersonPtr& person) const = 0;

    /// Get the creation timestamp of a person.
    virtual int64_t GetPersonCreateTime(const cosmo::PersonPtr& person) const = 0;

    /// Get all pictures for a person.
    virtual std::vector<cosmo::FacePicPtr> GetPersonPictures(const cosmo::PersonPtr& person) const = 0;

    /// Check if a person belongs to the specified face libraries.
    virtual bool IsPersonInFaceLibs(const cosmo::PersonPtr& person,
                                    const std::vector<std::string>& libIds) const = 0;

    /// Update person metadata (name, serial number, timestamps).
    virtual void UpdatePersonMetadata(cosmo::PersonPtr person, const std::string& name,
                                      const std::string& serialNumber, int64_t updateTime,
                                      int64_t createTime) = 0;

    /// Add a picture to a person.
    virtual void AddPersonPicture(cosmo::PersonPtr person, cosmo::FacePicPtr pic) = 0;

    /// Remove a picture from a person.
    virtual void RemovePersonPicture(cosmo::PersonPtr person, const std::string& picId) = 0;

    /// Create a FacePic domain object.
    virtual cosmo::FacePicPtr CreateFacePic(const std::string& id, const std::string& path,
                                            const cosmo::AiFeature& feature) = 0;
};

}  // namespace cosmo::service
