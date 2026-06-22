#ifndef __BM_LOG_HPP__
#define __BM_LOG_HPP__

#include <glog/logging.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace bmlog {

class LogMessage : public google::LogMessage {
public:
    LogMessage(const char* module_name, const char* file, int line, int verbose_level)
        : google::LogMessage(file, line, google::GLOG_INFO) { Init(module_name, verbose_level); }

private:
    void Init(const char* module_name, int verbose_level) {
        if (FLAGS_log_prefix) {
            stream() << '[' << module_name << ':' << severity_lookup[verbose_level] << "] ";
        }
    }

    LogMessage(const LogMessage&);
    void operator=(const LogMessage&);

    // F: Fatal, E: Error, W: Warning, I: Info, D: Debug
    static constexpr const char* severity_lookup = "FEWID";
};

}

#define BMLOG_(verbose_level, module_name) bmlog::LogMessage(#module_name, __FILE__, __LINE__, verbose_level).stream()

#define BMLOG_IF_(verbose_level, condition, module_name) \
  static_cast<void>(0),                                  \
  !(condition) ? (void) 0 : google::LogMessageVoidify() & BMLOG_(verbose_level, module_name)

#define BMVLOG(verbose_level, module_name) BMLOG_IF_(verbose_level, VLOG_IS_ON(verbose_level), module_name)

#define VLOG_FATAL(module_name)   BMVLOG(0, module_name)
#define VLOG_ERROR(module_name)   BMVLOG(1, module_name)
#define VLOG_WARNING(module_name) BMVLOG(2, module_name)
#define VLOG_INFO(module_name)    BMVLOG(3, module_name)
#define VLOG_DEBUG(module_name)   BMVLOG(4, module_name)

#define VLOG_DBG VLOG_DEBUG
#define VLOG_1   VLOG_DEBUG
#define VLOG_0   VLOG_DEBUG

// interfaces below
#define BMLOG2(severity, module_name) VLOG_ ## severity (module_name)

#define BMLOG_IF2(severity, condition, module_name) \
  static_cast<void>(0),                             \
  !(condition) ? (void) 0 : VLOG_ ## severity (module_name)


namespace bmlog {

static std::string argv0_;

inline void init (const std::string &argv0) {
    argv0_ = argv0;
    google::InitGoogleLogging(argv0_.c_str());
    FLAGS_logtostderr = true;
    FLAGS_v = 3;
}

inline void shutdown () {
    google::ShutdownGoogleLogging();
}

inline bool is_dir(const std::string &path) {
    struct stat statbuf;
    if (!stat(path.c_str(), &statbuf) && S_ISDIR(statbuf.st_mode)) return true;
    return false;
}

inline void set_v(int level) { FLAGS_v = level; }
inline int  get_v()          { return FLAGS_v;  }

inline void set_log_dir(const std::string &dir) {
    FLAGS_logtostderr = (dir.empty() || !is_dir(dir));
    if (FLAGS_logtostderr) {
        BMLOG2(WARNING, BMLOG) << "log_dir (" << dir << ") not exist, print log to stderr..." << std::endl;
        return;
    }
    FLAGS_log_dir = dir;
}

inline std::string get_log_dir() { return FLAGS_log_dir; }

inline void set_log_prefix(bool val) { FLAGS_log_prefix = val; }
inline bool get_log_prefix()         { return FLAGS_log_prefix; }


}

#endif
