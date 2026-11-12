#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <string>
#include <thread>

#include "catch_amalgamated.hpp"
#include "service/event/impl/EventNotifierImpl.h"

using namespace cosmo::service;

namespace {

int ReserveLoopbackPort() {
    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    REQUIRE(fd >= 0);

    sockaddr_in address{};
    address.sin_family      = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port        = htons(0);
    REQUIRE(bind(fd, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0);

    socklen_t address_size = sizeof(address);
    REQUIRE(getsockname(fd, reinterpret_cast<sockaddr*>(&address), &address_size) == 0);
    const int port = ntohs(address.sin_port);
    close(fd);
    return port;
}

std::string GetOrdinaryHttpResponse(int port) {
    for (int attempt = 0; attempt < 100; ++attempt) {
        const int fd = socket(AF_INET, SOCK_STREAM, 0);
        REQUIRE(fd >= 0);

        timeval timeout{};
        timeout.tv_sec  = 1;
        timeout.tv_usec = 0;
        REQUIRE(setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) == 0);

        sockaddr_in address{};
        address.sin_family      = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port        = htons(static_cast<uint16_t>(port));
        if (connect(fd, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0) {
            constexpr char request[] =
                "GET /health-probe HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n";
            const auto sent = send(fd, request, std::strlen(request), MSG_NOSIGNAL);
            REQUIRE(sent == static_cast<ssize_t>(std::strlen(request)));

            std::string response;
            char buffer[512];
            while (true) {
                const auto count = recv(fd, buffer, sizeof(buffer), 0);
                if (count <= 0)
                    break;
                response.append(buffer, static_cast<size_t>(count));
            }
            close(fd);
            return response;
        }
        close(fd);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return {};
}

}  // namespace

TEST_CASE("EventNotifierImpl: SetEventPostQue and push without processor", "[EventNotifier]") {
    EventNotifierImpl notifier;
    cosmo::AsyncQueue<cosmo::CMsgOnEventsReq> eventQue("test_no_proc", 100);
    notifier.SetEventPostQue(eventQue);

    // Push without processor — should not crash
    cosmo::CMsgOnEventsReq req;
    REQUIRE_NOTHROW(notifier.EventPush(req));
}

TEST_CASE("EventNotifierImpl: clearing event queue prevents later delivery", "[EventNotifier]") {
    EventNotifierImpl notifier;
    cosmo::AsyncQueue<cosmo::CMsgOnEventsReq> eventQue("test_clear", 100);
    std::atomic<int> processCount{0};
    eventQue.SetProcessor(
        [&processCount](cosmo::CMsgOnEventsReq&&) { processCount.fetch_add(1, std::memory_order_relaxed); });

    notifier.SetEventPostQue(eventQue);
    notifier.ClearEventPostQue(eventQue);
    cosmo::CMsgOnEventsReq req;
    notifier.EventPush(req);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    REQUIRE(processCount.load(std::memory_order_relaxed) == 0);
}

TEST_CASE("EventNotifierImpl: WebSocket and Events", "[EventNotifier]") {
    EventNotifierImpl notifier;

    SECTION("Event queues concurrent push") {
        cosmo::AsyncQueue<cosmo::CMsgOnEventsReq> eventQue("test_que", 1000);
        notifier.SetEventPostQue(eventQue);

        std::atomic<int> processCount{0};
        eventQue.SetProcessor([&processCount](cosmo::CMsgOnEventsReq&& req) {
            processCount.fetch_add(1, std::memory_order_relaxed);
        });

        std::thread t1([&]() {
            for (int i = 0; i < 50; ++i) {
                cosmo::CMsgOnEventsReq req;
                notifier.EventPush(req);
            }
        });

        std::thread t2([&]() {
            for (int i = 0; i < 50; ++i) {
                cosmo::CMsgOnEventsReq req;
                notifier.EventPush(req);
            }
        });

        t1.join();
        t2.join();

        // Wait for queue to process all elements
        for (int i = 0; i < 50 && processCount.load() < 100; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        REQUIRE(processCount.load() == 100);
    }
}

TEST_CASE("EventNotifierImpl: WebSocket shutdown is deferred to server loop", "[EventNotifier]") {
    EventNotifierImpl notifier;

    REQUIRE(notifier.InitializeWebSocket("127.0.0.1", 0));
    REQUIRE_NOTHROW(notifier.ShutdownWebSocket());
}

TEST_CASE("EventNotifierImpl: ordinary HTTP probe is rejected without aborting", "[EventNotifier]") {
    EventNotifierImpl notifier;
    const int port = ReserveLoopbackPort();

    REQUIRE(notifier.InitializeWebSocket("127.0.0.1", port));
    const auto response = GetOrdinaryHttpResponse(port);

    REQUIRE(response.find("404 Not Found") != std::string::npos);
    REQUIRE(response.find("Not Found") != std::string::npos);
    REQUIRE_NOTHROW(notifier.ShutdownWebSocket());
}
