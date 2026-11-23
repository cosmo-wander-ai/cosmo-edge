#pragma once

// Helpers for exact model reads and AX multi-stage file selection. A successful
// read does not establish model identity or protect against a concurrent writer.
#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <istream>
#include <limits>
#include <new>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "util/PathUtil.h"

#if defined(__unix__) || defined(__APPLE__)
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace cosmo::nn::model_file_safety {

inline bool ReadExactly(std::istream& stream, char* destination, std::size_t bytes) {
    if (!destination || bytes == 0 ||
        bytes > static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max()))
        return false;
    const auto count = static_cast<std::streamsize>(bytes);
    stream.read(destination, count);
    return stream.good() && stream.gcount() == count;
}

inline bool ValidAxStageNames(const std::vector<std::string>& names) {
    if (names.size() < 2)
        return false;
    std::set<std::string> unique;
    for (const auto& name : names) {
        if (!cosmo::path::IsSafePathComponent(name) ||
            std::filesystem::path(name).extension() != ".axmodel" || !unique.insert(name).second)
            return false;
    }
    return true;
}

#if defined(__unix__) || defined(__APPLE__)
struct FileIdentity {
    std::uintmax_t device;
    std::uintmax_t inode;
};

inline bool InsertIdentity(std::set<std::pair<std::uintmax_t, std::uintmax_t>>& identities,
                           const FileIdentity& identity) {
    return identities.emplace(identity.device, identity.inode).second;
}

// The same opened directory is retained for the entire multi-stage load.
class AxStageDirectory {
public:
    explicit AxStageDirectory(const std::filesystem::path& directory)
        : fd_(::open(directory.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW)) {}
    ~AxStageDirectory() {
        if (fd_ >= 0)
            ::close(fd_);
    }
    AxStageDirectory(const AxStageDirectory&)            = delete;
    AxStageDirectory& operator=(const AxStageDirectory&) = delete;
    bool valid() const {
        return fd_ >= 0;
    }

    // Open a basename relative to the pinned directory, refusing symlinks and
    // non-regular files. Bytes and identity come from the same descriptor.
    bool Read(const std::string& name, std::vector<char>& output, FileIdentity& identity) const {
        output.clear();
        if (!valid() || !cosmo::path::IsSafePathComponent(name) ||
            std::filesystem::path(name).extension() != ".axmodel")
            return false;
        struct Fd {
            int value;
            ~Fd() {
                if (value >= 0)
                    ::close(value);
            }
        };
        Fd file{::openat(fd_, name.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK)};
        if (file.value < 0)
            return false;
        struct stat before {};
        if (::fstat(file.value, &before) != 0 || !S_ISREG(before.st_mode) || before.st_nlink != 1 ||
            before.st_size <= 0 ||
            static_cast<std::uintmax_t>(before.st_size) > std::numeric_limits<std::uint32_t>::max() ||
            static_cast<std::uintmax_t>(before.st_size) > std::numeric_limits<std::size_t>::max())
            return false;
        const auto size = static_cast<std::size_t>(before.st_size);
        try {
            output.resize(size);
        } catch (const std::bad_alloc&) {
            return false;
        } catch (const std::length_error&) {
            return false;
        }
        std::size_t consumed = 0;
        while (consumed < size) {
            const auto request =
                std::min(size - consumed, static_cast<std::size_t>(std::numeric_limits<ssize_t>::max()));
            const auto count = ::read(file.value, output.data() + consumed, request);
            if (count < 0 && errno == EINTR)
                continue;
            if (count <= 0) {
                output.clear();
                return false;
            }
            consumed += static_cast<std::size_t>(count);
        }
        struct stat after {};
        if (::fstat(file.value, &after) != 0 || after.st_size != before.st_size || after.st_nlink != 1 ||
            after.st_ino != before.st_ino || after.st_dev != before.st_dev) {
            output.clear();
            return false;
        }
        identity = {static_cast<std::uintmax_t>(before.st_dev), static_cast<std::uintmax_t>(before.st_ino)};
        return true;
    }

private:
    int fd_;
};
#endif

}  // namespace cosmo::nn::model_file_safety
