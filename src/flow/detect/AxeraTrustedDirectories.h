#pragma once

#ifdef COSMO_NN_USE_AXERA_BACKEND
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdint>
#include <string>

namespace cosmo::axera_trusted_directories {
struct Fd {
    int value{-1};
    ~Fd() {
        if (value >= 0)
            ::close(value);
    }
    Fd(const Fd&)            = delete;
    Fd& operator=(const Fd&) = delete;
    explicit Fd(int fd = -1) : value(fd) {}
};
struct Roots {
    Fd contracts;
    Fd models;
    std::uint32_t publisher_uid{0};
};
inline bool TrustedDirectory(int fd, uid_t publisher) {
    struct stat st {};
    return fd >= 0 && ::fstat(fd, &st) == 0 && S_ISDIR(st.st_mode) && st.st_uid == publisher &&
           (st.st_mode & 0022) == 0;
}
inline int OpenDirectory(int parent, const char* name) {
    return ::openat(parent, name, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
}
inline int OpenTrustedAbsoluteDirectory(const std::string& absolute) {
    if (absolute.empty() || absolute.front() != '/')
        return -1;
    Fd current{::open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW)};
    std::size_t pos = 1;
    while (current.value >= 0 && pos < absolute.size()) {
        const auto end       = absolute.find('/', pos);
        const auto component = absolute.substr(pos, end == std::string::npos ? end : end - pos);
        if (component.empty() || component == "." || component == "..")
            return -1;
        const int child = OpenDirectory(current.value, component.c_str());
        if (child < 0)
            return -1;
        ::close(current.value);
        current.value = child;
        struct stat st {};
        if (::fstat(child, &st) || !S_ISDIR(st.st_mode) || (st.st_mode & 0022) ||
            (st.st_uid != 0 && st.st_uid != ::geteuid()))
            return -1;
        if (end == std::string::npos)
            break;
        pos = end + 1;
    }
    const int result = current.value;
    current.value    = -1;
    return result;
}
}  // namespace cosmo::axera_trusted_directories
#endif
