// application — application implementation.

#include "app/application.h"

#include <malloc.h>
#include <pthread.h>

#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <memory>
#include <stdexcept>
#include <system_error>
#include <thread>

#include "app/AppConstants.h"
#include "app/app_init.h"
#include "mem/DeviceContext.h"
#include "mem/IDeviceContext.h"
#include "service/detail/ServiceRegistry.h"
#include "service/network/IHttpLifecycle.h"
#include "util/Log.h"
#include "util/PathUtil.h"
#include "util/ProcessShutdown.h"
#include "util/Version.h"

namespace cosmo::app {

namespace {

    class ShutdownSignalMonitor {
    public:
        ShutdownSignalMonitor() {
            sigemptyset(&signal_set_);
            sigaddset(&signal_set_, SIGTERM);
            sigaddset(&signal_set_, SIGINT);
            sigaddset(&signal_set_, SIGUSR1);
            const int result = pthread_sigmask(SIG_BLOCK, &signal_set_, nullptr);
            if (result != 0) {
                throw std::system_error(result, std::generic_category(), "cannot block shutdown signals");
            }
        }

        ~ShutdownSignalMonitor() noexcept {
            Stop();
        }

        ShutdownSignalMonitor(const ShutdownSignalMonitor&)            = delete;
        ShutdownSignalMonitor& operator=(const ShutdownSignalMonitor&) = delete;

        void Start(std::function<void()> shutdown_callback) {
            if (wait_thread_.joinable()) {
                throw std::logic_error("shutdown signal monitor already started");
            }
            wait_thread_ = std::thread([this, callback = std::move(shutdown_callback)]() {
                while (!is_stopping_.load(std::memory_order_acquire)) {
                    int signal_number = 0;
                    const int result  = sigwait(&signal_set_, &signal_number);
                    if (result != 0) {
                        LOG_ERRO("sigwait failed: {}", result);
                        InvokeShutdownCallback(callback);
                        return;
                    }
                    if (is_stopping_.load(std::memory_order_acquire)) {
                        return;
                    }
                    if (signal_number == SIGTERM || signal_number == SIGINT) {
                        LOG_INFO("Received shutdown signal {}, requesting HTTP loop stop", signal_number);
                        InvokeShutdownCallback(callback);
                        return;
                    }
                }
            });
        }

        void Stop() noexcept {
            is_stopping_.store(true, std::memory_order_release);
            if (wait_thread_.joinable()) {
                const int result = pthread_kill(wait_thread_.native_handle(), SIGUSR1);
                if (result != 0 && result != ESRCH) {
                    LOG_ERRO("cannot wake shutdown signal monitor: {}", result);
                }
                wait_thread_.join();
            }
        }

    private:
        static void InvokeShutdownCallback(const std::function<void()>& callback) noexcept {
            try {
                callback();
            } catch (const std::exception& ex) {
                LOG_ERRO("cannot request graceful shutdown: {}", ex.what());
            } catch (...) {
                LOG_ERRO("{}", "cannot request graceful shutdown: unknown error");
            }
        }

        sigset_t signal_set_{};
        std::atomic<bool> is_stopping_{false};
        std::thread wait_thread_;
    };

}  // namespace

Application::Application(std::string name) : app_name_(std::move(name)) {}

namespace {
    // Configure glibc malloc to prevent per-thread arena fragmentation under
    // concurrent video inference. Pooled inference threads alloc/free many
    // multi-MB frame buffers; ptmalloc2 fragments each thread's arena and never
    // returns it to the OS (malloc_trim only handles the main arena). Under
    // --jobs N benchmarks this makes RSS climb monotonically and OOM the box
    // even though every allocation is correctly freed (verified: deletes
    // succeed, pool recycle succeeds, no orphaned objects). Limiting arenas +
    // aggressive trim/mmap returns freed memory to the OS promptly.
    // Each setting honors an explicit MALLOC_* env override so operators can
    // tune per-deployment (e.g. raise MALLOC_ARENA_MAX to trade RSS for
    // throughput under high concurrency).
    void ConfigureAllocator() {
        auto apply = [](int param, const char* env_name, int default_val) {
            const char* env = std::getenv(env_name);
            int val         = env ? std::atoi(env) : default_val;
            if (val > 0) {
                mallopt(param, val);
            }
        };
        apply(M_ARENA_MAX, "MALLOC_ARENA_MAX", 2);
        apply(M_TRIM_THRESHOLD, "MALLOC_TRIM_THRESHOLD_", 65536);
        apply(M_MMAP_THRESHOLD, "MALLOC_MMAP_THRESHOLD_", 131072);
        apply(M_TOP_PAD, "MALLOC_TOP_PAD_", 0);
    }
}  // namespace

void LogInit(const std::string& name, const std::string& basedir, const std::string& version) {
    using namespace constants;

    // Log initialization
    auto log_path = std::filesystem::path(basedir) / "logs";
    cosmo::log::LogInit(name.c_str(), log_path.c_str(), version.c_str());
    cosmo::log::SetLogCleanLogMode(0);
    cosmo::log::SetMaxLogFileCount(kMaxLogFileCount);
    cosmo::log::SetMaxLogFileKeepDays(kMaxLogFileKeepDays);
#ifdef COSMO_DEV_MODE
    // Development mode: mirror all log messages to stdout for real-time debugging.
    // In production this is disabled to prevent log output from being captured by
    // systemd/journald and flooding /var/log/syslog (especially under high-concurrency
    // benchmark loads like scenario-bench). Application logs are still written to
    // the configured runtime log directory via glog regardless of this setting.
    cosmo::log::SetLogCallback(
        [](const char*, const char*, int, const char* data, size_t) { printf("%s\r\n", data); });
#endif
    cosmo::log::SetMaxLogFileSize(kMaxLogFileSizeMb);
    cosmo::log::SetLogLevel(LOG_LEVEL_INFO);
    cosmo::log::SetLogFileFlushInterval(kLogFlushIntervalSec);
    cosmo::log::SetPrintLevel(LOG_LEVEL_INFO);
}

int Application::run(const char* base_dir) {
    // DeviceContext must outlive all services — created before SwDeviceInit,
    // destroyed after SwDeviceDestroy. Declared here (not constructed) so the
    // teardown after the catch still sees it; construction moved inside try so a
    // constructor exception is caught and logged cleanly instead of escaping to
    // std::terminate.
    std::unique_ptr<cosmo::mem::DeviceContext> device_ctx;
    std::unique_ptr<ShutdownSignalMonitor> shutdown_signals;
    int exit_code = EXIT_SUCCESS;

    try {
        // Block process termination signals before any service creates worker
        // threads. A dedicated sigwait thread handles them in normal C++
        // context, so the libevent loop and hardware watchdog can shut down.
        shutdown_signals = std::make_unique<ShutdownSignalMonitor>();
        signal(SIGILL, SIG_IGN);
        signal(SIGPIPE, SIG_IGN);

        // Tune glibc malloc before any inference allocation to prevent
        // per-thread arena fragmentation (see ConfigureAllocator comment).
        ConfigureAllocator();

        SwDevicePreInit();

        // Log initialization
        LogInit(app_name_, cosmo::path::GetLogPath(), cosmo::util::GetAbbrVersion());

        LOG_INFO("{} Version:{}", cosmo::util::GetProgramDesc(), cosmo::util::GetAbbrVersion());

        // Construct after LogInit so a bm_dev_request failure is logged to a configured glog.
        device_ctx = std::make_unique<cosmo::mem::DeviceContext>();

        // Register DeviceContext via non-owning Set (it outlives ServiceRegistry)
        cosmo::service::ServiceRegistry::Instance().Set<cosmo::mem::IDeviceContext>(device_ctx.get());
        SwDeviceInit();

        auto& http_lifecycle =
            cosmo::service::ServiceRegistry::Instance().Get<cosmo::service::IHttpLifecycle>();
        shutdown_signals->Start([&http_lifecycle]() {
            cosmo::util::ProcessShutdown::Request();
            http_lifecycle.RequestHttpStop();
        });

        // Blocking — runs the HTTP server event loop until shutdown.
        SwDeviceRun();

    } catch (const std::exception& ex) {
        exit_code = EXIT_FAILURE;
        LOG_ERRO("ERROR Exit! [{}]", ex.what());
    } catch (...) {
        exit_code = EXIT_FAILURE;
        LOG_ERRO("{}", "ERROR Exit! [unknown fatal exception]");
    }

    if (shutdown_signals) {
        shutdown_signals->Stop();
    }

    SwDeviceDestroy();

    // Unregister before destruction
    cosmo::service::ServiceRegistry::Instance().Set<cosmo::mem::IDeviceContext>(nullptr);
    device_ctx.reset();

    LOG_INFO("{} Version:{} Quit", cosmo::util::GetProgramDesc(), cosmo::util::GetAbbrVersion());

    cosmo::log::LogShutDown();
    return exit_code;
}

}  // namespace cosmo::app
