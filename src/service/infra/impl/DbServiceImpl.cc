// DbServiceImpl — Db Service Impl implementation.

#include "service/infra/impl/DbServiceImpl.h"

#include <SQLiteCpp/SQLiteCpp.h>

#include "db/ArticlesReidDao.h"
#include "db/PersonDao.h"
#include "db/PersonRecogDao.h"
#include "service/detail/ServiceRegistry.h"
#include "util/Log.h"
#include "util/PathUtil.h"

namespace cosmo::service {

DbServiceImpl::DbServiceImpl() {
    auto dbFileName = cosmo::path::GetDbPath() + "/ied.db";

    sql_db_ = std::make_shared<SQLite::Database>(dbFileName, SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);

    sql_db_->setBusyTimeout(5000);
}

DbServiceImpl::~DbServiceImpl() {
    LOG_INFO("{}", "DbServiceImpl Delete");
}

void DbServiceImpl::Init() {
    cosmo::db::PersonDao(*sql_db_).CreateTable();
    cosmo::db::PersonRecogDao(*sql_db_).CreateTable();
    cosmo::db::ArticlesReidDao(*sql_db_).CreateTable();
}

std::shared_ptr<SQLite::Database> DbServiceImpl::GetDb() {
    return sql_db_;
}

}  // namespace cosmo::service
