#include <arpa/inet.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <future>
#include <memory>
#include <stdexcept>
#include <thread>

#include "catch_amalgamated.hpp"
#include "media/VideoDemuxer.h"
#include "service/onvif/impl/OnvifProtocol.h"
#include "util/ProcessShutdown.h"

namespace {
// Generate public synthetic media locally; no camera recordings or credentials.
class DemuxFixture {
public:
    DemuxFixture() {
        char name[]  = "/tmp/cosmo-demux-lifecycle-XXXXXX";
        const int fd = mkstemp(name);
        if (fd < 0)
            throw std::runtime_error("cannot create demux fixture");
        close(fd);
        path              = name;
        const auto* codec = avcodec_find_encoder(AV_CODEC_ID_MJPEG);
        auto* context     = avcodec_alloc_context3(codec);
        auto* frame       = av_frame_alloc();
        auto* packet      = av_packet_alloc();
        if (!codec || !context || !frame || !packet) {
            av_packet_free(&packet);
            av_frame_free(&frame);
            avcodec_free_context(&context);
            std::filesystem::remove(path);
            throw std::runtime_error("MJPEG fixture encoder unavailable");
        }
        context->width     = 16;
        context->height    = 16;
        context->pix_fmt   = AV_PIX_FMT_YUVJ420P;
        context->time_base = {1, 25};
        frame->width       = context->width;
        frame->height      = context->height;
        frame->format      = context->pix_fmt;
        bool ok = avcodec_open2(context, codec, nullptr) == 0 && av_frame_get_buffer(frame, 32) == 0;
        if (ok) {
            for (int plane = 0; plane < 3; ++plane) {
                const int height = plane == 0 ? 16 : 8;
                for (int row = 0; row < height; ++row)
                    memset(frame->data[plane] + row * frame->linesize[plane], 128, plane == 0 ? 16 : 8);
            }
            ok = avcodec_send_frame(context, frame) == 0 && avcodec_receive_packet(context, packet) == 0;
        }
        if (ok) {
            std::ofstream output(path, std::ios::binary);
            for (int i = 0; i < 3; ++i)
                output.write(reinterpret_cast<const char*>(packet->data), packet->size);
            ok = output.good();
        }
        av_packet_free(&packet);
        av_frame_free(&frame);
        avcodec_free_context(&context);
        if (!ok) {
            std::filesystem::remove(path);
            throw std::runtime_error("cannot encode demux fixture");
        }
    }
    ~DemuxFixture() {
        std::error_code error;
        std::filesystem::remove(path, error);
    }
    std::string path;
};
}  // namespace

namespace {
class StalledRtspPeer {
public:
    StalledRtspPeer() : released_(release_.get_future().share()) {
        listener_ = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
        sockaddr_in address{};
        address.sin_family      = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        socklen_t size          = sizeof(address);
        if (listener_ < 0 || bind(listener_, reinterpret_cast<sockaddr*>(&address), size) != 0 ||
            listen(listener_, 1) != 0 ||
            getsockname(listener_, reinterpret_cast<sockaddr*>(&address), &size) != 0) {
            if (listener_ >= 0)
                close(listener_);
            throw std::runtime_error("cannot create loopback RTSP peer");
        }
        url     = "rtsp://127.0.0.1:" + std::to_string(ntohs(address.sin_port)) + "/fixture";
        worker_ = std::thread([this]() {
            pollfd descriptor{listener_, POLLIN, 0};
            while (!stopping_.load()) {
                if (poll(&descriptor, 1, 20) <= 0)
                    continue;
                int client = accept(listener_, nullptr, nullptr);
                if (client < 0)
                    break;
                connected_.set_value();
                // Deliberately never answer OPTIONS. RequestStop must abort
                // avformat_open_input without waiting for its 300s timeout.
                released_.wait();
                shutdown(client, SHUT_RDWR);
                close(client);
                break;
            }
        });
    }
    ~StalledRtspPeer() {
        Stop();
    }
    void Stop() {
        if (!stopping_.exchange(true))
            release_.set_value();
        if (worker_.joinable())
            worker_.join();
        if (listener_ >= 0) {
            close(listener_);
            listener_ = -1;
        }
    }
    bool WaitForConnection() {
        return connected_.get_future().wait_for(std::chrono::seconds(2)) == std::future_status::ready;
    }
    std::string url;

private:
    int listener_ = -1;
    std::atomic<bool> stopping_{false};
    std::promise<void> connected_, release_;
    std::shared_future<void> released_;
    std::thread worker_;
};

struct ShutdownReset {
    ~ShutdownReset() {
        cosmo::util::ProcessShutdown::ResetForStartup();
    }
};
}  // namespace

TEST_CASE("Reopening a probed input starts a fresh FFmpeg session", "[media][demux-lifecycle]") {
    DemuxFixture fixture;
    cosmo::media::VideoDemuxer demuxer;
    demuxer.SetFile(fixture.path);
    int64_t previous_stream = -1;
    for (int attempt = 0; attempt < 100; ++attempt) {
        // No CloseStream here: this is the same fresh-open path used by live
        // reconnect, where local-file seek reuse must NOT be selected.
        REQUIRE(demuxer.OpenStream(false) == cosmo::util::ErrorEnum::Success);
        REQUIRE(demuxer.FindStream(false) == cosmo::util::ErrorEnum::Success);
        auto packet = std::make_shared<cosmo::media::VideoPacket>();
        REQUIRE(demuxer.Demux(packet) == cosmo::media::ReadFrameStatus::Success);
        CHECK(packet->stream_idx > previous_stream);
        previous_stream = packet->stream_idx;
        bool ended      = false;
        for (int read = 0; read < 16 && !ended; ++read)
            ended = demuxer.Demux(packet) == cosmo::media::ReadFrameStatus::StreamEnd;
        REQUIRE(ended);
    }
}

TEST_CASE("Local file repeat keeps seek playback available", "[media][demux-lifecycle]") {
    DemuxFixture fixture;
    cosmo::media::VideoDemuxer demuxer;
    demuxer.SetFile(fixture.path);
    REQUIRE(demuxer.OpenStream() == cosmo::util::ErrorEnum::Success);
    REQUIRE(demuxer.FindStream() == cosmo::util::ErrorEnum::Success);
    for (int attempt = 0; attempt < 3; ++attempt) {
        auto packet = std::make_shared<cosmo::media::VideoPacket>();
        REQUIRE(demuxer.Demux(packet) == cosmo::media::ReadFrameStatus::Success);
        demuxer.CloseStream(true);
        REQUIRE(demuxer.OpenStream(true) == cosmo::util::ErrorEnum::Success);
        REQUIRE(demuxer.FindStream(true) == cosmo::util::ErrorEnum::Success);
    }
    demuxer.CloseStream();
    demuxer.CloseStream();
    CHECK(demuxer.FindStream() != cosmo::util::ErrorEnum::Success);
}

TEST_CASE("Failed reopen releases metadata and permits recovery", "[media][demux-lifecycle]") {
    DemuxFixture fixture;
    cosmo::media::VideoDemuxer demuxer;
    demuxer.SetFile(fixture.path);
    REQUIRE(demuxer.OpenStream() == cosmo::util::ErrorEnum::Success);
    REQUIRE(demuxer.FindStream() == cosmo::util::ErrorEnum::Success);
    // A repeated probe is idempotent; FFmpeg must not probe the same session again.
    REQUIRE(demuxer.FindStream() == cosmo::util::ErrorEnum::Success);
    CHECK(demuxer.GetWidth() == 16);
    CHECK_FALSE(demuxer.IsLiveStream());
    demuxer.SetFile(fixture.path + ".missing");
    CHECK(demuxer.OpenStream(true) != cosmo::util::ErrorEnum::Success);
    CHECK(demuxer.FindStream(true) != cosmo::util::ErrorEnum::Success);
    CHECK(demuxer.GetWidth() == 0);
    CHECK(demuxer.GetCodecExtradata().empty());
    CHECK(demuxer.Demux(std::make_shared<cosmo::media::VideoPacket>()) ==
          cosmo::media::ReadFrameStatus::StreamNotOpen);
    demuxer.SetFile(fixture.path);
    REQUIRE(demuxer.OpenStream(true) == cosmo::util::ErrorEnum::Success);
    REQUIRE(demuxer.FindStream(true) == cosmo::util::ErrorEnum::Success);
    CHECK(demuxer.GetWidth() == 16);
}

TEST_CASE("Blocked RTSP open responds to channel and process cancellation", "[media][demux-lifecycle]") {
    for (bool process_stop : {false, true}) {
        ShutdownReset reset;
        StalledRtspPeer peer;
        cosmo::media::VideoDemuxer demuxer;
        demuxer.SetFile(peer.url);
        auto opened = std::async(std::launch::async, [&]() { return demuxer.OpenStream(false, 300); });
        const bool connected = peer.WaitForConnection();
        if (process_stop)
            cosmo::util::ProcessShutdown::Request();
        else
            demuxer.RequestStop();
        const bool cancelled = opened.wait_for(std::chrono::seconds(2)) == std::future_status::ready;
        peer.Stop();  // Always unblock I/O before joining, even on a regression.
        const auto result = opened.get();
        CHECK(connected);
        CHECK(cancelled);
        CHECK(result != cosmo::util::ErrorEnum::Success);
        CHECK(demuxer.OpenStream() != cosmo::util::ErrorEnum::Success);
        CHECK(demuxer.FindStream() != cosmo::util::ErrorEnum::Success);
    }
}

TEST_CASE("Cancellation is sticky until an explicit new worker lifecycle", "[media][demux-lifecycle]") {
    ShutdownReset reset;
    DemuxFixture fixture;
    cosmo::media::VideoDemuxer demuxer;
    demuxer.SetFile(fixture.path);
    REQUIRE(demuxer.OpenStream() == cosmo::util::ErrorEnum::Success);
    demuxer.RequestStop();
    CHECK(demuxer.FindStream() != cosmo::util::ErrorEnum::Success);
    CHECK(demuxer.GetCodecExtradata().empty());
    CHECK(demuxer.OpenStream() != cosmo::util::ErrorEnum::Success);
    demuxer.ResetCancellation();
    REQUIRE(demuxer.OpenStream(true) == cosmo::util::ErrorEnum::Success);
    REQUIRE(demuxer.FindStream(true) == cosmo::util::ErrorEnum::Success);
    demuxer.RequestStop();
    CHECK(demuxer.Demux(std::make_shared<cosmo::media::VideoPacket>()) ==
          cosmo::media::ReadFrameStatus::StreamNotOpen);
    demuxer.CloseStream(true);
    demuxer.ResetCancellation();
    cosmo::util::ProcessShutdown::Request();
    CHECK(demuxer.OpenStream(true) != cosmo::util::ErrorEnum::Success);
    CHECK(demuxer.FindStream(true) != cosmo::util::ErrorEnum::Success);
}

TEST_CASE("ONVIF RTSP metadata probing stops at its deadline", "[onvif][demux-lifecycle]") {
    StalledRtspPeer peer;
    auto probing         = std::async(std::launch::async, [&] {
        try {
            cosmo::service::onvif::ProbeStreamMetadata(
                peer.url, std::chrono::steady_clock::now() + std::chrono::milliseconds(300), nullptr);
            return std::string("unexpected_success");
        } catch (const std::exception& error) {
            return std::string(error.what());
        }
    });
    const bool connected = peer.WaitForConnection();
    const bool bounded   = probing.wait_for(std::chrono::seconds(2)) == std::future_status::ready;
    peer.Stop();
    CHECK(connected);
    CHECK(bounded);
    CHECK(probing.get() == "timeout");
}

TEST_CASE("ONVIF media probing observes cancellation before opening", "[onvif][demux-lifecycle]") {
    std::atomic<bool> running{false};
    CHECK_THROWS_WITH(cosmo::service::onvif::ProbeStreamMetadata(
                          "rtsp://127.0.0.1:1/unused",
                          std::chrono::steady_clock::now() + std::chrono::seconds(5), &running),
                      "timeout");
}
