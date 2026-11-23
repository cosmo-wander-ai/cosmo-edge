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

Thread::Thread(std::string name) : registry_uuid_(GenerateUUID()), thread_name_(std::move(name)) {}

Thread::~Thread() {
    stop();
}

bool Thread::start() {
    std::lock_guard<std::mutex> lock(thread_lifecycle_mtx_);
    if (thread_.joinable()) {
        LOG_ERRO("Thread {} Start rejected: previous thread still joinable", thread_name_);
        return false;
    }
    RegisterThread(registry_uuid_, thread_name_);
    LOG_INFO("Thread {} Start", thread_name_);
    // Capture copies so lambda stays safe if Thread object is destroyed
    // before run() returns (guarded by stop() in dtor).
    auto name = thread_name_;
    auto uuid = registry_uuid_;
    thread_   = std::thread([this, name, uuid] {
        LOG_INFO("Thread {} Run", name);
        run();
        LOG_INFO("Thread {} Stopped", name);
        UnregisterThread(uuid);
    });
    return true;
}

void Thread::stop() {
    std::lock_guard<std::mutex> lock(thread_lifecycle_mtx_);
    if (!thread_.joinable())
        return;
    if (thread_.get_id() == std::this_thread::get_id()) {
        LOG_ERRO("Thread {} Stop rejected: cannot join current thread", thread_name_);
        return;
    }
    LOG_INFO("Thread {} Wait To Stop", thread_name_);
    auto start_time = Clock::now();
    thread_.join();
    auto cost    = Clock::now() - start_time;
    auto cost_ms = chrono::duration_cast<chrono::milliseconds>(cost).count();
    if (cost > timing::kThreadSlowStopThreshold) {
        LOG_WARN("Thread {} Stop Cost {} ms (SLOW!)", thread_name_, cost_ms);
    } else {
        LOG_INFO("Thread {} Stop Cost {} ms", thread_name_, cost_ms);
    }
}

const std::string& Thread::Name() const {
    return thread_name_;
}

}  // namespace cosmo::util
