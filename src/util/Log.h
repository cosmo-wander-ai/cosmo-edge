#pragma once

#include <functional>

#include "fmt/format.h"
#include "fmt/ostream.h"

namespace cosmo::log {
constexpr int kDefaultModule   = 255;
constexpr int kPrintAllModules = 256;

constexpr int kLevelDebug = 0;
constexpr int kLevelInfo  = 1;
constexpr int kLevelWarn  = 2;
constexpr int kLevelError = 3;

// Callback invoked after each log message is written.
typedef std::function<void(const char*, /* file */
                           const char*, /* function */
                           int,         /* line */
                           const char*, /* data */
                           size_t       /* size */
                           )>
    LogCallback;

// Initialize glog backend, set log destination and start cleanup thread.
void LogInit(const char* szAppName, const char* szLogDir, const char* title);

// Shut down glog and join the cleanup thread.
void LogShutDown();

// Flush all buffered log messages to disk immediately.
void FlushLog();

// Register a custom callback for log output (e.g. printf to console).
void SetLogCallback(LogCallback);

// Set the minimum severity level for log output (0=DEBUG .. 3=ERROR).
void SetLogLevel(int nLogLevel);
int GetLogLevel();

// Set the minimum severity level for stderr output (maps to glog threshold).
void SetPrintLevel(int nPrintLevel);

int GetLogModule();

// Set the maximum number of rotated log files to keep.
void SetMaxLogFileCount(int nCount);

// Set the log flush interval in seconds (minimum 1 second).
void SetLogFileFlushInterval(int nSeconds);

// Set the maximum size per log file in megabytes (clamped to [1, 1024]).
void SetMaxLogFileSize(int nMegabyte);

// Set the maximum number of days to retain historical log files.
void SetMaxLogFileKeepDays(int days);

// Set the log cleanup mode: 0 = by max file count, 1 = by max retention days.
void SetLogCleanLogMode(int mode);

size_t LogFormatArg(const char* file, const char* function, int line, int module, int severity,
                    fmt::string_view format, fmt::format_args args);

template <typename S, typename... Args>
size_t LogFormat(const char* file, const char* function, int line, int module, int severity, const S& format,
                 const Args&... args) {
    return LogFormatArg(file, function, line, module, severity, format, fmt::make_format_args(args...));
}

}  // namespace cosmo::log

// Internal constants exposed as macros for use in LOG_* macro bodies.
#define LOG_DEFAULT_MODULE cosmo::log::kDefaultModule
#define LOG_PRINT_ALL_MODULE cosmo::log::kPrintAllModules

// Log severity level aliases (used with SetLogLevel).
#define LOG_LEVEL_DEBUG cosmo::log::kLevelDebug
#define LOG_LEVEL_INFO cosmo::log::kLevelInfo
#define LOG_LEVEL_WARN cosmo::log::kLevelWarn
#define LOG_LEVEL_ERRO cosmo::log::kLevelError

// ---- Public logging macros (fmt {} format) ----

#define LOG_DEBUG(fmt, ...)                                                                                  \
    cosmo::log::LogFormat(__FILE__, __FUNCTION__, __LINE__, LOG_DEFAULT_MODULE, LOG_LEVEL_DEBUG,             \
                          FMT_STRING(fmt), ##__VA_ARGS__)
#define LOG_INFO(fmt, ...)                                                                                   \
    cosmo::log::LogFormat(__FILE__, __FUNCTION__, __LINE__, LOG_DEFAULT_MODULE, LOG_LEVEL_INFO,              \
                          FMT_STRING(fmt), ##__VA_ARGS__)
#define LOG_WARN(fmt, ...)                                                                                   \
    cosmo::log::LogFormat(__FILE__, __FUNCTION__, __LINE__, LOG_DEFAULT_MODULE, LOG_LEVEL_WARN,              \
                          FMT_STRING(fmt), ##__VA_ARGS__)
#define LOG_ERRO(fmt, ...)                                                                                   \
    cosmo::log::LogFormat(__FILE__, __FUNCTION__, __LINE__, LOG_DEFAULT_MODULE, LOG_LEVEL_ERRO,              \
                          FMT_STRING(fmt), ##__VA_ARGS__)

// AI target tracking debug log alias.
#define LOG_AITARGET LOG_DEBUG
