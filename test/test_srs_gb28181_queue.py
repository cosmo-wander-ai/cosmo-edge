#!/usr/bin/env python3
"""Compile the actual patched SRS dequeue method and exercise video-only startup.

Run after test_srs_gb28181_patch.cmake, passing its generated SRS source path.
Only the message storage shell is substituted; the dequeue policy is unmodified.
"""
import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile

PROGRAM_PREFIX = r"""
#include <cassert>
#include <cstdint>
#include <map>
#include <iostream>
struct SrsSharedPtrMessage {
    bool audio;
    bool is_audio() const { return audio; }
    bool is_video() const { return !audio; }
};
struct SrsMpegpsQueue {
    int nb_audios = 0, nb_videos = 0;
    std::map<int64_t, SrsSharedPtrMessage*> msgs;
    ~SrsMpegpsQueue() { for (auto& v : msgs) delete v.second; }
    void add(int64_t ts, bool audio = false) {
        assert(!msgs.count(ts));
        msgs[ts] = new SrsSharedPtrMessage{audio};
        if (audio) ++nb_audios; else ++nb_videos;
    }
    SrsSharedPtrMessage* dequeue();
};
"""
PROGRAM_SUFFIX = r"""
int main() {
    SrsMpegpsQueue empty;
    assert(!empty.dequeue());
    for (int interval : {40, 100}) {
        SrsMpegpsQueue q;
        int first = -1, sent = 0;
        for (int frame = 0; frame < 120; ++frame) {
            q.add(frame * interval);
            while (auto* msg = q.dequeue()) {
                if (first < 0) first = frame * interval;
                ++sent;
                delete msg;
            }
        }
        assert(first >= 0 && first <= 200);
        assert(sent >= 114);
    }
    SrsMpegpsQueue mixed;
    mixed.add(0); mixed.add(20, true); mixed.add(40); mixed.add(60, true);
    auto* msg = mixed.dequeue();
    assert(msg && msg->is_video()); delete msg;
    // Missing audio must not block the continuing video.
    for (int ts = 80; ts <= 800; ts += 40) {
        mixed.add(ts);
        while (auto* ready = mixed.dequeue()) delete ready;
    }
    assert(mixed.msgs.size() <= 6);
    SrsMpegpsQueue dense;
    for (int i = 0; i < 100; ++i) {
        dense.add(i);
        while (auto* ready = dense.dequeue()) delete ready;
    }
    assert(dense.msgs.size() <= 32);
    std::cout << "SRS actual dequeue: video-only 10/25fps, mixed, audio loss, count bound PASS\n";
}
"""


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    args = parser.parse_args()
    source = args.source.read_text(encoding="utf-8")
    start = source.index("SrsSharedPtrMessage* SrsMpegpsQueue::dequeue()")
    end = source.index("SrsGbMuxer::SrsGbMuxer", start)
    program = PROGRAM_PREFIX + source[start:end] + PROGRAM_SUFFIX
    compiler = shutil.which("c++") or shutil.which("g++") or shutil.which("clang++")
    if not compiler:
        raise SystemExit("A native C++ compiler is required for the SRS queue regression")
    with tempfile.TemporaryDirectory(prefix="cosmo-gb-queue-") as temporary:
        binary = Path(temporary) / "queue-test"
        subprocess.run([compiler, "-std=c++11", "-x", "c++", "-", "-o", str(binary)],
                       input=program, text=True, check=True)
        subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    main()
