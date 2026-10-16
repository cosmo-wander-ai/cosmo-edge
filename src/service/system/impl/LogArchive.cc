#include "service/system/impl/LogArchive.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdlib>
#include <stdexcept>
#include <system_error>

#include "util/Exec.h"
#include "util/ResourceBudget.h"

namespace cosmo::service {
namespace {

    namespace fs = std::filesystem;

    class ScopedFd {
    public:
        explicit ScopedFd(int fd) : fd_(fd) {}
        ~ScopedFd() {
            if (fd_ >= 0) {
                close(fd_);
            }
        }
        ScopedFd(const ScopedFd&)            = delete;
        ScopedFd& operator=(const ScopedFd&) = delete;
        int Get() const {
            return fd_;
        }
        void Close() {
            const int fd = fd_;
            fd_          = -1;
            if (close(fd) != 0) {
                throw std::system_error(errno, std::generic_category(), "close log snapshot");
            }
        }

    private:
        int fd_;
    };

    class SnapshotDirectory {
    public:
        explicit SnapshotDirectory(const fs::path& root) {
            auto pattern = (root / "log-export-XXXXXX").string();
            if (mkdtemp(pattern.data()) == nullptr) {
                throw std::system_error(errno, std::generic_category(), "create log snapshot directory");
            }
            path_ = std::move(pattern);
        }
        ~SnapshotDirectory() {
            std::error_code ec;
            fs::remove_all(path_, ec);
        }
        SnapshotDirectory(const SnapshotDirectory&)            = delete;
        SnapshotDirectory& operator=(const SnapshotDirectory&) = delete;
        const fs::path& Path() const {
            return path_;
        }

    private:
        fs::path path_;
    };

    // Account for both the snapshot and uncompressed archive, rounded up for
    // filesystem blocks, plus directory entries and tar long-name headers.
    // This is admission only; all subsequent writes/close/rename still checked.
    void ReserveEntry(std::uint64_t size, std::uint64_t& remaining) {
        constexpr std::uint64_t kBlock         = 4096;
        constexpr std::uint64_t kMetadataBytes = 16 * 1024;
        if (remaining < kMetadataBytes || size > (remaining - kMetadataBytes) / 2) {
            throw std::runtime_error("Insufficient storage for log snapshot and archive");
        }
        const auto rounded = size + (kBlock - size % kBlock) % kBlock;
        if (rounded > (remaining - kMetadataBytes) / 2) {
            throw std::runtime_error("Insufficient storage for log snapshot and archive");
        }
        remaining -= 2 * rounded + kMetadataBytes;
    }

    void SnapshotFile(const fs::path& source, const fs::path& destination, std::uint64_t& budget) {
        // O_NONBLOCK also prevents hanging if a file is replaced by a FIFO
        // between directory enumeration and open. Never follow a replaced link.
        ScopedFd input(open(source.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK));
        if (input.Get() < 0) {
            throw std::system_error(errno, std::generic_category(),
                                    "open log snapshot source " + source.string());
        }
        struct stat status {};
        if (fstat(input.Get(), &status) != 0) {
            throw std::system_error(errno, std::generic_category(), "stat log snapshot source");
        }
        if (!S_ISREG(status.st_mode) || status.st_size < 0) {
            throw std::runtime_error("Log snapshot source is not a regular file: " + source.string());
        }
        auto remaining = static_cast<std::uint64_t>(status.st_size);
        ReserveEntry(remaining, budget);
        ScopedFd output(open(destination.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600));
        if (output.Get() < 0) {
            throw std::system_error(errno, std::generic_category(), "create log snapshot file");
        }

        std::array<char, 64 * 1024> buffer;
        while (remaining > 0) {
            const auto count    = static_cast<size_t>(std::min<std::uint64_t>(remaining, buffer.size()));
            const auto received = read(input.Get(), buffer.data(), count);
            if (received < 0) {
                if (errno == EINTR) {
                    continue;
                }
                throw std::system_error(errno, std::generic_category(), "read log snapshot source");
            }
            if (received == 0) {
                throw std::runtime_error("Log snapshot source was truncated: " + source.string());
            }
            ssize_t written = 0;
            while (written < received) {
                const auto count_written = write(output.Get(), buffer.data() + written, received - written);
                if (count_written < 0 && errno == EINTR) {
                    continue;
                }
                if (count_written <= 0) {
                    throw std::system_error(count_written < 0 ? errno : EIO, std::generic_category(),
                                            "write log snapshot file");
                }
                written += count_written;
            }
            remaining -= static_cast<std::uint64_t>(received);
        }
        const timespec times[] = {status.st_atim, status.st_mtim};
        if (futimens(output.Get(), times) != 0) {
            throw std::system_error(errno, std::generic_category(), "preserve log snapshot timestamps");
        }
        output.Close();
    }

    void SnapshotTree(const fs::path& source, const fs::path& destination, std::uint64_t& budget) {
        if (!fs::is_directory(source)) {
            throw std::runtime_error("Invalid log/config directory: " + source.string());
        }
        ReserveEntry(0, budget);
        fs::create_directory(destination);
        for (const auto& entry : fs::recursive_directory_iterator(source)) {
            const auto target = destination / entry.path().lexically_relative(source);
            const auto status = entry.symlink_status();
            if (fs::is_symlink(status)) {
                ReserveEntry(0, budget);
                fs::copy_symlink(entry.path(), target);
            } else if (fs::is_directory(status)) {
                ReserveEntry(0, budget);
                fs::create_directory(target);
            } else if (fs::is_regular_file(status)) {
                SnapshotFile(entry.path(), target, budget);
            } else {
                throw std::runtime_error("Unsupported log/config entry: " + entry.path().string());
            }
        }
    }

}  // namespace

bool CreateLogArchive(const std::filesystem::path& log_path, const std::filesystem::path& config_path,
                      const std::filesystem::path& temporary_root, const std::filesystem::path& archive_path,
                      std::string& error) {
    error.clear();
    try {
        const auto snapshot_budget = util::InspectStorageResourceBudget(temporary_root.string());
        const auto archive_budget  = util::InspectStorageResourceBudget(archive_path.parent_path().string());
        if (!snapshot_budget.valid || !archive_budget.valid) {
            throw std::runtime_error("Cannot inspect log export storage");
        }
        auto remaining = std::min(snapshot_budget.usable_bytes, archive_budget.usable_bytes);
        // Include the workspace directory and tar end-of-archive/padding blocks.
        ReserveEntry(16 * 1024, remaining);
        SnapshotDirectory snapshot(temporary_root);
        SnapshotTree(log_path, snapshot.Path() / log_path.filename(), remaining);
        SnapshotTree(config_path, snapshot.Path() / config_path.filename(), remaining);

        const auto pending_archive = snapshot.Path() / "archive.tar";
        std::string output;
        const int result = util::Exec({"tar", "-cf", pending_archive.string(), "-C", snapshot.Path().string(),
                                       "--", log_path.filename().string(), config_path.filename().string()},
                                      output);
        if (result != 0) {
            throw std::runtime_error("tar failed (" + std::to_string(result) + "): " + output);
        }
        // Only a completed archive becomes visible to the download endpoint.
        fs::rename(pending_archive, archive_path);
        return true;
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }
}

}  // namespace cosmo::service
