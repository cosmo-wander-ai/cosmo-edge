// PersonDaoServiceImpl — Implementation of IPersonDaoService — delegates to PersonDao.

#include "service/face/impl/PersonDaoServiceImpl.h"

#include "service/detail/ServiceRegistry.h"
#include "service/infra/IDbService.h"

namespace cosmo::service {

PersonDaoServiceImpl::PersonDaoServiceImpl() : dao_(*ServiceRegistry::Instance().Get<IDbService>().GetDb()) {}

void PersonDaoServiceImpl::Begin() {
    dao_.Begin();
}

void PersonDaoServiceImpl::Commit() {
    dao_.Commit();
}

void PersonDaoServiceImpl::Rollback() {
    dao_.Rollback();
}

bool PersonDaoServiceImpl::AddFaceLib(const db::LibInfo& data) {
    return dao_.AddFaceLib(data);
}

bool PersonDaoServiceImpl::UpdateFaceLib(const db::LibInfo& data) {
    return dao_.UpdateFaceLib(data);
}

bool PersonDaoServiceImpl::RemoveFaceLib(const std::string& faceLibId) {
    return dao_.RemoveFaceLib(faceLibId);
}

bool PersonDaoServiceImpl::ClearFaceLib(const std::string& faceLibId) {
    return dao_.ClearFaceLib(faceLibId);
}

bool PersonDaoServiceImpl::AddPerson(const db::PersonCondition& person,
                                     const std::vector<std::pair<std::string, std::vector<float>>>& face) {
    return dao_.AddPerson(person, face);
}

bool PersonDaoServiceImpl::AddPerson(db::FaceRegRecordUnit& faceReg) {
    return dao_.AddPerson(faceReg);
}

bool PersonDaoServiceImpl::UpdatePerson(const db::PersonCondition& person,
                                        const std::vector<std::pair<std::string, std::vector<float>>>& face) {
    return dao_.UpdatePerson(person, face);
}

bool PersonDaoServiceImpl::RemovePerson(const std::string& personId) {
    return dao_.RemovePerson(personId);
}

db::FaceLibQueryResult PersonDaoServiceImpl::QueryFaceLib(const db::FaceLibQueryCondition& cond) {
    return dao_.QueryFaceLib(cond);
}

db::FacePersonQueryResult PersonDaoServiceImpl::QueryPersons(const db::FacePersonQueryCondition& cond) {
    return dao_.QueryPersons(cond);
}

std::vector<float> PersonDaoServiceImpl::QueryFaceFeature(const std::string& faceId) {
    return dao_.QueryFaceFeature(faceId);
}

}  // namespace cosmo::service
