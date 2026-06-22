// PersonRecogDaoServiceImpl — Implementation of IPersonRecogDaoService — delegates to PersonRecogDao.

#include "service/face/impl/PersonRecogDaoServiceImpl.h"

#include "service/detail/ServiceRegistry.h"
#include "service/infra/IDbService.h"

namespace cosmo::service {

PersonRecogDaoServiceImpl::PersonRecogDaoServiceImpl()
    : dao_(*ServiceRegistry::Instance().Get<IDbService>().GetDb()) {}

void PersonRecogDaoServiceImpl::Begin() {
    dao_.Begin();
}

void PersonRecogDaoServiceImpl::Commit() {
    dao_.Commit();
}

void PersonRecogDaoServiceImpl::Rollback() {
    dao_.Rollback();
}

bool PersonRecogDaoServiceImpl::AddPersonLib(const db::LibInfo& data) {
    return dao_.AddPersonLib(data);
}

bool PersonRecogDaoServiceImpl::UpdatePersonLib(const db::LibInfo& data) {
    return dao_.UpdatePersonLib(data);
}

bool PersonRecogDaoServiceImpl::RemovePersonLib(const std::string& personLibId) {
    return dao_.RemovePersonLib(personLibId);
}

bool PersonRecogDaoServiceImpl::ClearPersonLib(const std::string& personLibId) {
    return dao_.ClearPersonLib(personLibId);
}

std::vector<std::string> PersonRecogDaoServiceImpl::GetAllPersonLibs() {
    return dao_.GetAllPersonLibs();
}

bool PersonRecogDaoServiceImpl::AddPerson(const std::string& personId, const std::string& personLibId,
                                          const std::string& personName, const std::vector<float>& feature) {
    return dao_.AddPerson(personId, personLibId, personName, feature);
}

bool PersonRecogDaoServiceImpl::RemovePerson(const std::string& personId) {
    return dao_.RemovePerson(personId);
}

db::PersonRecogLibQueryResult PersonRecogDaoServiceImpl::QueryPersonLib(
    const db::PersonRecogLibQueryCondition& cond) {
    return dao_.QueryPersonLib(cond);
}

db::PersonRecogQueryResult PersonRecogDaoServiceImpl::QueryPersons(
    const db::PersonRecogQueryCondition& cond) {
    return dao_.QueryPersons(cond);
}

}  // namespace cosmo::service
