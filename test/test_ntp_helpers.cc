#include "catch_amalgamated.hpp"
// Unit tests for NTP protocol helper functions (NtpHelpers.h).
// These are pure functions with no I/O dependencies — ideal for unit testing.

// NtpHelpers.h is designed for internal linkage (anonymous namespace).
// We include it here directly to test the helper functions.
#include "service/system/impl/NtpHelpers.h"

using namespace cosmo::service;

// ---------------------------------------------------------------------------
// Timestamp conversion tests
// ---------------------------------------------------------------------------

TEST_CASE("NtpHelpers: TimevalToTimestamp and reverse conversion", "[ntp][helpers]") {
    SECTION("Round-trip conversion preserves values") {
        NtpTime ts;
        long sec  = 1700000000;  // approx 2023-11-14
        long usec = 500000;      // 0.5 seconds

        TimevalToTimestamp(ts, sec, usec);

        // Verify NTP epoch offset is applied
        REQUIRE(ts.seconds == static_cast<uint32_t>(sec + kJan1970));
        REQUIRE(ts.fraction > 0);

        // Convert back
        long out_sec = 0, out_usec = 0;
        TimestampToTimeval(ts, out_sec, out_usec);

        REQUIRE(out_sec == sec);
        // Allow small rounding error in microseconds due to floating-point
        REQUIRE(std::abs(out_usec - usec) < 100);
    }

    SECTION("Zero timeval produces kJan1970 NTP timestamp") {
        NtpTime ts;
        TimevalToTimestamp(ts, 0, 0);
        REQUIRE(ts.seconds == kJan1970);
        REQUIRE(ts.fraction == 0);
    }

    SECTION("TimestampToTimeval handles pre-1970 NTP timestamp") {
        NtpTime ts;
        ts.seconds  = kJan1970 - 1;  // Before Unix epoch
        ts.fraction = 0;

        long sec = 0, usec = 0;
        TimestampToTimeval(ts, sec, usec);

        // Should return zeros for pre-epoch timestamps
        REQUIRE(sec == 0);
        REQUIRE(usec == 0);
    }
}

TEST_CASE("NtpHelpers: TimevalToNs100 converts correctly", "[ntp][helpers]") {
    SECTION("1 second = 10,000,000 100ns units") {
        auto result = TimevalToNs100(1, 0);
        REQUIRE(result == 10000000LL);
    }

    SECTION("1 microsecond = 10 100ns units") {
        auto result = TimevalToNs100(0, 1);
        REQUIRE(result == 10LL);
    }

    SECTION("Combined seconds and microseconds") {
        auto result = TimevalToNs100(5, 500000);
        REQUIRE(result == 5 * 10000000LL + 500000 * 10LL);
    }

    SECTION("Zero values produce zero") {
        auto result = TimevalToNs100(0, 0);
        REQUIRE(result == 0);
    }
}

TEST_CASE("NtpHelpers: TimestampToNs100 delegates correctly", "[ntp][helpers]") {
    SECTION("Known NTP timestamp converts to expected 100ns value") {
        NtpTime ts;
        long test_sec  = 1700000000;
        long test_usec = 0;
        TimevalToTimestamp(ts, test_sec, test_usec);

        auto ns100    = TimestampToNs100(ts);
        auto expected = TimevalToNs100(test_sec, test_usec);

        // Allow small rounding tolerance
        REQUIRE(std::abs(ns100 - expected) < 100);
    }
}

// ---------------------------------------------------------------------------
// NTP packet initialization tests
// ---------------------------------------------------------------------------

TEST_CASE("NtpHelpers: InitPacket sets correct NTP client fields", "[ntp][helpers]") {
    NtpPacket pkt;
    InitPacket(pkt);

    SECTION("LI/VN/Mode byte is correctly formatted") {
        // LI=0, VN=3, Mode=3 -> (0<<6)|(3<<3)|3 = 0x1B = 27
        REQUIRE(static_cast<uint8_t>(pkt.li_vn_mode) == 0x1B);
    }

    SECTION("Poll interval is set to 4") {
        REQUIRE(pkt.poll == 4);
    }

    SECTION("Root delay is 1 << 16") {
        REQUIRE(pkt.root_delay == (1 << 16));
    }

    SECTION("Root dispersion is 1 << 16") {
        REQUIRE(pkt.root_dispersion == static_cast<uint32_t>(1 << 16));
    }

    SECTION("All timestamp fields are zeroed") {
        REQUIRE(pkt.reference_ts.seconds == 0);
        REQUIRE(pkt.reference_ts.fraction == 0);
        REQUIRE(pkt.originate_ts.seconds == 0);
        REQUIRE(pkt.originate_ts.fraction == 0);
        REQUIRE(pkt.receive_ts.seconds == 0);
        REQUIRE(pkt.receive_ts.fraction == 0);
        REQUIRE(pkt.transmit_ts.seconds == 0);
        REQUIRE(pkt.transmit_ts.fraction == 0);
    }
}

// ---------------------------------------------------------------------------
// Byte-order conversion tests
// ---------------------------------------------------------------------------

TEST_CASE("NtpHelpers: HtonPacket and NtohPacket are inverse operations", "[ntp][helpers]") {
    NtpPacket original;
    InitPacket(original);

    // Set some known values in host byte order
    original.root_delay            = 0x12345678;
    original.root_dispersion       = 0xAABBCCDD;
    original.reference_ts.seconds  = 0x11223344;
    original.reference_ts.fraction = 0x55667788;
    original.receive_ts.seconds    = 0x99AABBCC;
    original.transmit_ts.seconds   = 0xDDEEFF00;

    NtpPacket converted = original;

    SECTION("Round-trip hton -> ntoh preserves original values") {
        HtonPacket(converted);
        NtohPacket(converted);

        REQUIRE(converted.root_delay == original.root_delay);
        REQUIRE(converted.root_dispersion == original.root_dispersion);
        REQUIRE(converted.reference_ts.seconds == original.reference_ts.seconds);
        REQUIRE(converted.reference_ts.fraction == original.reference_ts.fraction);
        REQUIRE(converted.receive_ts.seconds == original.receive_ts.seconds);
        REQUIRE(converted.transmit_ts.seconds == original.transmit_ts.seconds);
    }
}

// ---------------------------------------------------------------------------
// Timezone parsing tests
// ---------------------------------------------------------------------------

TEST_CASE("NtpHelpers: ParseTimeZoneString parses timezone offsets", "[ntp][helpers]") {
    uint64_t is_west = 0;
    uint64_t offset  = 0;

    SECTION("Parses +08:00 (China Standard Time)") {
        REQUIRE(ParseTimeZoneString("+08:00", is_west, offset));
        REQUIRE(is_west == 0);
        REQUIRE(offset == 800);
    }

    SECTION("Parses -05:00 (US Eastern)") {
        REQUIRE(ParseTimeZoneString("-05:00", is_west, offset));
        REQUIRE(is_west == 1);
        REQUIRE(offset == 500);
    }

    SECTION("Parses +05:30 (India)") {
        REQUIRE(ParseTimeZoneString("+05:30", is_west, offset));
        REQUIRE(is_west == 0);
        REQUIRE(offset == 530);
    }

    SECTION("Parses -09:30 (Marquesas Islands)") {
        REQUIRE(ParseTimeZoneString("-09:30", is_west, offset));
        REQUIRE(is_west == 1);
        REQUIRE(offset == 930);
    }

    SECTION("Parses +00:00 (UTC)") {
        REQUIRE(ParseTimeZoneString("+00:00", is_west, offset));
        REQUIRE(is_west == 0);
        REQUIRE(offset == 0);
    }

    SECTION("Rejects empty string") {
        REQUIRE_FALSE(ParseTimeZoneString("", is_west, offset));
    }

    SECTION("Rejects wrong length") {
        REQUIRE_FALSE(ParseTimeZoneString("+8:00", is_west, offset));
        REQUIRE_FALSE(ParseTimeZoneString("+008:00", is_west, offset));
    }

    SECTION("Rejects missing sign") {
        REQUIRE_FALSE(ParseTimeZoneString("08:00x", is_west, offset));
    }

    SECTION("Rejects missing colon") {
        REQUIRE_FALSE(ParseTimeZoneString("+08000", is_west, offset));
    }
}

// ---------------------------------------------------------------------------
// Constants validation
// ---------------------------------------------------------------------------

TEST_CASE("NtpHelpers: Constants have expected values", "[ntp][helpers]") {
    SECTION("kJan1970 is the NTP epoch offset") {
        // Jan 1, 1970 00:00:00 in NTP timestamp = 2208988800
        REQUIRE(kJan1970 == 0x83aa7e80);
        REQUIRE(kJan1970 == 2208988800u);
    }

    SECTION("kMinSyncIntervalSec is 60 seconds") {
        REQUIRE(kMinSyncIntervalSec == 60);
    }

    SECTION("kNtpTimeoutMs is 10 seconds") {
        REQUIRE(kNtpTimeoutMs == 10000);
    }
}
