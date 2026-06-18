/// @file IDbService.h
/// @brief Database service interface — manages SQLite database initialization
///        and provides shared access to the database handle.
#pragma once

#include <memory>

namespace SQLite {
class Database;
}

namespace cosmo::service {

/// Manages the application's SQLite database lifecycle.
///
/// Provides initialization (schema migration, WAL mode setup) and shared
/// access to the database handle used across DAO classes.
class IDbService {
public:
    virtual ~IDbService() = default;

    /// Initialize the database: create tables, run migrations, set WAL mode.
    virtual void Init() = 0;

    /// Get the shared SQLite database handle.
    /// @return Shared pointer to the SQLite database instance.
    virtual std::shared_ptr<SQLite::Database> GetDb() = 0;
};

}  // namespace cosmo::service
