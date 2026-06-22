// Log — Callback invoked after each log message is written.

#include "Log.h"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <filesystem>
#include <map>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "glog/logging.h"
#include "util/Exec.h"

namespace cosmo::log {

namespace {
    namespace fs = std::filesystem;

    // Current log severity threshold.
    static std::atomic_int g_nLogLevel(kLevelInfo);
    // Active module filter for selective logging.
    static std::atomic_int g_nPrintModule(kDefaultModule);
    // Maximum number of current log files to keep.
    static std::atomic_int g_nMaxLogFileCount(200);
    // Maximum number of archived (history) log files to keep.
    static std::atomic_int g_nMaxHxLogFileCount(200);
    // Log cleanup interval in seconds.
    static std::atomic_int g_nSecCleanLogFile(120);
    // Log flush interval in seconds.
    static std::atomic_int g_nSecFlushLogFile(60);
    // Maximum days to retain log files.
    static std::atomic_int g_nMaxLogFileKeepDays(30);
    // Cleanup mode: 0 = by max file count, 1 = by max retention days.
    static std::atomic_int g_nCleanLogMode(1);
    // Whether automatic log cleanup is enabled.
    static std::atomic_bool g_bAutoClean(true);
    // Cleanup / flush thread and its synchronization primitives.
    static std::thread g_thrCleanLog;
    static std::condition_variable g_cvStop;
    static std::mutex g_mtxStop;
    static std::atomic_bool g_stopRequested(false);
    // Ensures LogInitImpl runs at most once.
    static std::once_flag g_initOnce;

    std::shared_mutex g_callbackMutex;
    LogCallback g_logCallback;

    constexpr int ToGlogSeverity(int mlog_severity) {
        // MLOG levels: 0=DEBUG, 1=INFO, 2=WARN, 3=ERROR
        // glog levels: 0=INFO, 1=WARNING, 2=ERROR
        constexpr int kMap[] = {0, 0, 1, 2};
        return (mlog_severity >= 0 && mlog_severity <= 3) ? kMap[mlog_severity] : 0;
    }
    bool LogLevelCheck(int module, int severity) {
        if (severity < GetLogLevel()) {
            return false;
        }

        return LOG_DEFAULT_MODULE <= module || LOG_PRINT_ALL_MODULE == GetLogModule() ||
               module == GetLogModule();
    }

    size_t LogModule(const char* file, const char* function, int line, int module, int severity,
                     std::string&& str) {
        int glog_severity = ToGlogSeverity(severity);
        if (LOG_DEFAULT_MODULE == module) {
            google::LogMessage(file, line, glog_severity).stream() << '[' << function << "] " << str;
        } else {
            google::LogMessage(file, line, glog_severity).stream()
                << '[' << function << "][" << module << "] " << str;
        }

        {
            std::shared_lock lock(g_callbackMutex);
            if (g_logCallback) {
                g_logCallback(file, function, line, str.data(), str.size());
            }
        }

        return str.size();
    }

    void logFailureWriter(const char* data, int size) {
        google::LogMessage("FATAL", 0, google::GLOG_ERROR).stream()
            << std::string_view(data, static_cast<size_t>(size));
        google::FlushLogFiles(google::GLOG_INFO);
    }

    static std::map<std::string, std::vector<fs::path>> GetLogFiles(
        const fs::path& dir, const std::string& appName, const std::vector<std::string>& severities) {
        std::map<std::string, std::vector<fs::path>> mapFileNames;
        std::vector<fs::path> excludePaths;

        if (!fs::exists(dir) || !fs::is_directory(dir)) {
            return mapFileNames;
        }

        for (const auto& entry : fs::directory_iterator(dir)) {
            if (!entry.is_regular_file() && !entry.is_symlink())
                continue;

            auto fileName = entry.path().filename().string();
            if (fileName.find(appName) != 0)
                continue;

            for (const auto& severity : severities) {
                if (fileName == appName + "." + severity) {
                    // It's a symlink or the active file, resolve it
                    std::error_code ec;
                    fs::path target = fs::canonical(entry.path(), ec);
                    if (!ec)
                        excludePaths.push_back(target);
                } else if (fileName.find(severity, appName.length() + 1) == appName.length() + 1) {
                    mapFileNames[severity].push_back(entry.path());
                }
            }
        }

        // Remove active files from list and sort
        for (auto& pair : mapFileNames) {
            auto& files = pair.second;
            files.erase(std::remove_if(files.begin(), files.end(),
                                       [&](const fs::path& p) {
                                           return std::find(excludePaths.begin(), excludePaths.end(), p) !=
                                                  excludePaths.end();
                                       }),
                        files.end());

            // Sort by filename (which includes timestamp for glog files)
            std::sort(files.begin(), files.end());
        }

        return mapFileNames;
    }

    static size_t CalculateRemoveCount(const std::vector<fs::path>& vecFiles, bool isHistory) {
        if (g_nCleanLogMode.load(std::memory_order_relaxed)) {
            size_t removeCount = 0;
            auto now           = fs::file_time_type::clock::now();
            for (const auto& file : vecFiles) {
                std::error_code ec;
                auto ftime = fs::last_write_time(file, ec);
                if (!ec) {
                    auto diff = std::chrono::duration_cast<std::chrono::hours>(now - ftime).count();
                    if (diff >= g_nMaxLogFileKeepDays.load(std::memory_order_relaxed) * 24) {
                        removeCount++;
                    }
                }
            }
            return removeCount;
        } else {
            int maxCount = isHistory ? g_nMaxHxLogFileCount.load(std::memory_order_relaxed)
                                     : g_nMaxLogFileCount.load(std::memory_order_relaxed);
            if (vecFiles.size() > static_cast<size_t>(maxCount)) {
                return vecFiles.size() - static_cast<size_t>(maxCount);
            }
        }
        return 0;
    }

    static void ArchiveLogFile(const fs::path& dir, const fs::path& filePath) {
        auto fileName = filePath.string();
        if (fileName.size() > 3 && fileName.substr(fileName.size() - 3) != ".gz") {
            auto escaped = cosmo::util::ShellEscape(fileName);
            auto execStr = "gzip " + escaped;
            std::string execOut;
            int ret = cosmo::util::Exec(execStr, execOut);
            if (ret != 0) {
                // Ignore or print error
            }

            auto historyDir = dir / "log_history";
            std::error_code ec;
            fs::create_directories(historyDir, ec);
            if (!ec) {
                auto execStr2 = "mv " + cosmo::util::ShellEscape(fileName + ".gz") + " " +
                                cosmo::util::ShellEscape(historyDir.string()) + "/";
                ret = cosmo::util::Exec(execStr2, execOut);
                if (ret != 0) {
                    fprintf(stderr, "move log archive failed (%d): %s\n", ret, execOut.c_str());
                }
            } else {
                fprintf(stderr, "create history directory failed: %s\n", ec.message().c_str());
            }
        }
    }

    static void CleanSeverityLogDir(const fs::path& dir, const std::string& /*appName*/,
                                    const std::string& severity, std::vector<fs::path>& vecFiles,
                                    bool isHistory) {
        if (vecFiles.empty())
            return;

        auto remFunc = [](const fs::path& filePath) {
            std::error_code ec;
            fs::remove(filePath, ec);
            if (ec) {
                fprintf(stderr, "[%s:%d] remove %s: fail (%s)\n", __FUNCTION__, __LINE__, filePath.c_str(),
                        ec.message().c_str());
            }
        };

        if (severity == google::GetLogSeverityName(google::GLOG_WARNING) ||
            severity == google::GetLogSeverityName(google::GLOG_ERROR)) {
            // Always remove WARNING / ERROR logs (not worth archiving).
            for (const auto& file : vecFiles)
                remFunc(file);
        } else {
            size_t removeCount = CalculateRemoveCount(vecFiles, isHistory);

            for (size_t i = 0; i < removeCount; ++i)
                remFunc(vecFiles[i]);

            // Compress and archive log files in the main dir.
            if (!isHistory) {
                for (size_t i = removeCount; i < vecFiles.size(); ++i) {
                    ArchiveLogFile(dir, vecFiles[i]);
                }
            }
        }
    }

    static void CleanLogDir(const fs::path& dir, const std::string& appName,
                            const std::vector<std::string>& severities, bool isHistory) {
        auto mapFiles = GetLogFiles(dir, appName, severities);
        for (auto& pair : mapFiles) {
            CleanSeverityLogDir(dir, appName, pair.first, pair.second, isHistory);
        }
    }

    void SetLogDest(int nSeverity, const std::string& fileName) {
        google::SetLogDestination(
            static_cast<google::LogSeverity>(nSeverity),
            (fileName + "." + google::GetLogSeverityName(static_cast<google::LogSeverity>(nSeverity)) + ".")
                .c_str());
    }

    void LogInitImpl(const char* szAppName, const char* logDir, bool handleFailure) {
        std::call_once(g_initOnce, [&] {
            auto appName = fs::path(szAppName).filename();
            create_directories(fs::path(logDir));
            google::InitGoogleLogging(appName.c_str());
            FLAGS_log_dir      = logDir;
            FLAGS_max_log_size = 10;

            auto fileName = fs::path(FLAGS_log_dir) / appName;
            SetLogDest(google::GLOG_INFO, fileName);
            SetLogDest(google::GLOG_WARNING, fileName);
            SetLogDest(google::GLOG_ERROR, fileName);

            if (handleFailure) {
                // Record detailed info on segfault.
                google::InstallFailureSignalHandler();
                google::InstallFailureWriter(&logFailureWriter);
            }

            std::vector<std::string> vecSeverity{google::GetLogSeverityName(google::GLOG_INFO),
                                                 google::GetLogSeverityName(google::GLOG_WARNING),
                                                 google::GetLogSeverityName(google::GLOG_ERROR)};

            // Capture logDir by value so the cleanup thread never reads FLAGS_log_dir.
            auto logDirPath = fs::path(logDir);
            g_stopRequested.store(false, std::memory_order_relaxed);
            g_thrCleanLog = std::thread([appName, vecSeverity, logDirPath]() {
                int times = 0;
                while (true) {
                    std::unique_lock lock(g_mtxStop);
                    if (g_cvStop.wait_for(lock, std::chrono::seconds(1),
                                          [] { return g_stopRequested.load(std::memory_order_relaxed); })) {
                        break;
                    }

                    times++;
                    // Periodic log cleanup.
                    if (g_bAutoClean.load(std::memory_order_relaxed) &&
                        times % g_nSecCleanLogFile.load(std::memory_order_relaxed) == 0) {
                        CleanLogDir(logDirPath, appName, vecSeverity, false);
                        CleanLogDir(logDirPath / "log_history", appName, vecSeverity, true);
                    }
                    // Periodic log flush.
                    if (times % g_nSecFlushLogFile.load(std::memory_order_relaxed) == 0) {
                        google::FlushLogFiles(google::GLOG_INFO);
                    }
                }
            });
        });
    }

}  // namespace

size_t LogFormatArg(const char* file, const char* function, int line, int module, int severity,
                    fmt::string_view format, fmt::format_args args) {
    return LogLevelCheck(module, severity)
               ? LogModule(file, function, line, module, severity, fmt::vformat(format, args))
               : 0;
}

void LogInit(const char* szAppName, const char* szLogDir, const char* /* title */) {
    LogInitImpl(szAppName, szLogDir, true);
}

void LogShutDown() {
    {
        std::lock_guard lock(g_mtxStop);
        g_stopRequested.store(true, std::memory_order_relaxed);
    }
    g_cvStop.notify_all();

    if (g_thrCleanLog.joinable()) {
        g_thrCleanLog.join();
    }
    google::ShutdownGoogleLogging();
}

void FlushLog() {
    google::FlushLogFiles(google::GLOG_INFO);
}

void SetLogCallback(LogCallback func) {
    std::unique_lock lock(g_callbackMutex);
    g_logCallback = std::move(func);
}

void SetLogLevel(int nLogLevel) {
    int nPrevLevel = g_nLogLevel.load(std::memory_order_relaxed);
    if (nLogLevel < 0) {
        g_nLogLevel.store(0, std::memory_order_relaxed);
    } else if (nLogLevel > 3) {
        g_nLogLevel.store(3, std::memory_order_relaxed);
    } else {
        g_nLogLevel.store(nLogLevel, std::memory_order_relaxed);
    }
    google::LogMessage("GLOG", 0, google::GLOG_INFO).stream()
        << "change log level, " << nPrevLevel << " -> " << g_nLogLevel.load(std::memory_order_relaxed);
}

int GetLogLevel() {
    return g_nLogLevel.load(std::memory_order_relaxed);
}

// NOTE: FLAGS_stderrthreshold is not thread-safe in glog. This function
// must only be called from the main thread during initialization.
void SetPrintLevel(int nPrintLevel) {
    int nPrevPrintLevel = FLAGS_stderrthreshold + 1;
    if (nPrintLevel <= 0) {
        FLAGS_stderrthreshold = 0;
    } else if (nPrintLevel > 3) {
        FLAGS_stderrthreshold = 2;
    } else {
        FLAGS_stderrthreshold = nPrintLevel - 1;
    }
    google::LogMessage("GLOG", 0, google::GLOG_INFO).stream()
        << "change print level, " << nPrevPrintLevel << " -> " << FLAGS_stderrthreshold + 1;
}

int GetLogModule() {
    return g_nPrintModule.load(std::memory_order_relaxed);
}

void SetMaxLogFileCount(int nCount) {
    if (nCount >= 1) {
        g_nMaxLogFileCount.store(nCount, std::memory_order_relaxed);
    }
}

// NOTE: FLAGS_max_log_size is not thread-safe in glog. This function
// must only be called from the main thread during initialization.
void SetMaxLogFileSize(int nMegabyte) {
    if (nMegabyte > 1024) {
        nMegabyte = 1024;
    } else if (nMegabyte < 1) {
        nMegabyte = 1;
    }
    FLAGS_max_log_size = nMegabyte;
}

void SetLogFileFlushInterval(int nSeconds) {
    int secFlush = g_nSecFlushLogFile.load(std::memory_order_relaxed);
    if (nSeconds < 1) {
        nSeconds = 1;
    }
    g_nSecFlushLogFile.store(nSeconds, std::memory_order_relaxed);
    google::LogMessage("GLOG", 0, google::GLOG_INFO).stream()
        << "change flush interval, " << secFlush << " -> "
        << g_nSecFlushLogFile.load(std::memory_order_relaxed);
}

void SetMaxLogFileKeepDays(int maxLogFileKeepDays) {
    g_nMaxLogFileKeepDays.store(maxLogFileKeepDays, std::memory_order_relaxed);
}

void SetLogCleanLogMode(int mode) {
    g_nCleanLogMode.store(mode, std::memory_order_relaxed);
}

}  // namespace cosmo::log
