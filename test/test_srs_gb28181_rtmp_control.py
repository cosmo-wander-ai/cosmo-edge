#!/usr/bin/env python3
"""Exercise patched GB RTMP methods with a blocking control-message peer.

Uses the actual patched GB lifecycle and H.264 sequence-header methods, RTMP
receive/response handlers and complete packet/message/response-flush call chain.
Only transport, packet wire decoding, raw FLV framing and the coroutine scheduler
are fixtures. This checks ownership, nonrecursive flushing and recovery, not
State Threads, real RTMP framing or device acceptance.
Run after test_srs_gb28181_patch.cmake, passing its generated GB source file.
"""
import argparse
import contextlib
import hashlib
from pathlib import Path
import shutil
import subprocess
import tempfile


PREFIX = r'''
#include <atomic>
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <functional>
#include <iostream>
#include <mutex>
#include <memory>
#include <string>
#include <thread>
#include <vector>
using namespace std;
#define CHECK(condition) do { if (!(condition)) { \
    cerr << "Check failed: " #condition << " at line " << __LINE__ << endl; \
    _Exit(1); } } while (false)
#define srs_assert(condition) CHECK(condition)
#define srs_trace(...) ((void)0)
#define srs_freep(ptr) do { delete (ptr); (ptr) = NULL; } while (false)
#define srs_freepa(ptr) do { free_payload(ptr); (ptr) = NULL; } while (false)
using srs_error_t = int;
using srs_utime_t = int64_t;
const int srs_success = 0, ERR_EOF = 1, ERR_INTERRUPTED = 2, ERR_START = 3;
const int ERR_CONNECT = 4, ERR_PUBLISH = 5, ERR_SEND = 6, ERR_CREATE = 7;
const int SRS_CONSTS_RTMP_PROTOCOL_CHUNK_SIZE = 128;
const srs_utime_t SRS_CONSTS_RTMP_TIMEOUT = 3000, SRS_CONSTS_RTMP_PULSE = 100;
const srs_utime_t SRS_UTIME_NO_TIMEOUT = -1;
srs_error_t srs_error_wrap(int err, const char*, ...) { return err; }
int srsu2msi(srs_utime_t value) { return value; }
string srs_string_replace(string value, string from, string to) {
    auto pos = value.find(from);
    if (pos != string::npos) value.replace(pos, from.size(), to);
    return value;
}
atomic<int> worker_count{0}, sdk_count{0}, message_count{0}, payload_count{0};
atomic<int> packet_count{0}, controls_read{0}, video_sent{0};
atomic<int> message_peak{0};
vector<string> video_payloads;
bool fail_start = false, fail_connect = false, fail_publish = false, fail_send = false;
mutex peer_mutex;
condition_variable peer_changed;
int pending_controls = 0;
bool peer_eof = false;
template<class T> using SrsUniquePtr = unique_ptr<T>;
char* allocate_payload(int size) { ++payload_count; return new char[size]; }
char* payload() { return allocate_payload(16); }
void free_payload(char* bytes) { if (bytes) { --payload_count; delete[] bytes; } }
const int RTMP_MSG_SetChunkSize = 1, RTMP_MSG_Acknowledgement = 3;
const int RTMP_MSG_UserControlMessage = 4, RTMP_MSG_WindowAcknowledgementSize = 5;
const int RTMP_MSG_AudioMessage = 8, RTMP_MSG_VideoMessage = 9;
const int SRS_CONSTS_RTMP_MIN_CHUNK_SIZE = 128, SRS_CONSTS_RTMP_MAX_CHUNK_SIZE = 65536;
const int SrcPCUCSetBufferLength = 3, SrcPCUCPingRequest = 6, SrcPCUCPingResponse = 7;
const int ERROR_RTMP_CHUNK_SIZE = 8;
const int SrsVideoAvcFrameTypeKeyFrame = 1, SrsVideoAvcFrameTraitSequenceHeader = 0;
const int SrsFrameTypeVideo = 9;
#define srs_warn(...) ((void)0)
int srs_error_new(int code, const char*, ...) { return code; }
struct SrsMessageHeader {
    int message_type = 0, payload_length = 1, stream_id = 1;
};
struct SrsCommonMessage {
    SrsMessageHeader header;
    int id = 0, event_type = 0, event_data = 0, extra_data = 0;
    SrsCommonMessage() {
        int count = ++message_count;
        int previous = message_peak.load();
        while (count > previous && !message_peak.compare_exchange_weak(previous, count)) {}
        // A regression in the real flush chain must fail before overflowing a stack.
        CHECK(count < 32);
    }
    ~SrsCommonMessage() { --message_count; }
};
struct SrsSharedPtrMessage {
    char* data = NULL;
    int size = 0, id = 0, kind = RTMP_MSG_VideoMessage;
    explicit SrsSharedPtrMessage(char* bytes = NULL) : data(bytes) {}
    ~SrsSharedPtrMessage() { free_payload(data); }
    bool is_audio() const { return false; }
    bool check(int) const { return true; }
    int create(SrsCommonMessage* msg) {
        id = msg->id; kind = msg->header.message_type;
        return srs_success;
    }
};
struct SrsPacket {
    int id;
    explicit SrsPacket(int value = 0) : id(value) { ++packet_count; }
    virtual ~SrsPacket() { --packet_count; }
    virtual int to_msg(SrsCommonMessage* msg, int) {
        msg->id = id; msg->header.message_type = RTMP_MSG_Acknowledgement;
        return srs_success;
    }
};
struct SrsAcknowledgementPacket : SrsPacket {
    uint32_t sequence_number = 0;
    int to_msg(SrsCommonMessage* msg, int stream) override {
        id = sequence_number;
        return SrsPacket::to_msg(msg, stream);
    }
};
struct SrsUserControlPacket : SrsPacket {
    int event_type = 0, event_data = 0, extra_data = 0;
    int to_msg(SrsCommonMessage* msg, int) override {
        msg->header.message_type = RTMP_MSG_UserControlMessage;
        msg->id = event_data;
        return srs_success;
    }
};
struct SrsSetWindowAckSizePacket : SrsPacket { int ackowledgement_window_size = 0; };
struct SrsSetChunkSizePacket : SrsPacket { int chunk_size = 128; };
struct FixtureTransport {
    atomic<int64_t> received{0};
    int64_t get_recv_bytes() { return received; }
};
struct AckWindow {
    uint32_t window = 0, sequence_number = 0;
    int64_t nb_recv_bytes = 0;
};
struct SrsProtocol {
    bool auto_response_when_recv = true;
    bool flushing_responses_ = false;
    int in_chunk_size = 128, in_buffer_length = 0;
    FixtureTransport transport;
    FixtureTransport* skt = &transport;
    AckWindow in_ack_size;
    vector<SrsPacket*> manual_response_queue;
    vector<int> sent;
    function<int(int)> on_send;
    ~SrsProtocol() { for (auto* packet : manual_response_queue) delete packet; }
    void set_auto_response(bool);
    srs_error_t manual_response_flush();
    srs_error_t do_send_and_free_packet(SrsPacket*, int);
    srs_error_t send_and_free_message(SrsSharedPtrMessage*, int);
    srs_error_t send_and_free_messages(SrsSharedPtrMessage**, int, int);
    srs_error_t on_recv_message(SrsCommonMessage*);
    srs_error_t response_acknowledgement_message();
    srs_error_t response_ping_message(int32_t);
    void print_debug_info() {}
    // The real hook has no state updates for the ACK/Ping packets tested here.
    int on_send_packet(SrsMessageHeader*, SrsPacket*) { return srs_success; }
    int decode_message(SrsCommonMessage* msg, SrsPacket** packet) {
        switch (msg->header.message_type) {
            case RTMP_MSG_UserControlMessage: {
                auto* p = new SrsUserControlPacket();
                p->event_type = msg->event_type; p->event_data = msg->event_data;
                p->extra_data = msg->extra_data; *packet = p; break;
            }
            case RTMP_MSG_WindowAcknowledgementSize: {
                auto* p = new SrsSetWindowAckSizePacket();
                p->ackowledgement_window_size = msg->event_data; *packet = p; break;
            }
            case RTMP_MSG_SetChunkSize: {
                auto* p = new SrsSetChunkSizePacket();
                p->chunk_size = msg->event_data; *packet = p; break;
            }
            default: CHECK(false);
        }
        return srs_success;
    }
    int do_send_messages(SrsSharedPtrMessage** msgs, int count) {
        for (int n = 0; n < count; ++n) {
            if (fail_send) return ERR_SEND;
            auto* msg = msgs[n];
            if (msg->kind == RTMP_MSG_VideoMessage) {
                ++video_sent;
                if (msg->data) video_payloads.emplace_back(msg->data, msg->size);
            } else {
                sent.push_back(msg->id);
                if (on_send) {
                    int err = on_send(msg->id);
                    if (err != srs_success) return err;
                }
            }
        }
        return srs_success;
    }
};
struct SrsRtmpClient {
    SrsProtocol storage;
    SrsProtocol* protocol = &storage;
    void set_auto_response(bool);
};
struct ISrsCoroutineHandler {
    virtual ~ISrsCoroutineHandler() {}
    virtual srs_error_t cycle() = 0;
};
struct SrsCoroutine {
    virtual ~SrsCoroutine() {}
    virtual srs_error_t start() = 0;
    virtual void stop() = 0;
    virtual srs_error_t pull() = 0;
};
struct SrsSTCoroutine;
thread_local SrsSTCoroutine* current_coroutine = NULL;
struct SrsSTCoroutine : SrsCoroutine {
    ISrsCoroutineHandler* handler;
    thread worker;
    atomic<bool> interrupted{false};
    atomic<int> error{srs_success};
    SrsSTCoroutine(const char*, ISrsCoroutineHandler* value) : handler(value) {}
    ~SrsSTCoroutine() { stop(); }
    srs_error_t start() {
        if (fail_start) { fail_start = false; return ERR_START; }
        ++worker_count;
        worker = thread([this] {
            current_coroutine = this;
            error = handler->cycle();
            --worker_count;
        });
        return srs_success;
    }
    void stop() {
        // Pair the predicate update with recv_message's wait mutex so a
        // notification cannot be lost between its check and the actual wait.
        {
            lock_guard<mutex> lock(peer_mutex);
            interrupted = true;
        }
        peer_changed.notify_all();
        if (worker.joinable()) worker.join();
    }
    srs_error_t pull() { return interrupted ? ERR_INTERRUPTED : error.load(); }
};
struct SrsBasicRtmpClient {
    SrsRtmpClient storage;
    SrsRtmpClient* client = &storage;
    srs_utime_t recv_timeout = 100;
    SrsBasicRtmpClient(string, srs_utime_t, srs_utime_t) { ++sdk_count; }
    virtual ~SrsBasicRtmpClient() {
        // Client destruction while recv_message is blocked would be a use-after-free.
        CHECK(worker_count == 0);
        --sdk_count;
    }
    void set_auto_response(bool);
    void set_recv_timeout(srs_utime_t value) { recv_timeout = value; }
    srs_error_t connect() { return fail_connect ? ERR_CONNECT : srs_success; }
    srs_error_t publish(int) { return fail_publish ? ERR_PUBLISH : srs_success; }
    int sid() { return 1; }
    srs_error_t recv_message(SrsCommonMessage** message) {
        CHECK(recv_timeout == SRS_UTIME_NO_TIMEOUT);
        CHECK(!client->protocol->auto_response_when_recv);
        unique_lock<mutex> lock(peer_mutex);
        peer_changed.wait(lock, [] {
            return pending_controls || peer_eof || current_coroutine->interrupted.load();
        });
        if (current_coroutine->interrupted) return ERR_INTERRUPTED;
        if (peer_eof) return ERR_EOF;
        --pending_controls;
        *message = new SrsCommonMessage();
        (*message)->header.message_type = RTMP_MSG_Acknowledgement;
        client->protocol->transport.received += 16;
        ++controls_read;
        return client->protocol->on_recv_message(*message);
    }
    srs_error_t send_and_free_message(SrsSharedPtrMessage* message) {
        return client->protocol->send_and_free_message(message, 1);
    }
};
struct SrsSimpleRtmpClient : SrsBasicRtmpClient {
    using SrsBasicRtmpClient::SrsBasicRtmpClient;
};
struct SrsBuffer {
    char* bytes; int position = 0;
    SrsBuffer(char* data, int) : bytes(data) {}
    void write_1bytes(char value) { bytes[position++] = value; }
    void write_2bytes(int16_t value) { write_1bytes(value >> 8); write_1bytes(value); }
    void write_string(const string& value) { for (char c : value) write_1bytes(c); }
};
struct SrsRawH264Stream {
    int mux_sequence_header(string, string, string&);
    int mux_avc2flv(const string& sh, int, int, uint32_t, uint32_t, char** bytes, int* size) {
        *size = sh.size(); *bytes = allocate_payload(*size);
        copy(sh.begin(), sh.end(), *bytes);
        return srs_success;
    }
};
struct SrsRawHEVCStream {};
struct SrsRawAacStream {};
struct SrsPithyPrint {
    static SrsPithyPrint* create_caster() { return new SrsPithyPrint(); }
    bool can_print() { return false; }
    int age() { return 0; }
};
struct SrsMpegpsQueue {
    deque<SrsSharedPtrMessage*> messages;
    ~SrsMpegpsQueue() { for (auto* msg : messages) delete msg; }
    srs_error_t push(SrsSharedPtrMessage* msg) { messages.push_back(msg); return 0; }
    SrsSharedPtrMessage* dequeue() {
        if (messages.empty()) return NULL;
        auto* msg = messages.front(); messages.pop_front(); return msg;
    }
};
struct SrsGbSession {
    SrsGbSession* sip_transport() { return this; }
    string device_id() { return "test-stream"; }
};
struct SrsGbMuxer : ISrsCoroutineHandler {
    SrsSimpleRtmpClient* sdk_;
    SrsCoroutine* rtmp_receiver_;
    SrsGbSession* session_;
    string output_ = "rtmp://fixture/live/[stream]";
    SrsRawH264Stream* avc_;
    bool h264_sps_changed_, h264_pps_changed_, h264_sps_pps_sent_;
    string h264_sps_, h264_pps_;
#ifdef SRS_H265
    SrsRawHEVCStream* hevc_;
    bool vps_sps_pps_sent_, vps_sps_pps_change_;
    string h265_vps_, h265_sps_, h265_pps_;
#endif
    SrsRawAacStream* aac_;
    string aac_specific_config_;
    SrsMpegpsQueue* queue_;
    SrsPithyPrint* pprint_;
    explicit SrsGbMuxer(SrsGbSession*);
    ~SrsGbMuxer();
    srs_error_t cycle();
    srs_error_t connect();
    void close();
    srs_error_t write_h264_sps_pps(uint32_t, uint32_t);
    srs_error_t rtmp_write_packet(char, uint32_t, char*, int);
};
srs_error_t srs_rtmp_create_msg(char type, uint32_t, char* bytes, int size, int,
                               SrsSharedPtrMessage** result) {
    if (!type) { free_payload(bytes); return ERR_CREATE; }
    *result = new SrsSharedPtrMessage(bytes);
    (*result)->size = size;
    return srs_success;
}
template<class Predicate> void wait_for(Predicate ready) {
    auto deadline = chrono::steady_clock::now() + chrono::seconds(3);
    while (!ready()) {
        CHECK(chrono::steady_clock::now() < deadline);
        this_thread::sleep_for(chrono::milliseconds(1));
    }
}
void reset_peer() {
    lock_guard<mutex> lock(peer_mutex);
    CHECK(worker_count == 0 && sdk_count == 0 && message_count == 0 && payload_count == 0);
    pending_controls = 0; peer_eof = false;
}
'''


SUFFIX = r'''
void receive_control(SrsProtocol& protocol, int type, int value = 0, int event = 0) {
    SrsCommonMessage message;
    message.header.message_type = type;
    message.event_type = event;
    message.event_data = value;
    CHECK(protocol.on_recv_message(&message) == srs_success);
}
void check_response_chain() {
    SrsProtocol protocol;
    protocol.set_auto_response(false);
    CHECK(!protocol.flushing_responses_);
    for (int n = 1; n <= 12000; ++n) {
        receive_control(protocol, RTMP_MSG_UserControlMessage, n, SrcPCUCPingRequest);
    }
    CHECK(protocol.manual_response_queue.size() == 12000 && protocol.sent.empty());
    protocol.on_send = [&](int id) {
        // Exercise the actual on_recv_message/Ping queue path while sending yields.
        // Appending enough responses reallocates the vector that the writer owns.
        if (id == 1) {
            for (int n = 12001; n <= 17000; ++n) {
                receive_control(protocol, RTMP_MSG_UserControlMessage, n, SrcPCUCPingRequest);
            }
        }
        return srs_success;
    };
    message_peak = 0;
    CHECK(protocol.manual_response_flush() == srs_success);
    CHECK(protocol.sent.size() == 17000 && protocol.manual_response_queue.empty());
    CHECK(message_peak <= 2 && !protocol.flushing_responses_);
    for (int n = 1; n <= 17000; ++n) CHECK(protocol.sent[n - 1] == n);

    protocol.on_send = nullptr;
    protocol.sent.clear();
    receive_control(protocol, RTMP_MSG_WindowAcknowledgementSize, 128);
    for (int n = 1; n <= 10000; ++n) {
        protocol.transport.received = n * 64;
        receive_control(protocol, RTMP_MSG_Acknowledgement);
    }
    CHECK(protocol.manual_response_queue.size() == 10000);
    message_peak = 0;
    CHECK(protocol.manual_response_flush() == srs_success);
    CHECK(protocol.sent.size() == 10000 && message_peak <= 2);
    for (int n = 1; n <= 10000; ++n) CHECK(protocol.sent[n - 1] == n * 64);
    CHECK(!protocol.flushing_responses_ && protocol.manual_response_queue.empty());

    // A send failure must preserve remaining ownership and allow a later flush.
    protocol.in_ack_size.window = 0;
    receive_control(protocol, RTMP_MSG_UserControlMessage, 10001, SrcPCUCPingRequest);
    receive_control(protocol, RTMP_MSG_UserControlMessage, 10002, SrcPCUCPingRequest);
    protocol.on_send = [](int) { return ERR_SEND; };
    CHECK(protocol.manual_response_flush() == ERR_SEND);
    CHECK(protocol.manual_response_queue.size() == 1 && !protocol.flushing_responses_);
    protocol.on_send = nullptr;
    CHECK(protocol.manual_response_flush() == srs_success);
    CHECK(protocol.manual_response_queue.empty() && !protocol.flushing_responses_);
}
void check_h264_recovery(SrsGbSession& session) {
    const string sps("\x67\x42\x00\x1f", 4), pps("\x68\x01\x02", 3);
    for (bool pps_first : {false, true}) {
        reset_peer();
        SrsGbMuxer mux(&session);
        CHECK(mux.connect() == srs_success);
        mux.h264_sps_ = sps; mux.h264_pps_ = pps;
        mux.h264_sps_changed_ = mux.h264_pps_changed_ = true;
        auto count = video_payloads.size();
        fail_send = true;
        CHECK(mux.write_h264_sps_pps(0, 0) == ERR_SEND);
        fail_send = false;
        CHECK(!mux.sdk_ && !mux.rtmp_receiver_);
        CHECK(!mux.h264_sps_pps_sent_ && !mux.h264_sps_changed_ && !mux.h264_pps_changed_);
        CHECK(mux.h264_sps_.empty() && mux.h264_pps_.empty());
        CHECK(video_payloads.size() == count && payload_count == 0);
        CHECK(mux.connect() == srs_success);

        // Match the per-parameter updates made by mux_h264, checking each boundary.
        if (pps_first) {
            mux.h264_pps_ = pps; mux.h264_pps_changed_ = true;
        } else {
            mux.h264_sps_ = sps; mux.h264_sps_changed_ = true;
        }
        CHECK(mux.write_h264_sps_pps(1, 1) == srs_success);
        CHECK(video_payloads.size() == count && !mux.h264_sps_pps_sent_);
        if (pps_first) {
            mux.h264_sps_ = sps; mux.h264_sps_changed_ = true;
        } else {
            mux.h264_pps_ = pps; mux.h264_pps_changed_ = true;
        }
        CHECK(mux.write_h264_sps_pps(2, 2) == srs_success);
        CHECK(video_payloads.size() == count + 1 && mux.h264_sps_pps_sent_);
        CHECK(!mux.h264_sps_changed_ && !mux.h264_pps_changed_);
        const auto& sh = video_payloads.back();
        CHECK(sh.size() == 18 && uint8_t(sh[13]) == 0 && uint8_t(sh[14]) == pps.size());
        CHECK(sh.substr(sh.size() - pps.size()) == pps);
        CHECK(mux.write_h264_sps_pps(3, 3) == srs_success);
        CHECK(video_payloads.size() == count + 1);

        // Even inconsistent stale flags must not reach the serializer with missing data.
        for (int size : {0, 1, 3}) {
            mux.h264_sps_ = string(size, '\x67'); mux.h264_pps_ = pps;
            mux.h264_sps_changed_ = mux.h264_pps_changed_ = true;
            CHECK(mux.write_h264_sps_pps(4, 4) == srs_success);
            CHECK(video_payloads.size() == count + 1);
        }
        mux.h264_sps_ = sps; mux.h264_pps_.clear();
        CHECK(mux.write_h264_sps_pps(5, 5) == srs_success);
        CHECK(video_payloads.size() == count + 1);
    }
}
int main() {
    SrsGbSession session;
    reset_peer();
    {
        SrsGbMuxer mux(&session);
        CHECK(mux.sdk_ == NULL && mux.rtmp_receiver_ == NULL);
        CHECK(mux.connect() == srs_success);
        for (int batch = 0; batch < 5; ++batch) {
            int expected = controls_read + 2000;
            {
                lock_guard<mutex> lock(peer_mutex);
                pending_controls += 2000;
            }
            peer_changed.notify_all();
            for (int frame = 0; frame < 20; ++frame) {
                CHECK(mux.rtmp_write_packet(9, frame, payload(), 16) == srs_success);
            }
            wait_for([&] { return controls_read == expected && message_count == 0; });
        }
        CHECK(video_sent == 100);
        // An idle server leaves recv blocked; close must interrupt and join it.
        mux.h264_sps_pps_sent_ = true; mux.h264_sps_ = "old";
        mux.h264_sps_changed_ = mux.h264_pps_changed_ = true;
#ifdef SRS_H265
        mux.vps_sps_pps_sent_ = true; mux.h265_vps_ = "old";
#endif
        mux.close();
        mux.close();
        CHECK(!mux.sdk_ && !mux.rtmp_receiver_ && !mux.h264_sps_pps_sent_);
        CHECK(!mux.h264_sps_changed_ && !mux.h264_pps_changed_);
        CHECK(mux.h264_sps_.empty());
#ifdef SRS_H265
        CHECK(!mux.vps_sps_pps_sent_ && mux.h265_vps_.empty());
#endif
    }
    reset_peer();
    {
        SrsGbMuxer mux(&session);
        CHECK(mux.connect() == srs_success);
        {
            lock_guard<mutex> lock(peer_mutex);
            peer_eof = true;
        }
        peer_changed.notify_all();
        wait_for([] { return worker_count == 0; });
        CHECK(mux.rtmp_write_packet(9, 0, payload(), 16) == ERR_EOF);
        CHECK(!mux.sdk_ && !mux.rtmp_receiver_ && payload_count == 0);
        reset_peer();
        CHECK(mux.connect() == srs_success);
        fail_send = true;
        CHECK(mux.rtmp_write_packet(9, 0, payload(), 16) == ERR_SEND);
        fail_send = false;
        CHECK(!mux.sdk_ && !mux.rtmp_receiver_ && payload_count == 0);
        CHECK(mux.connect() == srs_success);
        CHECK(mux.rtmp_write_packet(0, 0, payload(), 16) == ERR_CREATE);
        // Destructor also joins a blocked receiver.
    }
    for (int failure : {ERR_CONNECT, ERR_PUBLISH, ERR_START}) {
        reset_peer();
        SrsGbMuxer mux(&session);
        fail_connect = failure == ERR_CONNECT;
        fail_publish = failure == ERR_PUBLISH;
        fail_start = failure == ERR_START;
        CHECK(mux.rtmp_write_packet(9, 0, payload(), 16) == failure);
        fail_connect = fail_publish = false;
        CHECK(!mux.sdk_ && !mux.rtmp_receiver_);
        CHECK(worker_count == 0 && sdk_count == 0 && payload_count == 0);
        CHECK(mux.connect() == srs_success);
    }
    reset_peer();
    check_response_chain();
    check_h264_recovery(session);
    CHECK(packet_count == 0);
    reset_peer();
    cout << "GB RTMP controls: 10000 inbound ACKs, 17000 Ping/10000 ACK response chain, "
            "nonrecursive flush, error retry, H264 partial-header/reconnect recovery, "
            "publishing, EOF/reconnect, startup/send failures and blocked shutdown PASS\n";
}
'''


def function(source, signature):
    start = source.index(signature)
    end = source.index("\n}\n", start) + 3
    return source[start:end] + "\n"


def check_patch_versions(project, temporary):
    """Exercise a real V1 build copy, V2 convergence and fail-before-write guards."""
    names = ["app/srs_app_gb28181.cpp", "app/srs_app_gb28181.hpp",
             "protocol/srs_protocol_rtmp_conn.cpp", "protocol/srs_protocol_rtmp_conn.hpp",
             "protocol/srs_protocol_rtmp_stack.cpp", "protocol/srs_protocol_rtmp_stack.hpp"]
    original = project / "3rd/srs-6.0-r0/trunk"
    patch = project / "cmake/patch_srs_gb28181_rtmp_control.cmake"

    def copy_source(source, destination):
        for name in names:
            path = destination / "src" / name
            path.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source / "src" / name, path)

    def hashes(root):
        return {name: hashlib.sha256((root / "src" / name).read_bytes()).hexdigest()
                for name in names}

    def apply(root, script=patch, success=True):
        result = subprocess.run(["cmake", f"-DSRS_SOURCE_DIR={root}", "-P", str(script)],
                                text=True, capture_output=True)
        if (result.returncode == 0) != success:
            raise AssertionError(f"Unexpected patch result for {root.name}: "
                                 f"{result.stdout}{result.stderr}")

    # The implementation deliberately retains its original V1 stage. Execute
    # that stage on pristine vendor files to model an actual incremental build,
    # rather than changing markers on a V2 source that already has its fixes.
    v1_script = Path(temporary) / "legacy-control-v1.cmake"
    v1_stage = patch.read_text().split(
        "# Upgrade both a fresh V1-shaped source and an existing complete V1 build copy.", 1)
    if len(v1_stage) != 2:
        raise AssertionError("Cannot identify the retained V1 patch stage")
    v1_script.write_text(v1_stage[0] + '''
foreach(part IN LISTS parts)
    file(WRITE "${${part}_file}" "${${part}}")
endforeach()
''')
    v1 = Path(temporary) / "legacy-source"
    fresh = Path(temporary) / "fresh-source"
    copy_source(original, v1)
    apply(v1, v1_script)
    for name in names:
        content = (v1 / "src" / name).read_text()
        if "COSMO_GB_RTMP_CONTROL_V1" not in content or "COSMO_GB_RTMP_CONTROL_V2" in content:
            raise AssertionError("Legacy fixture is not a complete V1 source")
    legacy_snapshot = Path(temporary) / "legacy-snapshot"
    copy_source(v1, legacy_snapshot)
    copy_source(original, fresh)
    apply(fresh)
    expected = hashes(fresh)
    for attempt in range(3):
        apply(v1)
        if hashes(v1) != expected:
            raise AssertionError(f"V1 upgrade/reapply {attempt} differs from fresh V2")

    cases = ["unexpected-anchor", "partial-v1", "partial-v2", "unknown-version",
             "unknown-complete-v2", "mixed-versions", "broken-v1-upgrade-anchor"]
    for case in cases:
        root = Path(temporary) / case
        source = fresh if case in {"unknown-complete-v2", "mixed-versions"} else original
        if case == "broken-v1-upgrade-anchor":
            source = legacy_snapshot
        copy_source(source, root)
        target = root / "src/protocol/srs_protocol_rtmp_conn.hpp"
        if case == "unexpected-anchor":
            target.write_text(target.read_text().replace("set_recv_timeout", "unexpected_timeout"))
        elif case == "mixed-versions":
            target.write_text(target.read_text().replace("COSMO_GB_RTMP_CONTROL_V2",
                                                        "COSMO_GB_RTMP_CONTROL_V1"))
        elif case == "broken-v1-upgrade-anchor":
            target = root / "src/app/srs_app_gb28181.cpp"
            target.write_text(target.read_text().replace(
                "if (!h264_sps_changed_ || !h264_pps_changed_)", "if (false)"))
        else:
            version = {"partial-v1": 1, "partial-v2": 2}.get(case, 99)
            with target.open("a") as output:
                output.write(f"\n// COSMO_GB_RTMP_CONTROL_V{version}\n")
        before = hashes(root)
        apply(root, success=False)
        if hashes(root) != before:
            raise AssertionError(f"Patch wrote source files before rejecting {case}")
    print("GB RTMP patch: fresh/V1 upgrade convergence, V2 idempotence, "
          "unknown/partial/mismatched-source no-write guards PASS")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("--compiler", help="Native C++ compiler path")
    parser.add_argument("--work-dir", type=Path, help="Retain generated fixtures and binaries here")
    args = parser.parse_args()
    source = args.source.read_text()
    protocol_dir = args.source.resolve().parents[1] / "protocol"
    client = (protocol_dir / "srs_protocol_rtmp_conn.cpp").read_text()
    protocol = (protocol_dir / "srs_protocol_rtmp_stack.cpp").read_text()
    if "flushing_responses_ = false;" not in function(protocol, "SrsProtocol::SrsProtocol("):
        raise AssertionError("The real protocol constructor must initialize its flush guard")
    if "bool flushing_responses_;" not in (protocol_dir / "srs_protocol_rtmp_stack.hpp").read_text():
        raise AssertionError("The real protocol must declare its flush guard")
    methods = function(client, "void SrsBasicRtmpClient::set_auto_response(")
    methods += function(protocol, "void SrsRtmpClient::set_auto_response(")
    for signature in ["void SrsProtocol::set_auto_response(",
                      "srs_error_t SrsProtocol::manual_response_flush(",
                      "srs_error_t SrsProtocol::do_send_and_free_packet(",
                      "srs_error_t SrsProtocol::send_and_free_message(",
                      "srs_error_t SrsProtocol::send_and_free_messages(",
                      "srs_error_t SrsProtocol::on_recv_message(",
                      "srs_error_t SrsProtocol::response_acknowledgement_message(",
                      "srs_error_t SrsProtocol::response_ping_message("]:
        methods += function(protocol, signature)
    for signature in ["SrsGbMuxer::SrsGbMuxer(", "SrsGbMuxer::~SrsGbMuxer(",
                      "srs_error_t SrsGbMuxer::cycle(", "srs_error_t SrsGbMuxer::connect(",
                      "void SrsGbMuxer::close(", "srs_error_t SrsGbMuxer::rtmp_write_packet(",
                      "srs_error_t SrsGbMuxer::write_h264_sps_pps("]:
        methods += function(source, signature)
    compiler = args.compiler or shutil.which("c++") or shutil.which("g++") or shutil.which("clang++")
    if not compiler:
        raise SystemExit("A native C++ compiler is required")
    project = Path(__file__).resolve().parents[1]
    raw_avc = (project / "3rd/srs-6.0-r0/trunk/src/protocol/srs_protocol_raw_avc.cpp").read_text()
    methods += function(raw_avc, "srs_error_t SrsRawH264Stream::mux_sequence_header(")
    if args.work_dir:
        args.work_dir.mkdir(parents=True, exist_ok=True)
    workspace = (contextlib.nullcontext(str(args.work_dir)) if args.work_dir else
                 tempfile.TemporaryDirectory(prefix="cosmo-gb-rtmp-control-"))
    with workspace as temporary:
        check_patch_versions(project, temporary)
        for hevc in (False, True):
            binary = Path(temporary) / ("control-hevc" if hevc else "control-avc")
            fixture = binary.with_suffix(".cpp")
            fixture.write_text(PREFIX + methods + SUFFIX)
            command = [compiler, "-std=c++11", "-pthread", "-D_GLIBCXX_DEBUG",
                       str(fixture), "-o", str(binary)]
            if hevc:
                command.insert(1, "-DSRS_H265")
            subprocess.run(command, check=True)
            subprocess.run([str(binary)], check=True, timeout=30)


if __name__ == "__main__":
    main()
