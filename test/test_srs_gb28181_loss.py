#!/usr/bin/env python3
"""Exercise the actual patched RTP-to-PS method with observable parser fixtures.

The RTP reader/PS parser are fixtures; the production decode_rtp method is
extracted unchanged. This verifies loss resets partial PES and reserved bytes
before the existing PS recovery path, and preserves ordinary TCP/wrap behavior.
"""
import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile

PREFIX = r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
using srs_error_t = int;
const int srs_success = 0;
#define srs_assert assert
#define srs_freep(p) do { delete (p); (p) = nullptr; } while (0)
int srs_error_wrap(int error, const char*) { return error; }
struct SrsBuffer {
    char* bytes; int length, position = 0;
    SrsBuffer(char* p, int n) : bytes(p), length(n) {}
    int pos() { return position; }
    char* head() { return bytes + position; }
    int left() { return length-position; }
    void skip(int n) { position += n; assert(position >= 0 && position <= length); }
};
struct Payload { virtual ~Payload() {} };
struct SrsRtpRawPayload : Payload { char* payload = nullptr; int nn_payload = 0; };
struct Header {
    uint16_t seq = 0;
    uint16_t get_sequence() { return seq; }
    int get_timestamp() { return 90000; }
    int get_payload_type() { return 96; }
};
struct SrsRtpPacket {
    Header header; SrsRtpRawPayload raw;
    int decode(SrsBuffer* b) {
        assert(b->left() >= 12);
        auto* p = reinterpret_cast<unsigned char*>(b->head());
        header.seq = (p[2] << 8) | p[3]; b->skip(12);
        raw.payload = b->head(); raw.nn_payload = b->left(); return 0;
    }
    Payload* payload() { return &raw; }
};
struct SrsTsMessage {};
struct Context {
    struct Helper { int rtp_seq_ = 0, rtp_ts_ = 0, rtp_pt_ = 0; } helper_;
    int reaps = 0;
    SrsTsMessage* reap() { ++reaps; return new SrsTsMessage; }
};
struct ISrsPsMessageHandler {
    int recovered = 0;
    void on_recover_mode(int) { ++recovered; }
};
struct SrsRecoverablePsContext {
    Context ctx_;
    int recover_ = 0;
    bool rtp_seen_ = false;
    uint16_t rtp_next_ = 0;
    std::string decoded;
    int decode_rtp(SrsBuffer*, int, ISrsPsMessageHandler*);
    int enter_recover_mode(SrsBuffer*, ISrsPsMessageHandler*, int, int error) { return error; }
    int decode(SrsBuffer* b, ISrsPsMessageHandler*) {
        decoded.assign(b->head(), b->left()); b->skip(b->left()); return 0;
    }
};
'''

SUFFIX = r'''
void packet(SrsRecoverablePsContext& c, ISrsPsMessageHandler& h, uint16_t seq, const std::string& reserved) {
    std::string data = reserved + std::string(12, '\0');
    data[reserved.size()+2] = char(seq >> 8); data[reserved.size()+3] = char(seq);
    data += "new-ps";
    SrsBuffer b(data.data(), data.size());
    assert(c.decode_rtp(&b, reserved.size(), &h) == 0);
    assert(b.left() == 0);
}
int main() {
    SrsRecoverablePsContext c;
    ISrsPsMessageHandler h;
    packet(c, h, 65535, ""); assert(h.recovered == 0);
    packet(c, h, 0, "prefix"); assert(h.recovered == 0);
    assert(c.decoded == "prefixnew-ps");
    packet(c, h, 2, "damaged");
    assert(h.recovered == 1 && c.ctx_.reaps == 1 && c.recover_ == 1);
    assert(c.decoded == "new-ps");
    packet(c, h, 3, "valid"); assert(h.recovered == 1);
    assert(c.decoded == "validnew-ps");
    std::cout << "SRS actual decode_rtp: contiguous, wrap, gap and partial PES reset PASS\n";
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    args = parser.parse_args()
    source = args.source.read_text(encoding="utf-8")
    start = source.index("srs_error_t SrsRecoverablePsContext::decode_rtp(")
    end = source.index("srs_error_t SrsRecoverablePsContext::decode(", start)
    method = source[start:end]
    assert "COSMO_GB_RTP_LOSS_V1" in method
    compiler = shutil.which("c++") or shutil.which("g++") or shutil.which("clang++")
    if not compiler:
        raise SystemExit("A native C++ compiler is required")
    with tempfile.TemporaryDirectory(prefix="cosmo-gb-loss-") as temporary:
        binary = Path(temporary) / "loss-test"
        subprocess.run([compiler, "-std=c++17", "-x", "c++", "-", "-o", str(binary)],
                       input=PREFIX + method + SUFFIX, text=True, check=True)
        subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    main()
