#!/usr/bin/env python3
"""Exercise actual patched H.264 mux methods with single/multi-slice Annex B data.

The mux loop, frame writer and raw AVC codec methods come from the SRS sources.
Only buffer storage, logging, sequence-header setup and RTMP delivery are fixtures;
this checks emitted FLV messages, not device decoding or RTP/PS transport.
Run after test_srs_gb28181_patch.cmake with its generated SRS source path.
"""
import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile

PREFIX = r'''
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <vector>
using namespace std;
#define CHECK(condition) do { if (!(condition)) { \
    cerr << "Check failed: " #condition << " at line " << __LINE__ << endl; \
    exit(1); } } while (false)
#define srs_assert(condition) CHECK(condition)
#define srs_warn(...) ((void)0)
using srs_error_t = int;
const srs_error_t srs_success = 0;
srs_error_t srs_error_new(int code, const char*) { return code; }
srs_error_t srs_error_wrap(srs_error_t err, const char*) { return err; }
string srs_string_dumps_hex(char*, int, int) { return ""; }
template<class T> using SrsUniquePtr = unique_ptr<T>;
const int ERROR_H264_API_NO_PREFIXED = 1, ERROR_H264_DROP_BEFORE_SPS_PPS = 2;
const int ERROR_STREAM_CASTER_AVC_PPS = 3;
enum SrsAvcNaluType {
    SrsAvcNaluTypeNonIDR = 1, SrsAvcNaluTypeIDR = 5, SrsAvcNaluTypeSEI = 6,
    SrsAvcNaluTypeSPS = 7, SrsAvcNaluTypePPS = 8, SrsAvcNaluTypeAccessUnitDelimiter = 9
};
enum SrsVideoAvcFrameType { SrsVideoAvcFrameTypeKeyFrame = 1, SrsVideoAvcFrameTypeInterFrame = 2 };
const int SrsVideoAvcFrameTraitNALU = 1, SrsVideoCodecIdAVC = 7, SrsFrameTypeVideo = 9;
class SrsBuffer {
    char* bytes_; int size_, position_ = 0;
public:
    SrsBuffer(char* bytes, int size) : bytes_(bytes), size_(size) {}
    bool empty() const { return position_ >= size_; }
    bool require(int size) const { return size <= size_ - position_; }
    int pos() const { return position_; }
    char* data() { return bytes_; }
    char* head() { return bytes_ + position_; }
    void skip(int size) { CHECK(require(size)); position_ += size; }
    void write_4bytes(int32_t value) {
        CHECK(require(4));
        for (int shift = 24; shift >= 0; shift -= 8) bytes_[position_++] = (value >> shift) & 0xff;
    }
    void write_bytes(char* bytes, int size) {
        CHECK(require(size)); memcpy(head(), bytes, size); skip(size);
    }
};
struct SrsRawH264Stream {
    srs_error_t annexb_demux(SrsBuffer*, char**, int*);
    bool is_sps(char*, int);
    bool is_pps(char*, int);
    srs_error_t sps_demux(char*, int, string&);
    srs_error_t pps_demux(char*, int, string&);
    srs_error_t mux_ipb_frame(char*, int, string&);
    srs_error_t mux_avc2flv(string, int8_t, int8_t, uint32_t, uint32_t, char**, int*);
};
struct SrsTsMessage { int64_t dts = 90000; };
struct Packet { uint32_t timestamp; string payload; };
struct SrsGbMuxer {
    SrsRawH264Stream codec;
    SrsRawH264Stream* avc_ = &codec;
    string h264_sps_, h264_pps_;
    bool h264_sps_changed_ = false, h264_pps_changed_ = false;
    bool h264_sps_pps_sent_ = true;
    vector<Packet> packets;
    srs_error_t mux_h264(SrsTsMessage*, SrsBuffer*);
    srs_error_t write_h264_ipb_frame(const string&, bool, uint32_t, uint32_t);
    srs_error_t write_h264_sps_pps(uint32_t, uint32_t) { return srs_success; }
    srs_error_t rtmp_write_packet(int type, uint32_t timestamp, char* bytes, int size) {
        CHECK(type == SrsFrameTypeVideo);
        packets.push_back(Packet{timestamp, string(bytes, size)});
        delete[] bytes;
        return srs_success;
    }
};
'''

SUFFIX = r'''
string annexb(const vector<string>& nalus) {
    string bytes;
    for (const auto& nalu : nalus) { bytes.append("\0\0\0\1", 4); bytes += nalu; }
    return bytes;
}
vector<string> flv_nalus(const string& payload) {
    CHECK(payload.size() >= 5 && (payload[0] & 0xf) == SrsVideoCodecIdAVC);
    CHECK(payload[1] == SrsVideoAvcFrameTraitNALU);
    vector<string> nalus;
    size_t pos = 5;
    while (pos < payload.size()) {
        CHECK(pos + 4 <= payload.size());
        uint32_t size = 0;
        for (int i = 0; i < 4; ++i) size = (size << 8) | uint8_t(payload[pos++]);
        CHECK(size > 0 && size <= payload.size() - pos);
        nalus.push_back(payload.substr(pos, size));
        pos += size;
    }
    return nalus;
}
int main() {
    // Compare 50-frame streams with/without trailing filler after one or three slices.
    for (bool keyframe : {false, true}) {
        for (int slice_count : {1, 3}) {
            for (bool filler : {false, true}) {
                SrsGbMuxer mux;
                vector<string> slices;
                for (int i = 0; i < slice_count; ++i) {
                    slices.push_back(string(1, keyframe ? '\x65' : '\x41') + char(0x80 + i));
                }
                auto nalus = slices;
                if (filler) nalus.push_back(string("\x0c\xff\x80", 3));
                string bytes = annexb(nalus);
                for (int frame = 0; frame < 50; ++frame) {
                    SrsTsMessage message;
                    message.dts += frame * 3600;
                    SrsBuffer buffer(&bytes[0], bytes.size());
                    CHECK(mux.mux_h264(&message, &buffer) == srs_success);
                    CHECK(mux.packets.size() == size_t(frame + 1));
                    const auto& packet = mux.packets.back();
                    CHECK(packet.timestamp == uint32_t(message.dts / 90));
                    CHECK((uint8_t(packet.payload[0]) >> 4) == (keyframe ? 1 : 2));
                    CHECK(flv_nalus(packet.payload) == slices);
                }
            }
        }
    }
    SrsGbMuxer empty;
    string filler = annexb({string("\x0c\xff\x80", 3)});
    SrsBuffer buffer(&filler[0], filler.size());
    SrsTsMessage message;
    CHECK(empty.mux_h264(&message, &buffer) == srs_success);
    CHECK(empty.packets.empty());
    cout << "SRS actual H264 mux: single/multi-slice, IDR/P, trailing filler, 400 frames PASS\n";
}
'''


def function(source, signature):
    """Extract a complete upstream function without changing its body."""
    start = source.index(signature)
    end = source.index("\n}\n", start) + 3
    return source[start:end] + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("--compiler", help="Native C++ compiler path")
    args = parser.parse_args()
    source = args.source.read_text(encoding="utf-8")
    vendor = Path(__file__).resolve().parents[1] / "3rd/srs-6.0-r0/trunk/src"
    raw = (vendor / "protocol/srs_protocol_raw_avc.cpp").read_text(encoding="utf-8")
    utility = (vendor / "kernel/srs_kernel_utility.cpp").read_text(encoding="utf-8")
    methods = function(utility, "bool srs_avc_startswith_annexb(")
    for name in ("annexb_demux", "is_sps", "is_pps", "sps_demux", "pps_demux",
                 "mux_ipb_frame", "mux_avc2flv"):
        result = "bool" if name.startswith("is_") else "srs_error_t"
        methods += function(raw, f"{result} SrsRawH264Stream::{name}(")
    for name in ("mux_h264", "write_h264_ipb_frame"):
        methods += function(source, f"srs_error_t SrsGbMuxer::{name}(")
    compiler = (args.compiler or shutil.which("c++") or shutil.which("g++")
                or shutil.which("clang++") or shutil.which("cl"))
    if not compiler:
        raise SystemExit("A native C++ compiler is required")
    with tempfile.TemporaryDirectory(prefix="cosmo-gb-frames-") as temporary:
        program = PREFIX + methods + SUFFIX
        if Path(compiler).name.lower() in {"cl", "cl.exe", "clang-cl", "clang-cl.exe"}:
            binary = Path(temporary) / "frames-test.exe"
            source_file = Path(temporary) / "frames-test.cpp"
            source_file.write_text(program, encoding="utf-8")
            subprocess.run([compiler, "/nologo", "/std:c++17", "/EHsc",
                            str(source_file), "/Fe" + str(binary)],
                           cwd=temporary, check=True)
        else:
            binary = Path(temporary) / "frames-test"
            subprocess.run([compiler, "-std=c++17", "-x", "c++", "-", "-o", str(binary)],
                           input=program, text=True, check=True)
        subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    main()
