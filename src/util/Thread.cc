// Named, joinable worker thread with registry diagnostics.

#include "util/Thread.h"

#include <chrono>

#include "util/Log.h"
#include "util/ThreadRegistry.h"
#include "util/TimingConstants.h"
#include "util/UuidUtil.h"

namespace cosmo::util {

namespace chrono = std::chrono;
using Clock      = chrono::steady_clock;

Thread::Thread(std::string name) : uuid_(GenerateUUID()), name_(std::move(name)) {}

Thread::~Thread() {
    stop();
}

bool Thread::start() {
    std::lock_guard<std::mutex> lock(lifecycle_mtx_);
    if (thread_.joinable()) {
        LOG_ERRO("Thread {} Start rejected: previous thread still joinable", name_);
        return false;
    }
    RegisterThread(uuid_, name_);
    LOG_INFO("Thread {} Start", name_);
    // Capture copies so lambda stays safe if Thread object is destroyed
    // before run() returns (guarded by stop() in dtor).
    auto name = name_;
    auto uuid = uuid_;
    thread_   = std::thread([this, name, uuid] {
        LOG_INFO("Thread {} Run", name);
        run();
        LOG_INFO("Thread {} Stopped", name);
        UnregisterThread(uuid);
    });
    return true;
}

void Thread::stop() {
    std::lock_guard<std::mutex> lock(lifecycle_mtx_);
    if (!thread_.joinable())
        return;
    if (thread_.get_id() == std::this_thread::get_id()) {
        LOG_ERRO("Thread {} Stop rejected: cannot join current thread", name_);
        return;
    }
    LOG_INFO("Thread {} Wait To Stop", name_);
    auto start_time = Clock::now();
    thread_.join();
    auto cost    = Clock::now() - start_time;
    auto cost_ms = chrono::duration_cast<chrono::milliseconds>(cost).count();
    if (cost > timing::kThreadSlowStopThreshold) {
        LOG_WARN("Thread {} Stop Cost {} ms (SLOW!)", name_, cost_ms);
    } else {
        LOG_INFO("Thread {} Stop Cost {} ms", name_, cost_ms);
    }
}

const std::string& Thread::Name() const {
    return name_;
}

}  // namespace cosmo::util
