// ArticlesReidDaoServiceImpl — Implementation of IArticlesReidDaoService — delegates to ArticlesReidDao.

#include "service/face/impl/ArticlesReidDaoServiceImpl.h"

#include "service/detail/ServiceRegistry.h"
#include "service/infra/IDbService.h"

namespace cosmo::service {

ArticlesReidDaoServiceImpl::ArticlesReidDaoServiceImpl()
    : dao_(*ServiceRegistry::Instance().Get<IDbService>().GetDb()) {}

void ArticlesReidDaoServiceImpl::Begin() {
    dao_.Begin();
}

void ArticlesReidDaoServiceImpl::Commit() {
    dao_.Commit();
}

void ArticlesReidDaoServiceImpl::Rollback() {
    dao_.Rollback();
}

bool ArticlesReidDaoServiceImpl::AddArticlesReidLib(const db::LibInfo& data) {
    return dao_.AddArticlesReidLib(data);
}

bool ArticlesReidDaoServiceImpl::UpdateArticlesReidLib(const db::LibInfo& data) {
    return dao_.UpdateArticlesReidLib(data);
}

bool ArticlesReidDaoServiceImpl::RemoveArticlesReidLib(const std::string& articlesReidLibId) {
    return dao_.RemoveArticlesReidLib(articlesReidLibId);
}

bool ArticlesReidDaoServiceImpl::ClearArticlesReidLib(const std::string& articlesReidLibId) {
    return dao_.ClearArticlesReidLib(articlesReidLibId);
}

std::vector<std::string> ArticlesReidDaoServiceImpl::GetAllArticlesReidLibs() {
    return dao_.GetAllArticlesReidLibs();
}

bool ArticlesReidDaoServiceImpl::AddArticlesReid(const std::string& articlesReidId,
                                                 const std::string& articlesReidLibId,
                                                 const std::string& articlesReidName,
                                                 const std::vector<float>& feature) {
    return dao_.AddArticlesReid(articlesReidId, articlesReidLibId, articlesReidName, feature);
}

bool ArticlesReidDaoServiceImpl::RemoveArticlesReid(const std::string& articlesReidId) {
    return dao_.RemoveArticlesReid(articlesReidId);
}

db::ThingsLibQueryResult ArticlesReidDaoServiceImpl::QueryThingsLib(const db::ThingsLibQueryCondition& cond) {
    return dao_.QueryThingsLib(cond);
}

db::ThingsQueryResult ArticlesReidDaoServiceImpl::QueryThings(const db::ThingsQueryCondition& cond) {
    return dao_.QueryThings(cond);
}

}  // namespace cosmo::service
