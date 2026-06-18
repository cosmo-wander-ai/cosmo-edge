/// @file ArticlesReidDaoServiceImpl.h
/// @brief Implementation of IArticlesReidDaoService — delegates to ArticlesReidDao.
#pragma once

#include "db/ArticlesReidDao.h"
#include "service/face/IArticlesReidDaoService.h"

namespace cosmo::service {

/// Concrete implementation that holds an ArticlesReidDao internally.
/// Constructed with the database handle from IDbService.
class ArticlesReidDaoServiceImpl : public IArticlesReidDaoService {
public:
    ArticlesReidDaoServiceImpl();
    ~ArticlesReidDaoServiceImpl() override = default;

    // ── Transaction Control ──
    void Begin() override;
    void Commit() override;
    void Rollback() override;

    // ── Things Library CRUD ──
    bool AddArticlesReidLib(const db::LibInfo& data) override;
    bool UpdateArticlesReidLib(const db::LibInfo& data) override;
    bool RemoveArticlesReidLib(const std::string& articlesReidLibId) override;
    bool ClearArticlesReidLib(const std::string& articlesReidLibId) override;
    std::vector<std::string> GetAllArticlesReidLibs() override;

    // ── Things CRUD ──
    bool AddArticlesReid(const std::string& articlesReidId, const std::string& articlesReidLibId,
                         const std::string& articlesReidName, const std::vector<float>& feature) override;
    bool RemoveArticlesReid(const std::string& articlesReidId) override;

    // ── Queries ──
    db::ThingsLibQueryResult QueryThingsLib(const db::ThingsLibQueryCondition& cond) override;
    db::ThingsQueryResult QueryThings(const db::ThingsQueryCondition& cond) override;

private:
    db::ArticlesReidDao dao_;
};

}  // namespace cosmo::service
