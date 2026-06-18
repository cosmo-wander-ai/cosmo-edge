/// @file PersonRecogDaoServiceImpl.h
/// @brief Implementation of IPersonRecogDaoService — delegates to PersonRecogDao.
#pragma once

#include "db/PersonRecogDao.h"
#include "service/face/IPersonRecogDaoService.h"

namespace cosmo::service {

/// Concrete implementation that holds a PersonRecogDao internally.
/// Constructed with the database handle from IDbService.
class PersonRecogDaoServiceImpl : public IPersonRecogDaoService {
public:
    PersonRecogDaoServiceImpl();
    ~PersonRecogDaoServiceImpl() override = default;

    // ── Transaction Control ──
    void Begin() override;
    void Commit() override;
    void Rollback() override;

    // ── Person Library CRUD ──
    bool AddPersonLib(const db::LibInfo& data) override;
    bool UpdatePersonLib(const db::LibInfo& data) override;
    bool RemovePersonLib(const std::string& personLibId) override;
    bool ClearPersonLib(const std::string& personLibId) override;
    std::vector<std::string> GetAllPersonLibs() override;

    // ── Person CRUD ──
    bool AddPerson(const std::string& personId, const std::string& personLibId, const std::string& personName,
                   const std::vector<float>& feature) override;
    bool RemovePerson(const std::string& personId) override;

    // ── Queries ──
    db::PersonRecogLibQueryResult QueryPersonLib(const db::PersonRecogLibQueryCondition& cond) override;
    db::PersonRecogQueryResult QueryPersons(const db::PersonRecogQueryCondition& cond) override;

private:
    db::PersonRecogDao dao_;
};

}  // namespace cosmo::service
