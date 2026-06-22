#include "catch_amalgamated.hpp"
/// @file test_transaction_guard.cc
/// @brief TransactionGuard RAII unit tests — validates commit path, rollback on
///        scope exit, and rollback on exception.

#include <stdexcept>

namespace cosmo::test {

// Minimal mock that records Begin/Commit/Rollback calls
struct MockTransactable {
    int begin_count{0};
    int commit_count{0};
    int rollback_count{0};

    void Begin() {
        ++begin_count;
    }
    void Commit() {
        ++commit_count;
    }
    void Rollback() {
        ++rollback_count;
    }
};

}  // namespace cosmo::test

// Include after MockTransactable is defined so template can see it
#include "db/TransactionGuard.h"

namespace cosmo::test {

TEST_CASE("TransactionGuard: normal commit path", "[transaction-guard]") {
    MockTransactable mock;

    {
        db::TransactionGuard<MockTransactable> guard(mock);
        REQUIRE(mock.begin_count == 1);
        guard.Commit();
    }

    REQUIRE(mock.commit_count == 1);
    REQUIRE(mock.rollback_count == 0);
}

TEST_CASE("TransactionGuard: rollback on scope exit without commit", "[transaction-guard]") {
    MockTransactable mock;

    {
        db::TransactionGuard<MockTransactable> guard(mock);
        REQUIRE(mock.begin_count == 1);
        // No Commit() call — guard should rollback
    }

    REQUIRE(mock.commit_count == 0);
    REQUIRE(mock.rollback_count == 1);
}

TEST_CASE("TransactionGuard: rollback on exception", "[transaction-guard]") {
    MockTransactable mock;

    try {
        db::TransactionGuard<MockTransactable> guard(mock);
        throw std::runtime_error("test exception");
    } catch (const std::runtime_error&) {
        // Expected
    }

    REQUIRE(mock.begin_count == 1);
    REQUIRE(mock.commit_count == 0);
    REQUIRE(mock.rollback_count == 1);
}

TEST_CASE("TransactionGuard: commit prevents rollback", "[transaction-guard]") {
    MockTransactable mock;

    {
        db::TransactionGuard<MockTransactable> guard(mock);
        guard.Commit();
        // Leaving scope after commit — should NOT rollback
    }

    REQUIRE(mock.commit_count == 1);
    REQUIRE(mock.rollback_count == 0);
}

}  // namespace cosmo::test
