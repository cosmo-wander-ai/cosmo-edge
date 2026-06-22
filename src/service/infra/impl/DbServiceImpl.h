#pragma once

#include <memory>

#include "service/infra/IDbService.h"

namespace SQLite {
class Database;
}

namespace cosmo::service {

class DbServiceImpl : public IDbService {
public:
    DbServiceImpl();
    ~DbServiceImpl() override;

    void Init() override;

    std::shared_ptr<SQLite::Database> GetDb() override;

private:
    std::shared_ptr<SQLite::Database> sql_db_{nullptr};
};

}  // namespace cosmo::service
