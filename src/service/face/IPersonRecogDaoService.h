/// @file IPersonRecogDaoService.h
/// @brief Service interface that encapsulates PersonRecogDao database operations.
///        Eliminates direct SQLite::Database exposure to api/ and service/ layers.
#pragma once

#include <string>
#include <vector>

#include "db/DbTypes.h"

namespace cosmo::service {

/// Encapsulates PersonRecogDao operations behind a service interface.
///
/// Callers in api/ and service/ layers use this interface instead of
/// directly constructing PersonRecogDao with a raw SQLite::Database handle.
/// The implementation internally manages the database reference.
class IPersonRecogDaoService {
public:
    virtual ~IPersonRecogDaoService() = default;

    // ── Transaction Control ──

    virtual void Begin()    = 0;
    virtual void Commit()   = 0;
    virtual void Rollback() = 0;

    // ── Person Library CRUD ──

    virtual bool AddPersonLib(const db::LibInfo& data)           = 0;
    virtual bool UpdatePersonLib(const db::LibInfo& data)        = 0;
    virtual bool RemovePersonLib(const std::string& personLibId) = 0;
    virtual bool ClearPersonLib(const std::string& personLibId)  = 0;
    virtual std::vector<std::string> GetAllPersonLibs()          = 0;

    // ── Person CRUD ──

    virtual bool AddPerson(const std::string& personId, const std::string& personLibId,
                           const std::string& personName, const std::vector<float>& feature) = 0;
    virtual bool RemovePerson(const std::string& personId)                                   = 0;

    // ── Queries ──

    virtual db::PersonRecogLibQueryResult QueryPersonLib(const db::PersonRecogLibQueryCondition& cond) = 0;
    virtual db::PersonRecogQueryResult QueryPersons(const db::PersonRecogQueryCondition& cond)         = 0;
};

}  // namespace cosmo::service
