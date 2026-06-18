/// @file IPersonDaoService.h
/// @brief Service interface that encapsulates PersonDao database operations.
///        Eliminates direct SQLite::Database exposure to api/ and flow/ layers.
#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "db/DbTypes.h"

namespace cosmo::service {

/// Encapsulates PersonDao operations behind a service interface.
///
/// Callers in api/ and flow/ layers use this interface instead of
/// directly constructing PersonDao with a raw SQLite::Database handle.
/// The implementation internally manages the database reference.
class IPersonDaoService {
public:
    virtual ~IPersonDaoService() = default;

    // ── Transaction Control ──

    virtual void Begin()    = 0;
    virtual void Commit()   = 0;
    virtual void Rollback() = 0;

    // ── Face Library CRUD ──

    virtual bool AddFaceLib(const db::LibInfo& data)         = 0;
    virtual bool UpdateFaceLib(const db::LibInfo& data)      = 0;
    virtual bool RemoveFaceLib(const std::string& faceLibId) = 0;
    virtual bool ClearFaceLib(const std::string& faceLibId)  = 0;

    // ── Person CRUD ──

    virtual bool AddPerson(const db::PersonCondition& person,
                           const std::vector<std::pair<std::string, std::vector<float>>>& face)    = 0;
    virtual bool AddPerson(db::FaceRegRecordUnit& faceReg)                                         = 0;
    virtual bool UpdatePerson(const db::PersonCondition& person,
                              const std::vector<std::pair<std::string, std::vector<float>>>& face) = 0;
    virtual bool RemovePerson(const std::string& personId)                                         = 0;

    // ── Queries ──

    virtual db::FaceLibQueryResult QueryFaceLib(const db::FaceLibQueryCondition& cond)       = 0;
    virtual db::FacePersonQueryResult QueryPersons(const db::FacePersonQueryCondition& cond) = 0;
    virtual std::vector<float> QueryFaceFeature(const std::string& faceId)                   = 0;
};

}  // namespace cosmo::service
