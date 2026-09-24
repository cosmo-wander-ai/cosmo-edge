#!/usr/bin/env python3
"""Run actual patched mux dispatch and session error handling with PS fixtures.

Codec internals are instrumented fixtures. This checks that unsupported PES never
reaches AAC, supported AAC and video do, and repeated failures free every error
while preserving subsequent video. No camera payload is checked into the repo.
"""
import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile

PREFIX = r'''
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>
using namespace std;
using srs_utime_t = int64_t;
const int SRS_UTIME_SECONDS = 1000000;
srs_utime_t clock_now = 1;
int warnings = 0, errors_freed = 0;
srs_utime_t srs_update_system_time() { return clock_now; }
struct Error {};
using srs_error_t = Error*;
const srs_error_t srs_success = nullptr;
string srs_error_desc(Error*) { return "fixture"; }
srs_error_t srs_error_wrap(Error* e, const char*) { return e; }
void free_error(Error*& e) { if (e) { ++errors_freed; delete e; e = nullptr; } }
#define srs_freep(e) free_error(e)
#define srs_warn(...) (++warnings)
template<class T> using SrsUniquePtr = unique_ptr<T>;
const int SrsTsPESStreamIdVideoCommon = 0xe0, SrsTsPESStreamIdAudioCommon = 0xc0;
const int SrsTsStreamAudioAAC = 0x0f;
struct SrsPsContext { int audio_stream_type_ = 0; };
struct SrsPsDecodeHelper { SrsPsContext* ctx_ = nullptr; };
struct Payload {
    string value;
    char* bytes() { return const_cast<char*>(value.data()); }
    int length() { return value.size(); }
    void append(Payload* p) { value += p->value; }
};
struct SrsTsMessage {
    int sid = 0; int64_t pts = 0, dts = 0; void* ps_helper_ = nullptr;
    Payload storage; Payload* payload = &storage;
};
struct SrsBuffer { SrsBuffer(char*, int) {} };
struct SrsGbMuxer {
    int audios = 0, videos = 0; bool malformed = false;
    srs_error_t on_ts_message(SrsTsMessage*);
    srs_error_t on_ts_audio(SrsTsMessage*, SrsBuffer*) {
        ++audios; return malformed ? new Error : srs_success;
    }
    srs_error_t on_ts_video(SrsTsMessage*, SrsBuffer*) { ++videos; return srs_success; }
};
struct SrsPackContext {
    int media_id_ = 0, media_startime_ = 0, media_nn_recovered_ = 0;
    int media_nn_msgs_dropped_ = 0, media_reserved_ = 0;
};
struct SrsPsPacket {};
struct SrsGbSession {
    SrsGbMuxer* muxer_;
    srs_utime_t external_deadline_ = 0, next_audio_warning_ = 0;
    int media_id_ = 0, media_msgs_ = 0, media_packs_ = 0, media_starttime_ = 0;
    int media_recovered_ = 0, media_msgs_dropped_ = 0, media_reserved_ = 0;
    int total_msgs_ = 0, total_packs_ = 0, total_recovered_ = 0;
    int total_msgs_dropped_ = 0, total_reserved_ = 0;
    void on_ps_pack(SrsPackContext*, SrsPsPacket*, const vector<SrsTsMessage*>&);
};
'''

SUFFIX = r'''
int main() {
#ifdef _MSC_VER
    // Keep expected red runs non-interactive in Windows automation.
    _set_error_mode(_OUT_TO_STDERR);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
    SrsGbMuxer mux;
    SrsTsMessage audio, video;
    SrsPsContext context;
    SrsPsDecodeHelper helper; helper.ctx_ = &context;
    audio.sid = 0xc0; audio.ps_helper_ = &helper; audio.storage.value = "not-adts";
    video.sid = 0xe0; video.storage.value = "video";
    for (int codec : {0, 0x90, 0x91, 0x03, 0xff}) {
        context.audio_stream_type_ = codec;
        for (int i=0; i<1000; ++i) assert(mux.on_ts_message(&audio) == nullptr);
    }
    audio.ps_helper_ = nullptr;
    assert(mux.on_ts_message(&audio) == nullptr);
    audio.ps_helper_ = &helper; helper.ctx_ = nullptr;
    assert(mux.on_ts_message(&audio) == nullptr);
    helper.ctx_ = &context;
    assert(mux.audios == 0);
    context.audio_stream_type_ = 0x0f;
    assert(mux.on_ts_message(&audio) == nullptr && mux.audios == 1);
    audio.sid = 0xbd;
    assert(mux.on_ts_message(&audio) == nullptr && mux.audios == 1);
    audio.sid = 0xc0;
    assert(mux.on_ts_message(&video) == nullptr && mux.videos == 1);
    mux.malformed = true;
    SrsGbSession session; session.muxer_ = &mux;
    SrsPackContext pack;
    for (int i=0; i<1000; ++i) session.on_ps_pack(&pack, nullptr, {&audio, &video});
    assert(warnings == 1 && errors_freed == 1000 && mux.videos == 1001);
    clock_now += 10 * SRS_UTIME_SECONDS;
    session.on_ps_pack(&pack, nullptr, {&audio, &video});
    assert(warnings == 2 && errors_freed == 1001 && mux.videos == 1002);
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("--compiler", help="Native C++ compiler path (cl.exe needs a VS developer environment)")
    args = parser.parse_args()
    source = args.source.read_text(encoding="utf-8")
    start = source.index("srs_error_t SrsGbMuxer::on_ts_message(")
    dispatch = source[start:source.index("srs_error_t SrsGbMuxer::on_ts_video(", start)]
    start = source.index("void SrsGbSession::on_ps_pack(")
    session = source[start:source.index("void SrsGbSession::on_sip_transport(", start)]
    compiler = (args.compiler or shutil.which("c++") or shutil.which("g++")
                or shutil.which("clang++") or shutil.which("cl"))
    if not compiler:
        raise SystemExit("A native C++ compiler is required")
    with tempfile.TemporaryDirectory(prefix="cosmo-gb-audio-") as temporary:
        program = PREFIX + dispatch + session + SUFFIX
        if Path(compiler).name.lower() in {"cl", "cl.exe", "clang-cl", "clang-cl.exe"}:
            binary = Path(temporary) / "audio-test.exe"
            source_file = Path(temporary) / "audio-test.cpp"
            source_file.write_text(program, encoding="utf-8")
            subprocess.run([compiler, "/nologo", "/std:c++17", "/EHsc",
                            str(source_file), "/Fe" + str(binary)],
                           cwd=temporary, check=True)
        else:
            binary = Path(temporary) / "audio-test"
            subprocess.run([compiler, "-std=c++17", "-x", "c++", "-", "-o", str(binary)],
                           input=program, text=True, check=True)
        subprocess.run([str(binary)], check=True)
    print("GB actual audio dispatch + session warning/free path PASS")


if __name__ == "__main__":
    main()
