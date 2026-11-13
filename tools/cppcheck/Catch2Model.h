// Analysis-only assertion models. Keep Catch2's test/section control flow, but
// avoid its expression decomposer and string-literal machinery in cppcheck.
#pragma once

#include "catch_amalgamated.hpp"

#undef REQUIRE
#define REQUIRE(...)                                                                                         \
    do {                                                                                                     \
        if (!(__VA_ARGS__))                                                                                  \
            throw false;                                                                                     \
    } while (false)
#undef REQUIRE_FALSE
#define REQUIRE_FALSE(...)                                                                                   \
    do {                                                                                                     \
        if ((__VA_ARGS__))                                                                                   \
            throw false;                                                                                     \
    } while (false)

// Nonfatal checks evaluate their expression without constraining later paths.
#undef CHECK
#define CHECK(...) (void)(__VA_ARGS__)
#undef CHECK_FALSE
#define CHECK_FALSE(...) (void)(__VA_ARGS__)
#undef REQUIRE_NOTHROW
#define REQUIRE_NOTHROW(...) (void)(__VA_ARGS__)
#undef CHECK_NOTHROW
#define CHECK_NOTHROW(...) (void)(__VA_ARGS__)

// Exception assertions must still expose the tested expression to analysis.
#undef REQUIRE_THROWS
#define REQUIRE_THROWS(...)                                                                                  \
    do {                                                                                                     \
        try {                                                                                                \
            (void)(__VA_ARGS__);                                                                             \
        } catch (...) {                                                                                      \
        }                                                                                                    \
    } while (false)
#undef CHECK_THROWS
#define CHECK_THROWS(...) REQUIRE_THROWS(__VA_ARGS__)
#undef REQUIRE_THROWS_WITH
#define REQUIRE_THROWS_WITH(expr, matcher)                                                                   \
    do {                                                                                                     \
        REQUIRE_THROWS(expr);                                                                                \
        (void)(matcher);                                                                                     \
    } while (false)
#undef CHECK_THROWS_WITH
#define CHECK_THROWS_WITH(expr, matcher) REQUIRE_THROWS_WITH(expr, matcher)
#undef REQUIRE_THROWS_AS
#define REQUIRE_THROWS_AS(expr, type)                                                                        \
    do {                                                                                                     \
        try {                                                                                                \
            (void)(expr);                                                                                    \
        } catch (const type&) {                                                                              \
        }                                                                                                    \
    } while (false)
#undef CHECK_THROWS_AS
#define CHECK_THROWS_AS(expr, type) REQUIRE_THROWS_AS(expr, type)
