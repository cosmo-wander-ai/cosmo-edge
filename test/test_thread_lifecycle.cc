#include "catch_amalgamated.hpp"

#include <atomic>
#include <chrono>
#include <thread>

#include "util/Thread.h"

namespace {

class TestWorkerThread : public cosmo::util::Thread {
public:
    TestWorkerThread() : Thread("TestWorkerThread") {}

    void RequestStop() {
        keep_running_.store(false);
    }

    int RunCount() const {
        return run_count_.load();
    }

private:
    void run() override {
        run_count_.fetch_add(1);
        while (keep_running_.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    std::atomic<bool> keep_running_{true};
    std::atomic<int> run_count_{0};
};

}  // namespace

TEST_CASE("Thread lifecycle: repeated start is rejected while joinable", "[thread]") {
    TestWorkerThread worker;

    REQUIRE(worker.start());
    REQUIRE_FALSE(worker.start());

    worker.RequestStop();
    worker.stop();
    REQUIRE(worker.RunCount() == 1);
}
