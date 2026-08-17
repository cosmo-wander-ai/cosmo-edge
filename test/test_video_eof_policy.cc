#include "catch_amalgamated.hpp"

#include "flow/channel/VideoEofPolicy.h"

using cosmo::flow::DecideVideoEof;
using cosmo::flow::IsTerminalOfflineReadEnd;
using cosmo::flow::VideoEofDisposition;

TEST_CASE("Infinite local video EOF always reopens", "[video-eof][repeat]") {
    for (int read_count = 1; read_count <= 1000; ++read_count) {
        CHECK(DecideVideoEof(false, read_count, 0) == VideoEofDisposition::Reopen);
    }
}

TEST_CASE("Finite local video EOF completes only after the configured playback count",
          "[video-eof][repeat]") {
    CHECK(DecideVideoEof(false, 1, 1) == VideoEofDisposition::Complete);

    CHECK(DecideVideoEof(false, 1, 3) == VideoEofDisposition::Reopen);
    CHECK(DecideVideoEof(false, 2, 3) == VideoEofDisposition::Reopen);
    CHECK(DecideVideoEof(false, 3, 3) == VideoEofDisposition::Complete);
}

TEST_CASE("Live stream EOF remains a reopen condition", "[video-eof][live]") {
    CHECK(DecideVideoEof(true, 1, 1) == VideoEofDisposition::Reopen);
    CHECK(DecideVideoEof(true, 1000, 3) == VideoEofDisposition::Reopen);
}

TEST_CASE("Camera monitor rejects terminal interpretation while reopen is pending",
          "[video-eof][monitor]") {
    CHECK_FALSE(IsTerminalOfflineReadEnd(false, false));
    CHECK_FALSE(IsTerminalOfflineReadEnd(false, true));
    CHECK_FALSE(IsTerminalOfflineReadEnd(true, true));
    CHECK(IsTerminalOfflineReadEnd(true, false));
}
