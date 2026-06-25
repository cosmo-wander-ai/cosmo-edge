#include "catch_amalgamated.hpp"
/*
 * test_date_time_format.cc — DateTimeFormat unit tests
 *
 * Tests HMSTime, YMDDate, DateTime: construction, parsing, formatting,
 * arithmetic, comparison, and edge cases.
 */
#include "util/DateTimeFormat.h"

using namespace cosmo::util;

// ─── HMSTime ─────────────────────────────────────────────────────────────────

TEST_CASE("HMSTime: default constructs to 00:00:00", "[datetime]") {
    HMSTime t;
    REQUIRE(t.Hour() == 0);
    REQUIRE(t.Minute() == 0);
    REQUIRE(t.Second() == 0);
    REQUIRE(t.ToInt() == 0);
}

TEST_CASE("HMSTime: construct from components", "[datetime]") {
    SECTION("Normal values") {
        HMSTime t(12, 30, 45);
        REQUIRE(t.Hour() == 12);
        REQUIRE(t.Minute() == 30);
        REQUIRE(t.Second() == 45);
    }

    SECTION("Midnight boundary") {
        HMSTime t(23, 59, 59);
        REQUIRE(t.Hour() == 23);
        REQUIRE(t.Minute() == 59);
        REQUIRE(t.Second() == 59);
    }

    SECTION("Zero second default") {
        HMSTime t(8, 15);
        REQUIRE(t.Second() == 0);
    }
}

TEST_CASE("HMSTime: construct from string", "[datetime]") {
    SECTION("HH:MM:SS format") {
        HMSTime t("14:30:15");
        REQUIRE(t.Hour() == 14);
        REQUIRE(t.Minute() == 30);
        REQUIRE(t.Second() == 15);
    }

    SECTION("HH:MM format") {
        HMSTime t("09:05");
        REQUIRE(t.Hour() == 9);
        REQUIRE(t.Minute() == 5);
        REQUIRE(t.Second() == 0);
    }
}

TEST_CASE("HMSTime: construct from seconds", "[datetime]") {
    HMSTime t(static_cast<uint32_t>(3661));  // 1h 1m 1s
    REQUIRE(t.Hour() == 1);
    REQUIRE(t.Minute() == 1);
    REQUIRE(t.Second() == 1);
}

TEST_CASE("HMSTime: ToInt round-trip", "[datetime]") {
    HMSTime t(10, 20, 30);
    uint32_t secs = t.ToInt();
    REQUIRE(secs == 10 * 3600 + 20 * 60 + 30);

    HMSTime t2(secs);
    REQUIRE(t2.Hour() == 10);
    REQUIRE(t2.Minute() == 20);
    REQUIRE(t2.Second() == 30);
}

TEST_CASE("HMSTime: formatting", "[datetime]") {
    HMSTime t(8, 5, 3);

    SECTION("ToHMS default separator") {
        REQUIRE(t.ToHMS() == "08:05:03");
    }

    SECTION("ToHMS custom separator") {
        REQUIRE(t.ToHMS("-") == "08-05-03");
    }

    SECTION("ToHM") {
        REQUIRE(t.ToHM() == "08:05");
    }
}

TEST_CASE("HMSTime: comparison operators", "[datetime]") {
    HMSTime a(10, 0, 0);
    HMSTime b(10, 0, 0);
    HMSTime c(11, 0, 0);

    REQUIRE(a == b);
    REQUIRE(a < c);
    REQUIRE(a <= b);
    REQUIRE(a <= c);
}

// ─── YMDDate ─────────────────────────────────────────────────────────────────

TEST_CASE("YMDDate: default constructs to epoch", "[datetime]") {
    YMDDate d;
    REQUIRE(d.Year() == 1970);
    REQUIRE(d.Month() == 1);
    REQUIRE(d.Day() == 1);
}

TEST_CASE("YMDDate: construct from components", "[datetime]") {
    YMDDate d(2025, 6, 25);
    REQUIRE(d.Year() == 2025);
    REQUIRE(d.Month() == 6);
    REQUIRE(d.Day() == 25);
}

TEST_CASE("YMDDate: construct from string", "[datetime]") {
    SECTION("YYYY-MM-DD") {
        YMDDate d("2025-12-31");
        REQUIRE(d.Year() == 2025);
        REQUIRE(d.Month() == 12);
        REQUIRE(d.Day() == 31);
    }

    SECTION("YYYY-MM") {
        YMDDate d("2025-06");
        REQUIRE(d.Year() == 2025);
        REQUIRE(d.Month() == 6);
        REQUIRE(d.Day() == 1);
    }

    SECTION("YYYY-MM-DD HH:MM:SS") {
        YMDDate d("2025-06-25 14:30:00");
        REQUIRE(d.Year() == 2025);
        REQUIRE(d.Month() == 6);
        REQUIRE(d.Day() == 25);
    }
}

TEST_CASE("YMDDate: formatting", "[datetime]") {
    YMDDate d(2025, 1, 5);

    SECTION("ToYMD default separator") {
        REQUIRE(d.ToYMD() == "2025-01-05");
    }

    SECTION("ToYMD custom separator") {
        REQUIRE(d.ToYMD("/") == "2025/01/05");
    }
}

TEST_CASE("YMDDate: comparison operators", "[datetime]") {
    YMDDate a(2025, 1, 1);
    YMDDate b(2025, 1, 1);
    YMDDate c(2025, 1, 2);

    REQUIRE(a == b);
    REQUIRE(a < c);
    REQUIRE(a <= b);
    REQUIRE(a <= c);
}

TEST_CASE("YMDDate: AddDay", "[datetime]") {
    SECTION("Normal add") {
        auto d = AddDay(YMDDate(2025, 1, 30), 2);
        REQUIRE(d.Month() == 2);
        REQUIRE(d.Day() == 1);
    }

    SECTION("Add across year boundary") {
        auto d = AddDay(YMDDate(2025, 12, 31), 1);
        REQUIRE(d.Year() == 2026);
        REQUIRE(d.Month() == 1);
        REQUIRE(d.Day() == 1);
    }

    SECTION("Negative add") {
        auto d = AddDay(YMDDate(2025, 3, 1), -1);
        REQUIRE(d.Month() == 2);
        REQUIRE(d.Day() == 28);
    }
}

TEST_CASE("YMDDate: AddMonth", "[datetime]") {
    SECTION("Normal add") {
        auto d = AddMonth(YMDDate(2025, 1, 15), 3);
        REQUIRE(d.Month() == 4);
        REQUIRE(d.Day() == 15);
    }

    SECTION("Cross year boundary") {
        auto d = AddMonth(YMDDate(2025, 11, 15), 3);
        REQUIRE(d.Year() == 2026);
        REQUIRE(d.Month() == 2);
    }
}

TEST_CASE("YMDDate: WeekDay", "[datetime]") {
    // 2025-06-25 is Wednesday (3)
    YMDDate d(2025, 6, 25);
    REQUIRE(d.WeekDay() == 3);
}

TEST_CASE("YMDDate: GetYearDay", "[datetime]") {
    // Jan 1 = day 0
    REQUIRE(GetYearDay(YMDDate(2025, 1, 1)) == 0);
    // Feb 1 = day 31
    REQUIRE(GetYearDay(YMDDate(2025, 2, 1)) == 31);
}

// ─── DateTime ────────────────────────────────────────────────────────────────

TEST_CASE("DateTime: construct from string", "[datetime]") {
    DateTime dt("2025-06-25 14:30:45");
    REQUIRE(dt.Date().Year() == 2025);
    REQUIRE(dt.Date().Month() == 6);
    REQUIRE(dt.Date().Day() == 25);
    REQUIRE(dt.Time().Hour() == 14);
    REQUIRE(dt.Time().Minute() == 30);
    REQUIRE(dt.Time().Second() == 45);
}

TEST_CASE("DateTime: ToTimeStamp and back", "[datetime]") {
    DateTime dt("2025-06-25 12:00:00");
    auto ts = dt.ToTimeStamp();
    REQUIRE(ts > 0);

    DateTime dt2(ts);
    REQUIRE(dt2.Date().Year() == 2025);
    REQUIRE(dt2.Date().Month() == 6);
    REQUIRE(dt2.Date().Day() == 25);
    REQUIRE(dt2.Time().Hour() == 12);
}

TEST_CASE("DateTime: ToYMDHMSInt and back", "[datetime]") {
    DateTime dt("2025-06-25 14:30:45");
    auto intVal = dt.ToYMDHMSInt();
    REQUIRE(intVal == 20250625143045ULL);

    auto dt2 = DateTimeFromYMDHMSInt(intVal);
    REQUIRE(dt2.Date().Year() == 2025);
    REQUIRE(dt2.Time().Hour() == 14);
    REQUIRE(dt2.Time().Minute() == 30);
    REQUIRE(dt2.Time().Second() == 45);
}

TEST_CASE("DateTime: comparison operators", "[datetime]") {
    DateTime a("2025-06-25 10:00:00");
    DateTime b("2025-06-25 10:00:00");
    DateTime c("2025-06-25 11:00:00");

    REQUIRE(a == b);
    REQUIRE(a < c);
    REQUIRE(a <= b);
}

TEST_CASE("DateTime: GetCurrentDateTime returns valid date", "[datetime]") {
    auto now = GetCurrentDateTime();
    REQUIRE(now.Date().Year() >= 2025);
}
