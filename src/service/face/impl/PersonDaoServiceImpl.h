/// @file PersonDaoServiceImpl.h
/// @brief Implementation of IPersonDaoService — delegates to PersonDao.
#pragma once

#include "db/PersonDao.h"
#include "service/face/IPersonDaoService.h"

namespace cosmo::service {

/// Concrete implementation that holds a PersonDao internally.
/// Constructed with the database handle from IDbService.
class PersonDaoServiceImpl : public IPersonDaoService {
public:
    PersonDaoServiceImpl();
    ~PersonDaoServiceImpl() override = default;

    // ── Transaction Control ──
    void Begin() override;
    void Commit() override;
    void Rollback() override;

    // ── Face Library CRUD ──
    bool AddFaceLib(const db::LibInfo& data) override;
    bool UpdateFaceLib(const db::LibInfo& data) override;
    bool RemoveFaceLib(const std::string& faceLibId) override;
    bool ClearFaceLib(const std::string& faceLibId) override;

    // ── Person CRUD ──
    bool AddPerson(const db::PersonCondition& person,
                   const std::vector<std::pair<std::string, std::vector<float>>>& face) override;
    bool AddPerson(db::FaceRegRecordUnit& faceReg) override;
    bool UpdatePerson(const db::PersonCondition& person,
                      const std::vector<std::pair<std::string, std::vector<float>>>& face) override;
    bool RemovePerson(const std::string& personId) override;

    // ── Queries ──
    db::FaceLibQueryResult QueryFaceLib(const db::FaceLibQueryCondition& cond) override;
    db::FacePersonQueryResult QueryPersons(const db::FacePersonQueryCondition& cond) override;
    std::vector<float> QueryFaceFeature(const std::string& faceId) override;

private:
    db::PersonDao dao_;
};

}  // namespace cosmo::service
