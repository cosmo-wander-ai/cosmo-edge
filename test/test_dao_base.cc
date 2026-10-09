#include "catch_amalgamated.hpp"
/// @file test_dao_base.cc
/// @brief DaoBase unit tests — validates transaction control, SetLimit,
///        DeleteItems, column management using in-memory SQLite.

#include <SQLiteCpp/SQLiteCpp.h>

#include <chrono>
#include <future>
#include <memory>
#include <stdexcept>

#include "db/DaoBase.h"
#include "db/TransactionGuard.h"

namespace cosmo::test {

// Expose protected methods via a test subclass
class TestableDao : public db::DaoBase {
public:
    using DaoBase::DaoBase;

    using DaoBase::AddColumnToTable;
    using DaoBase::DeleteItems;
    using DaoBase::GetTableAllColumns;
    using DaoBase::IsColumnExist;
    using DaoBase::SetLimit;
};

namespace {

    SQLite::Database MakeInMemoryDb() {
        return SQLite::Database(":memory:",
                                SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE | SQLite::OPEN_FULLMUTEX);
    }

    void VerifyTransactionsAreSerialized(TestableDao& first_dao, TestableDao& second_dao,
                                         SQLite::Database& sqlite_db) {
        using namespace std::chrono_literals;

        std::promise<void> release_first;
        auto release_first_future = release_first.get_future().share();
        std::promise<void> first_started;
        auto first_started_future = first_started.get_future();
        std::promise<void> second_started;
        auto second_started_future = second_started.get_future();

        auto first_result = std::async(std::launch::async, [&]() {
            db::TransactionGuard guard(first_dao);
            sqlite_db.exec("INSERT INTO t_transaction_lock VALUES (1)");
            first_started.set_value();
            release_first_future.wait();
            guard.Commit();
        });

        const auto first_status = first_started_future.wait_for(2s);
        std::future<void> second_result;
        bool second_was_blocked{false};
        if (first_status == std::future_status::ready) {
            second_result      = std::async(std::launch::async, [&]() {
                db::TransactionGuard guard(second_dao);
                second_started.set_value();
                sqlite_db.exec("INSERT INTO t_transaction_lock VALUES (2)");
            });
            second_was_blocked = second_started_future.wait_for(100ms) == std::future_status::timeout;
        }

        release_first.set_value();
        const auto first_completed  = first_result.wait_for(2s) == std::future_status::ready;
        const auto second_completed = second_result.valid() &&
                                      second_started_future.wait_for(2s) == std::future_status::ready &&
                                      second_result.wait_for(2s) == std::future_status::ready;

        REQUIRE(first_status == std::future_status::ready);
        REQUIRE(first_completed);
        REQUIRE_NOTHROW(first_result.get());
        REQUIRE(second_was_blocked);
        REQUIRE(second_completed);
        REQUIRE_NOTHROW(second_result.get());

        SQLite::Statement query(sqlite_db, "SELECT id FROM t_transaction_lock ORDER BY id");
        REQUIRE(query.executeStep());
        REQUIRE(query.getColumn(0).getInt() == 1);
        REQUIRE_FALSE(query.executeStep());
    }

}  // namespace

TEST_CASE("DaoBase: transaction control", "[dao-base]") {
    auto db = MakeInMemoryDb();
    TestableDao dao(db);

    SECTION("Begin/Commit cycle does not throw") {
        REQUIRE_NOTHROW(dao.Begin());
        REQUIRE_NOTHROW(dao.Commit());
    }

    SECTION("Begin/Rollback cycle does not throw") {
        REQUIRE_NOTHROW(dao.Begin());
        REQUIRE_NOTHROW(dao.Rollback());
    }

    SECTION("Double Begin is idempotent (only first takes effect)") {
        REQUIRE_NOTHROW(dao.Begin());
        REQUIRE_NOTHROW(dao.Begin());
        REQUIRE_NOTHROW(dao.Commit());
    }

    SECTION("Commit without Begin is no-op") {
        REQUIRE_NOTHROW(dao.Commit());
    }

    SECTION("Rollback without Begin is no-op") {
        REQUIRE_NOTHROW(dao.Rollback());
    }

    SECTION("Transaction protects data on commit") {
        db.exec("CREATE TABLE t_test (id INTEGER PRIMARY KEY, val TEXT)");

        dao.Begin();
        db.exec("INSERT INTO t_test VALUES (1, 'hello')");
        dao.Commit();

        SQLite::Statement query(db, "SELECT val FROM t_test WHERE id = 1");
        REQUIRE(query.executeStep());
        REQUIRE(query.getColumn(0).getString() == "hello");
    }

    SECTION("Transaction rolls back data on rollback") {
        db.exec("CREATE TABLE t_rollback_test (id INTEGER PRIMARY KEY, val TEXT)");
        // Insert baseline data outside transaction
        db.exec("INSERT INTO t_rollback_test VALUES (1, 'before')");

        dao.Begin();
        db.exec("UPDATE t_rollback_test SET val = 'during' WHERE id = 1");
        dao.Rollback();

        SQLite::Statement query(db, "SELECT val FROM t_rollback_test WHERE id = 1");
        REQUIRE(query.executeStep());
        REQUIRE(query.getColumn(0).getString() == "before");
    }
}

TEST_CASE("DaoBase: transaction owns its SQLite connection for the guard lifetime",
          "[dao-base][transaction-guard][thread]") {
    auto sqlite_db = MakeInMemoryDb();
    sqlite_db.exec("CREATE TABLE t_transaction_lock (id INTEGER PRIMARY KEY)");
    TestableDao first_dao(sqlite_db);
    TestableDao second_dao(sqlite_db);

    SECTION("concurrent guards on the same DAO are serialized") {
        VerifyTransactionsAreSerialized(first_dao, first_dao, sqlite_db);
    }

    SECTION("concurrent guards on different DAOs sharing a connection are serialized") {
        VerifyTransactionsAreSerialized(first_dao, second_dao, sqlite_db);
    }

    SECTION("nested guards on different DAOs are rejected") {
        db::TransactionGuard first_guard(first_dao);
        REQUIRE_THROWS_AS(db::TransactionGuard(second_dao), std::logic_error);
        first_guard.Commit();
    }
}

TEST_CASE("DaoBase: Db() returns reference to database", "[dao-base]") {
    auto db = MakeInMemoryDb();
    TestableDao dao(db);

    REQUIRE(&dao.Db() == &db);

    const TestableDao& const_dao = dao;
    REQUIRE(&const_dao.Db() == &db);
}

TEST_CASE("DaoBase: SetLimit appends LIMIT/OFFSET clause", "[dao-base]") {
    auto db = MakeInMemoryDb();
    TestableDao dao(db);

    SECTION("positive page_num and page_size appends clause") {
        std::string sql = "SELECT * FROM t";
        dao.SetLimit(sql, 1, 10);
        REQUIRE(sql == "SELECT * FROM t LIMIT 10 OFFSET 0");
    }

    SECTION("page_num 2 computes correct offset") {
        std::string sql = "SELECT * FROM t";
        dao.SetLimit(sql, 2, 10);
        REQUIRE(sql == "SELECT * FROM t LIMIT 10 OFFSET 10");
    }

    SECTION("page_num 3, page_size 5") {
        std::string sql = "SELECT * FROM t";
        dao.SetLimit(sql, 3, 5);
        REQUIRE(sql == "SELECT * FROM t LIMIT 5 OFFSET 10");
    }

    SECTION("page_size 0 does not append") {
        std::string sql = "SELECT * FROM t";
        dao.SetLimit(sql, 1, 0);
        REQUIRE(sql == "SELECT * FROM t");
    }

    SECTION("page_num 0 does not append") {
        std::string sql = "SELECT * FROM t";
        dao.SetLimit(sql, 0, 10);
        REQUIRE(sql == "SELECT * FROM t");
    }
}

TEST_CASE("DaoBase: DeleteItems deletes by rec_id list", "[dao-base]") {
    auto db = MakeInMemoryDb();
    db.exec("CREATE TABLE t_del (rec_id TEXT PRIMARY KEY, val TEXT)");
    db.exec("INSERT INTO t_del VALUES ('a', 'v1')");
    db.exec("INSERT INTO t_del VALUES ('b', 'v2')");
    db.exec("INSERT INTO t_del VALUES ('c', 'v3')");

    TestableDao dao(db);

    SECTION("empty list deletes nothing") {
        dao.DeleteItems("t_del", {});

        SQLite::Statement q(db, "SELECT COUNT(*) FROM t_del");
        q.executeStep();
        REQUIRE(q.getColumn(0).getInt() == 3);
    }

    SECTION("delete subset of records") {
        dao.DeleteItems("t_del", {"a", "c"});

        SQLite::Statement q(db, "SELECT rec_id FROM t_del");
        REQUIRE(q.executeStep());
        REQUIRE(q.getColumn(0).getString() == "b");
        REQUIRE_FALSE(q.executeStep());
    }

    SECTION("delete all records") {
        dao.DeleteItems("t_del", {"a", "b", "c"});

        SQLite::Statement q(db, "SELECT COUNT(*) FROM t_del");
        q.executeStep();
        REQUIRE(q.getColumn(0).getInt() == 0);
    }
}

TEST_CASE("DaoBase: column management helpers", "[dao-base]") {
    auto db = MakeInMemoryDb();
    db.exec("CREATE TABLE t_col (id INTEGER PRIMARY KEY, name TEXT)");
    TestableDao dao(db);

    SECTION("GetTableAllColumns returns existing columns") {
        auto cols = dao.GetTableAllColumns("t_col");
        REQUIRE(cols.size() == 2);
        REQUIRE(cols[0] == "id");
        REQUIRE(cols[1] == "name");
    }

    SECTION("IsColumnExist finds existing column") {
        auto cols = dao.GetTableAllColumns("t_col");
        REQUIRE(dao.IsColumnExist(cols, "name"));
        REQUIRE_FALSE(dao.IsColumnExist(cols, "nonexistent"));
    }

    SECTION("AddColumnToTable adds TEXT column") {
        dao.AddColumnToTable("t_col", "description", db::DaoBase::ColumnType::TEXT);
        auto cols = dao.GetTableAllColumns("t_col");
        REQUIRE(dao.IsColumnExist(cols, "description"));
    }

    SECTION("AddColumnToTable adds INTEGER column") {
        dao.AddColumnToTable("t_col", "count", db::DaoBase::ColumnType::INTEGER);
        auto cols = dao.GetTableAllColumns("t_col");
        REQUIRE(dao.IsColumnExist(cols, "count"));
    }

    SECTION("AddColumnToTable adds REAL column") {
        dao.AddColumnToTable("t_col", "score", db::DaoBase::ColumnType::FLOAT);
        auto cols = dao.GetTableAllColumns("t_col");
        REQUIRE(dao.IsColumnExist(cols, "score"));
    }

    SECTION("AddColumnToTable adds BLOB column") {
        dao.AddColumnToTable("t_col", "data", db::DaoBase::ColumnType::BLOB);
        auto cols = dao.GetTableAllColumns("t_col");
        REQUIRE(dao.IsColumnExist(cols, "data"));
    }

    SECTION("AddColumnToTable adds INT64 column (maps to INTEGER)") {
        dao.AddColumnToTable("t_col", "big_num", db::DaoBase::ColumnType::INT64);
        auto cols = dao.GetTableAllColumns("t_col");
        REQUIRE(dao.IsColumnExist(cols, "big_num"));
    }

    SECTION("AddColumnToTable adds BOOLEAN column (maps to INTEGER)") {
        dao.AddColumnToTable("t_col", "active", db::DaoBase::ColumnType::BOOLEAN);
        auto cols = dao.GetTableAllColumns("t_col");
        REQUIRE(dao.IsColumnExist(cols, "active"));
    }
}

}  // namespace cosmo::test
