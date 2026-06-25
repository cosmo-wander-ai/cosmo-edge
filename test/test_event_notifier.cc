#include <atomic>
#include <thread>

#include "catch_amalgamated.hpp"
#include "service/event/impl/EventNotifierImpl.h"

using namespace cosmo::service;

TEST_CASE("EventNotifierImpl: construction and destruction", "[EventNotifier]") {
    REQUIRE_NOTHROW([]() { EventNotifierImpl notifier; }());
}

TEST_CASE("EventNotifierImpl: SetEventPostQue and push without processor", "[EventNotifier]") {
    EventNotifierImpl notifier;
    cosmo::AsyncQueue<cosmo::CMsgOnEventsReq> eventQue("test_no_proc", 100);
    notifier.SetEventPostQue(eventQue);

    // Push without processor — should not crash
    cosmo::CMsgOnEventsReq req;
    REQUIRE_NOTHROW(notifier.EventPush(req));
}

TEST_CASE("EventNotifierImpl: WebSocket and Events", "[EventNotifier]") {
    EventNotifierImpl notifier;

    // NOTE: InitializeWebSocket and ShutdownWebSocket are not tested here because
    // uWebSockets' us_listen_socket_close is not thread-safe when called from a different thread,
    // which causes a deadlock in the test environment.

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
