/// @file IArticlesReidDaoService.h
/// @brief Service interface that encapsulates ArticlesReidDao database operations.
///        Eliminates direct SQLite::Database exposure to api/ layers.
#pragma once

#include <string>
#include <vector>

#include "db/DbTypes.h"

namespace cosmo::service {

/// Encapsulates ArticlesReidDao operations behind a service interface.
///
/// Callers in api/ layers use this interface instead of directly
/// constructing ArticlesReidDao with a raw SQLite::Database handle.
/// The implementation internally manages the database reference.
class IArticlesReidDaoService {
public:
    virtual ~IArticlesReidDaoService() = default;

    // ── Transaction Control ──

    virtual void Begin()    = 0;
    virtual void Commit()   = 0;
    virtual void Rollback() = 0;

    // ── Things Library CRUD ──

    virtual bool AddArticlesReidLib(const db::LibInfo& data)                 = 0;
    virtual bool UpdateArticlesReidLib(const db::LibInfo& data)              = 0;
    virtual bool RemoveArticlesReidLib(const std::string& articlesReidLibId) = 0;
    virtual bool ClearArticlesReidLib(const std::string& articlesReidLibId)  = 0;
    virtual std::vector<std::string> GetAllArticlesReidLibs()                = 0;

    // ── Things CRUD ──

    virtual bool AddArticlesReid(const std::string& articlesReidId, const std::string& articlesReidLibId,
                                 const std::string& articlesReidName, const std::vector<float>& feature) = 0;
    virtual bool RemoveArticlesReid(const std::string& articlesReidId)                                   = 0;

    // ── Queries ──

    virtual db::ThingsLibQueryResult QueryThingsLib(const db::ThingsLibQueryCondition& cond) = 0;
    virtual db::ThingsQueryResult QueryThings(const db::ThingsQueryCondition& cond)          = 0;
};

}  // namespace cosmo::service
