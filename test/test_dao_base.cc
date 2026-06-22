#include "catch_amalgamated.hpp"
/// @file test_dao_base.cc
/// @brief DaoBase unit tests — validates transaction control, SetCondition,
///        SetLimit, DeleteItems, column management using in-memory SQLite.

#include <SQLiteCpp/SQLiteCpp.h>

#include <memory>

#include "db/DaoBase.h"

namespace cosmo::test {

// Expose protected methods via a test subclass
class TestableDao : public db::DaoBase {
public:
    using DaoBase::DaoBase;

    using DaoBase::AddColumnToTable;
    using DaoBase::DeleteItems;
    using DaoBase::GetTableAllColumns;
    using DaoBase::IsColumnExist;
    using DaoBase::SetCondition;
    using DaoBase::SetLimit;
};

namespace {

    SQLite::Database MakeInMemoryDb() {
        return SQLite::Database(":memory:", SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
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

TEST_CASE("DaoBase: Db() returns reference to database", "[dao-base]") {
    auto db = MakeInMemoryDb();
    TestableDao dao(db);

    REQUIRE(&dao.Db() == &db);

    const TestableDao& const_dao = dao;
    REQUIRE(&const_dao.Db() == &db);
}

TEST_CASE("DaoBase: QueryRows counts rows correctly", "[dao-base]") {
    auto db = MakeInMemoryDb();
    db.exec("CREATE TABLE t_count (id INTEGER PRIMARY KEY, val TEXT)");
    TestableDao dao(db);

    SECTION("empty table returns 0") {
        auto count = dao.QueryRows("SELECT id FROM t_count");
        REQUIRE(count == 0);
    }

    SECTION("non-empty table returns correct count") {
        db.exec("INSERT INTO t_count VALUES (1, 'a')");
        db.exec("INSERT INTO t_count VALUES (2, 'b')");
        db.exec("INSERT INTO t_count VALUES (3, 'c')");

        auto count = dao.QueryRows("SELECT id FROM t_count");
        REQUIRE(count == 3);
    }
}

TEST_CASE("DaoBase: SetCondition builds WHERE clause from vector", "[dao-base]") {
    auto db = MakeInMemoryDb();
    TestableDao dao(db);

    SECTION("empty vector does not append anything") {
        std::string sql = "SELECT * FROM t";
        dao.SetCondition(sql, std::vector<std::string>{});
        REQUIRE(sql == "SELECT * FROM t");
    }

    SECTION("single condition") {
        std::string sql = "SELECT * FROM t";
        dao.SetCondition(sql, std::vector<std::string>{"id = 1"});
        REQUIRE(sql == "SELECT * FROM t WHERE id = 1");
    }

    SECTION("multiple conditions joined by AND") {
        std::string sql = "SELECT * FROM t";
        dao.SetCondition(sql, std::vector<std::string>{"id = 1", "name = 'test'"});
        REQUIRE(sql == "SELECT * FROM t WHERE id = 1 AND name = 'test'");
    }

    SECTION("const ref overload works the same") {
        std::string sql                      = "SELECT * FROM t";
        const std::vector<std::string> conds = {"a = 1", "b = 2"};
        dao.SetCondition(sql, conds);
        REQUIRE(sql == "SELECT * FROM t WHERE a = 1 AND b = 2");
    }
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
