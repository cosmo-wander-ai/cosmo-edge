// Link-wrap device syscalls: exercise the real driver and worker thread without
// ever opening a hardware watchdog on the build host.
#include <fcntl.h>
#include <linux/watchdog.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <thread>

#include "platform/WatchDog.h"

namespace {
constexpr int kFakeFd = 12345;
std::atomic<int> opens{0}, feeds{0}, closes{0}, disables{0}, magic_closes{0};
bool fail_open = false, fail_timeout = false, fail_get_timeout = false;
bool fail_feed = false, fail_disable = false, fail_close = false;
int driver_timeout = 40;
std::atomic<int> failures{0};

void Check(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

void Reset() {
    opens = feeds = closes = disables = magic_closes = 0;
    fail_open = fail_timeout = fail_get_timeout = fail_feed = fail_disable = fail_close = false;
    driver_timeout                                                                      = 40;
}
}  // namespace

namespace cosmo::util {
std::string GenerateUUID() {
    return "watchdog-test";
}
}  // namespace cosmo::util

extern "C" int __wrap_open(const char* path, int flags, ...) {
    Check(std::strcmp(path, "/dev/watchdog") == 0, "opens watchdog device");
    Check((flags & O_ACCMODE) == O_WRONLY, "opens device for writing");
    ++opens;
    if (fail_open) {
        errno = EACCES;
        return -1;
    }
    return kFakeFd;
}

extern "C" int __wrap_ioctl(int fd, unsigned long request, ...) {
    Check(fd == kFakeFd, "ioctl uses the owned descriptor");
    va_list args;
    va_start(args, request);
    int result = 0;
    if (request == WDIOC_SETTIMEOUT) {
        auto* timeout = va_arg(args, int*);
        Check(*timeout == 30, "requests the configured timeout");
        *timeout = driver_timeout;  // Hardware may round the requested timeout.
        result   = fail_timeout ? -1 : 0;
    } else if (request == WDIOC_GETTIMEOUT) {
        *va_arg(args, int*) = driver_timeout;
        result              = fail_get_timeout ? -1 : 0;
    } else if (request == WDIOC_KEEPALIVE) {
        ++feeds;
        result = fail_feed ? -1 : 0;
    } else if (request == WDIOC_SETOPTIONS) {
        Check(*va_arg(args, int*) == WDIOS_DISABLECARD, "requests disable on stop");
        ++disables;
        result = fail_disable ? -1 : 0;
    } else {
        Check(false, "unexpected ioctl");
        result = -1;
    }
    va_end(args);
    if (result < 0) {
        errno = EIO;
    }
    return result;
}

extern "C" ssize_t __wrap_write(int fd, const void* data, size_t size) {
    Check(fd == kFakeFd && size == 1 && *static_cast<const char*>(data) == 'V', "writes magic close");
    ++magic_closes;
    return static_cast<ssize_t>(size);
}

extern "C" int __wrap_close(int fd) {
    Check(fd == kFakeFd, "closes owned device");
    ++closes;
    errno = EIO;
    return fail_close ? -1 : 0;
}

int main(int argc, char** argv) {
    const bool enabled = argc > 1 && std::strcmp(argv[1], "enabled") == 0;
    {
        cosmo::platform::WatchDog dog;
        Check(dog.Start(), "start succeeds");
        if (enabled && argc <= 2) {
            Check(dog.Start(), "repeated start preserves the running watchdog");
        }
        if (enabled) {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(4);
            while (feeds < 3 && std::chrono::steady_clock::now() < deadline) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }
        Check(dog.Stop(), "stop succeeds");
        Check(dog.Stop(), "second stop succeeds");
        Check(opens == (enabled ? 1 : 0), "production RK/Sophon opens hardware; CPU/dev does not");
        Check(closes == (enabled ? 1 : 0), "device closed exactly once");
        Check(enabled ? feeds >= 3 : feeds == 0, "production worker feeds hardware repeatedly");
        Check(disables == (enabled ? 1 : 0), "stop disables hardware");
    }
    if (!enabled || (argc > 2 && std::strcmp(argv[2], "smoke") == 0)) {
        return failures ? 1 : 0;
    }
    Reset();
    {
        cosmo::platform::WatchDog dog;
        fail_open = true;
        Check(!dog.Start(), "open failure propagates");
        Check(dog.Stop() && closes == 0, "failed open owns no descriptor");
    }
    Reset();
    {
        cosmo::platform::WatchDog dog;
        fail_timeout = true;
        Check(dog.Start(), "unsupported timeout uses readable driver timeout");
        Check(dog.Stop(), "fallback timeout can stop");
    }
    Reset();
    {
        cosmo::platform::WatchDog dog;
        fail_timeout = fail_get_timeout = true;
        Check(!dog.Start(), "unknown timeout fails startup");
        Check(closes == 1 && disables == 1, "timeout failure cleans up armed device");
    }
    Reset();
    {
        cosmo::platform::WatchDog dog;
        driver_timeout = 1;
        Check(!dog.Start(), "timeout must exceed feed interval");
        Check(closes == 1 && disables == 1, "unsafe timeout cleans up armed device");
    }
    Reset();
    {
        cosmo::platform::WatchDog dog;
        fail_feed = true;
        Check(!dog.Start(), "initial keepalive failure fails startup");
        Check(closes == 1 && disables == 1, "keepalive failure cleans up armed device");
    }
    Reset();
    {
        cosmo::platform::WatchDog dog;
        Check(dog.Start(), "start before disable failure");
        fail_disable = true;
        Check(!dog.Stop(), "unconfirmed disable is not reported as success");
        Check(magic_closes == 1 && closes == 1, "disable failure attempts magic close");
    }
    Reset();
    {
        cosmo::platform::WatchDog dog;
        Check(dog.Start(), "start before close failure");
        fail_close = true;
        Check(!dog.Stop(), "close failure propagates");
    }
    return failures ? 1 : 0;
}
