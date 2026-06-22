// NtpHelpers.h — Shared NTP protocol helpers for TimeServiceImpl and TimeServiceConfig.
// Extracted from duplicated anonymous namespaces (DEBT-013).
//
// NOTE: This header is intentionally designed for internal use within the
//       TimeService implementation files only. All functions and types are
//       in an anonymous namespace to preserve internal linkage.

#pragma once

#include <arpa/inet.h>
#include <sys/time.h>
#include <unistd.h>

#include <cstdint>
#include <cstring>
#include <fstream>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "platform/SystemTime.h"
#include "util/Exec.h"
#include "util/Log.h"

namespace cosmo::service {
namespace {

    // ---------------------------------------------------------------------------
    // Constants
    // ---------------------------------------------------------------------------

    constexpr uint32_t kJan1970       = 0x83aa7e80;
    constexpr double kFracPerMs       = 4.294967296E6;  // 2^32 / 1000
    constexpr int kNtpTimeoutMs       = 10000;          // send/recv timeout
    constexpr int kMinSyncIntervalSec = 60;

    // ---------------------------------------------------------------------------
    // NTP protocol structures
    // ---------------------------------------------------------------------------

    struct NtpTime {
        uint32_t seconds{0};
        uint32_t fraction{0};
    };

    struct NtpPacket {
        int8_t li_vn_mode{0};
        uint8_t stratum{0};
        char poll{0};
        char precision{0};
        int root_delay{0};
        uint32_t root_dispersion{0};
        int reference_id{0};
        NtpTime reference_ts;
        NtpTime originate_ts;
        NtpTime receive_ts;
        NtpTime transmit_ts;
    };

    // ---------------------------------------------------------------------------
    // Byte-order conversion
    // ---------------------------------------------------------------------------

    // Convert NTP packet fields from network to host byte order
    inline void NtohPacket(NtpPacket& pkt) {
        pkt.root_delay            = ntohl(pkt.root_delay);
        pkt.root_dispersion       = ntohl(pkt.root_dispersion);
        pkt.reference_id          = ntohl(pkt.reference_id);
        pkt.reference_ts.seconds  = ntohl(pkt.reference_ts.seconds);
        pkt.reference_ts.fraction = ntohl(pkt.reference_ts.fraction);
        pkt.originate_ts.seconds  = ntohl(pkt.originate_ts.seconds);
        pkt.originate_ts.fraction = ntohl(pkt.originate_ts.fraction);
        pkt.receive_ts.seconds    = ntohl(pkt.receive_ts.seconds);
        pkt.receive_ts.fraction   = ntohl(pkt.receive_ts.fraction);
        pkt.transmit_ts.seconds   = ntohl(pkt.transmit_ts.seconds);
        pkt.transmit_ts.fraction  = ntohl(pkt.transmit_ts.fraction);
    }

    // Convert NTP packet fields from host to network byte order
    inline void HtonPacket(NtpPacket& pkt) {
        pkt.root_delay            = htonl(pkt.root_delay);
        pkt.root_dispersion       = htonl(pkt.root_dispersion);
        pkt.reference_id          = htonl(pkt.reference_id);
        pkt.reference_ts.seconds  = htonl(pkt.reference_ts.seconds);
        pkt.reference_ts.fraction = htonl(pkt.reference_ts.fraction);
        pkt.originate_ts.seconds  = htonl(pkt.originate_ts.seconds);
        pkt.originate_ts.fraction = htonl(pkt.originate_ts.fraction);
        pkt.receive_ts.seconds    = htonl(pkt.receive_ts.seconds);
        pkt.receive_ts.fraction   = htonl(pkt.receive_ts.fraction);
        pkt.transmit_ts.seconds   = htonl(pkt.transmit_ts.seconds);
        pkt.transmit_ts.fraction  = htonl(pkt.transmit_ts.fraction);
    }

    // ---------------------------------------------------------------------------
    // Timestamp conversion utilities
    // ---------------------------------------------------------------------------

    // Convert NTP timestamp to timeval (microseconds since 1970-01-01)
    inline void TimestampToTimeval(const NtpTime& ts, long& out_sec, long& out_usec) {
        if (ts.seconds >= kJan1970) {
            out_sec  = static_cast<long>(ts.seconds - kJan1970);
            out_usec = static_cast<long>(ts.fraction / kFracPerMs * 1000.0);
        } else {
            out_sec  = 0;
            out_usec = 0;
        }
    }

    // Convert timeval to NTP timestamp
    inline void TimevalToTimestamp(NtpTime& ts, long sec, long usec) {
        ts.seconds  = static_cast<uint32_t>(sec + kJan1970);
        ts.fraction = static_cast<uint32_t>(usec / 1000.0 * kFracPerMs);
    }

    // Convert timeval to 100-nanosecond units
    inline int64_t TimevalToNs100(long sec, long usec) {
        return 10000000LL * sec + 10LL * usec;
    }

    // Convert NTP timestamp to 100-nanosecond units
    inline int64_t TimestampToNs100(const NtpTime& ts) {
        long sec = 0, usec = 0;
        TimestampToTimeval(ts, sec, usec);
        return TimevalToNs100(sec, usec);
    }

    // Get current system time in 100-nanosecond units since epoch
    inline int64_t GetTimevalueNs100() {
        struct timeval tv {};
        gettimeofday(&tv, nullptr);
        return 10000000LL * tv.tv_sec + 10LL * tv.tv_usec;
    }

    // ---------------------------------------------------------------------------
    // NTP packet initialization
    // ---------------------------------------------------------------------------

    // Initialize NTP request packet (client mode, version 3)
    inline void InitPacket(NtpPacket& pkt) {
        pkt                        = NtpPacket{};
        constexpr uint8_t kLeap    = 0;
        constexpr uint8_t kVersion = 3;
        constexpr uint8_t kMode    = 3;  // client
        pkt.li_vn_mode             = static_cast<int8_t>((kLeap << 6) | (kVersion << 3) | kMode);
        pkt.poll                   = 4;
        pkt.precision              = static_cast<char>((-6) & 0xFF);
        pkt.root_delay             = (1 << 16);
        pkt.root_dispersion        = (1 << 16);
    }

    // ---------------------------------------------------------------------------
    // Socket helpers
    // ---------------------------------------------------------------------------

    // Set socket send/recv timeout
    inline void SetSocketTimeout(int sockfd, int timeout_ms) {
        struct timeval tv {};
        tv.tv_sec  = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
        setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    }

    // Resolve hostname to IP via shell ping (non-blocking, avoids gethostbyname thread issues)
    inline std::string ResolveDomain(const std::string& host) {
        if (host.empty()) {
            LOG_ERRO("{}", "host is empty");
            return "";
        }
        // Already an IP address
        if (inet_addr(host.c_str()) != INADDR_NONE) {
            return host;
        }

        auto ip = cosmo::util::PingResolveIp(host);
        if (ip.empty()) {
            LOG_ERRO("PingResolveIp failed for host: [{}]", host);
        }
        return ip;
    }

    // Connect UDP socket to NTP server, returns sockfd or -1
    inline int ConnectNtpServer(const std::string& host, int port) {
        LOG_INFO("NTP connecting to host=[{}] port=[{}]", host, port);

        std::string ip = ResolveDomain(host);
        if (ip.empty()) {
            LOG_ERRO("Cannot resolve host: {}", host);
            return -1;
        }

        int sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (sockfd < 0) {
            LOG_ERRO("{}", "socket() failed");
            return -1;
        }
        SetSocketTimeout(sockfd, kNtpTimeoutMs);

        struct sockaddr_in src {};
        src.sin_family      = AF_INET;
        src.sin_port        = htons(0);
        src.sin_addr.s_addr = htonl(INADDR_ANY);

        if (bind(sockfd, reinterpret_cast<struct sockaddr*>(&src), sizeof(src)) < 0) {
            close(sockfd);
            LOG_ERRO("{}", "bind() failed");
            return -1;
        }

        struct sockaddr_in dst {};
        dst.sin_family      = AF_INET;
        dst.sin_port        = htons(port);
        dst.sin_addr.s_addr = inet_addr(ip.c_str());

        if (connect(sockfd, reinterpret_cast<struct sockaddr*>(&dst), sizeof(dst)) < 0) {
            close(sockfd);
            LOG_ERRO("connect() failed, host: [{}]", host);
            return -1;
        }

        LOG_INFO("NTP server connected, sockfd={}", sockfd);
        return sockfd;
    }

    // ---------------------------------------------------------------------------
    // NTP time application
    // ---------------------------------------------------------------------------

    // Set system time from 100ns-unit timestamp (skip if diff < 1s)
    inline void ApplyNtpTime(int64_t timestamp_ns100) {
        int64_t timestamp_ms = timestamp_ns100 / 10000;

        struct timeval old_tv {};
        gettimeofday(&old_tv, nullptr);
        int64_t old_ms = 1000LL * old_tv.tv_sec + old_tv.tv_usec / 1000;

        int64_t diff_ms = (timestamp_ms > old_ms) ? (timestamp_ms - old_ms) : (old_ms - timestamp_ms);
        if (diff_ms < 1000) {
            return;  // difference too small, skip
        }

        auto result = cosmo::platform::SetSystemTime(timestamp_ms);
        if (result != cosmo::util::ErrorEnum::Success) {
            LOG_WARN("NTP set time failed: [{}] -> [{}] diff:{}ms code:{}", old_ms, timestamp_ms, diff_ms,
                     static_cast<int>(result));
            return;
        }
        LOG_INFO("NTP set time: [{}] -> [{}] diff:{}ms", old_ms, timestamp_ms, diff_ms);
    }

    // ---------------------------------------------------------------------------
    // JSON read/write helpers
    // ---------------------------------------------------------------------------

    template <typename T>
    bool WriteJson(const T& t, const std::string& fileName) {
        std::ofstream ofile(fileName);
        if (!ofile.is_open()) {
            LOG_ERRO("open file {} failed.", fileName);
            return false;
        }
        auto jsonStr = nlohmann::json(t).dump(2);
        ofile.write(jsonStr.data(), jsonStr.size());
        ofile.close();
        return true;
    }

    template <typename T>
    bool ReadJson(T& t, const std::string& fileName) {
        try {
            std::ifstream infile(fileName);
            if (!infile.is_open()) {
                LOG_WARN("ReadJson: cannot open {}", fileName);
                return false;
            }
            auto doc = nlohmann::json::parse(infile);
            t        = doc.get<T>();
            return true;
        } catch (const std::exception& e) {
            LOG_ERRO("load json failed: {}", e.what());
            return false;
        }
    }

    // ---------------------------------------------------------------------------
    // Timezone parsing
    // ---------------------------------------------------------------------------

    // Parses "+08:00" or "-05:30" format timezone string
    inline bool ParseTimeZoneString(const std::string& tz, uint64_t& is_west, uint64_t& offset) {
        if (tz.length() != 6 || (tz[0] != '+' && tz[0] != '-') || tz[3] != ':') {
            return false;
        }
        int h = 0, m = 0;
        if (sscanf(tz.c_str() + 1, "%d:%d", &h, &m) != 2) {
            return false;
        }
        is_west = (tz[0] == '-') ? 1 : 0;
        offset  = h * 100 + m;
        return true;
    }

}  // namespace
}  // namespace cosmo::service
