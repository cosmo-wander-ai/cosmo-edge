/// @file IFaceLibRepo.h
/// @brief Face library repository — CRUD operations for face library entities.
///        ISP split from IFaceLibService.
///        Consumed by MessageFaceLibHandler (lib add/update/delete/query).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "service/face/dto/FaceLibFwd.h"
#include "util/ErrorCode.h"
#include "util/dto/EventMsgTypes.h"

namespace cosmo::service {

/// Face library entity management operations.
///
/// Provides create, read, update, and delete operations for face libraries,
/// including library capacity management.
class IFaceLibRepo {
public:
    virtual ~IFaceLibRepo() = default;

    /// Get all registered face libraries.
    /// @return Vector of all face library pointers.
    virtual std::vector<cosmo::FaceLibPtr> GetAllFaceLibs() = 0;

    /// Get the maximum allowed number of face libraries.
    virtual size_t GetFaceLibMaxCount() const = 0;

    /// Add a new face library.
    /// @param faceLib Face library object to register.
    /// @return ErrorEnum::kSuccess on success.
    virtual cosmo::util::ErrorEnum AddFaceLib(cosmo::FaceLibPtr faceLib) = 0;

    /// Update face library metadata.
    /// @param faceLibId Library identifier.
    /// @param info      Updated metadata (moved in).
    /// @return ErrorEnum::kSuccess on success.
    virtual cosmo::util::ErrorEnum UpdateFaceLib(const std::string& faceLibId,
                                                 cosmo::MsgBaseFaceLibInfo&& info) = 0;

    /// Remove face libraries by ID list.
    /// @param libIdList List of library IDs to remove.
    /// @return Vector of removal results per library.
    virtual std::vector<cosmo::MsgResultFaceLibInfo> RemoveFaceLib(
        const std::vector<std::string>& libIdList) = 0;

    /// Get face libraries by ID list.
    /// @param libIds List of library IDs to retrieve.
    /// @return Vector of matching face library pointers.
    virtual std::vector<cosmo::FaceLibPtr> GetFaceLibs(std::vector<std::string> libIds) const = 0;

    /// Get a single face library by ID.
    /// @param libId Library identifier.
    /// @return Face library pointer, or nullptr if not found.
    virtual cosmo::FaceLibPtr GetFaceLib(const std::string& libId) const = 0;

    /// Create a new face library domain object from metadata.
    /// The library is NOT registered until AddFaceLib() is called.
    /// @param data Library metadata.
    /// @param outId [out] Generated library ID.
    /// @return Newly created face library pointer.
    virtual cosmo::FaceLibPtr CreateFaceLib(cosmo::MsgBaseFaceLibInfo&& data, std::string& outId) = 0;
};

}  // namespace cosmo::service
