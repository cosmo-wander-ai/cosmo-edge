#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>

#include "catch_amalgamated.hpp"
#include "service/system/impl/LogArchive.h"
#include "util/Exec.h"
#include "util/ResourceBudget.h"

namespace {

namespace fs = std::filesystem;

void WriteFile(const fs::path& path, const std::string& contents) {
    std::ofstream output(path, std::ios::binary);
    REQUIRE(output.is_open());
    output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    output.close();
    REQUIRE(output.good());
}

std::string ReadFile(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    REQUIRE(input.is_open());
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

std::string RealTar() {
    for (const char* path : {"/usr/bin/tar", "/bin/tar"}) {
        if (access(path, X_OK) == 0) {
            return path;
        }
    }
    throw std::runtime_error("Log archive tests require tar");
}

class LogArchiveFiles {
public:
    LogArchiveFiles() {
        auto pattern = (fs::temp_directory_path() / "cosmo-log 'archive-XXXXXX").string();
        if (mkdtemp(pattern.data()) == nullptr) {
            throw std::runtime_error("Cannot create log archive test directory");
        }
        root    = pattern;
        log     = root / "log";
        conf    = root / "conf";
        staging = root / "temporary files";
        archive = root / "downloads" / "device logs.tar";
        fs::create_directory(log);
        fs::create_directory(conf);
        fs::create_directory(staging);
        fs::create_directory(archive.parent_path());
    }

    ~LogArchiveFiles() {
        std::error_code error;
        fs::remove_all(root, error);
    }

    LogArchiveFiles(const LogArchiveFiles&)            = delete;
    LogArchiveFiles& operator=(const LogArchiveFiles&) = delete;

    fs::path Extract() const {
        const auto destination = root / "extracted";
        fs::create_directory(destination);
        std::string output;
        const int result =
            cosmo::util::Exec({RealTar(), "-xf", archive.string(), "-C", destination.string()}, output);
        INFO(output);
        REQUIRE(result == 0);
        return destination;
    }

    fs::path root;
    fs::path log;
    fs::path conf;
    fs::path staging;
    fs::path archive;
};

// Catch2 runs these cases serially. Restore PATH even when a REQUIRE aborts a case.
class ScopedTarWrapper {
public:
    ScopedTarWrapper(const fs::path& root, const std::string& script) {
        const char* previous = std::getenv("PATH");
        had_path_            = previous != nullptr;
        if (previous != nullptr) {
            old_path_ = previous;
        }
        const auto directory = root / "bin";
        fs::create_directory(directory);
        const auto wrapper = directory / "tar";
        WriteFile(wrapper, "#!/bin/sh\nset -eu\n" + script);
        fs::permissions(wrapper, fs::perms::owner_all);
        const auto path = directory.string() + ":" + old_path_;
        if (setenv("PATH", path.c_str(), 1) != 0) {
            throw std::runtime_error("Cannot set PATH for log archive test");
        }
    }

    ~ScopedTarWrapper() {
        if (had_path_) {
            setenv("PATH", old_path_.c_str(), 1);
        } else {
            unsetenv("PATH");
        }
    }

    ScopedTarWrapper(const ScopedTarWrapper&)            = delete;
    ScopedTarWrapper& operator=(const ScopedTarWrapper&) = delete;

private:
    bool had_path_ = false;
    std::string old_path_;
};

}  // namespace

TEST_CASE("Log export preserves nested files, empty directories and config", "[log-archive]") {
    LogArchiveFiles files;
    fs::create_directories(files.log / "nested" / "empty directory");
    WriteFile(files.log / "nginx_access.log", "first request\nsecond request\n");
    const struct timespec timestamps[] = {{1700000000, 0}, {1700000123, 0}};
    REQUIRE(utimensat(AT_FDCWD, (files.log / "nginx_access.log").c_str(), timestamps, 0) == 0);
    WriteFile(files.log / "nested" / "empty.log", "");
    const std::string binary("a\0b\xff", 4);
    WriteFile(files.conf / "settings.bin", binary);

    std::string error;
    const bool result =
        cosmo::service::CreateLogArchive(files.log, files.conf, files.staging, files.archive, error);
    INFO(error);
    REQUIRE(result);
    CHECK(error.empty());
    CHECK(fs::is_empty(files.staging));
    const auto extracted = files.Extract();
    CHECK(ReadFile(extracted / "log" / "nginx_access.log") == "first request\nsecond request\n");
    CHECK(fs::last_write_time(extracted / "log" / "nginx_access.log") ==
          fs::last_write_time(files.log / "nginx_access.log"));
    CHECK(ReadFile(extracted / "log" / "nested" / "empty.log").empty());
    CHECK(fs::is_directory(extracted / "log" / "nested" / "empty directory"));
    CHECK(ReadFile(extracted / "conf" / "settings.bin") == binary);
    CHECK(ReadFile(files.log / "nginx_access.log") == "first request\nsecond request\n");
}

TEST_CASE("Log export archives a stable snapshot when the live log changes", "[log-archive]") {
    LogArchiveFiles files;
    const auto live_log = files.log / "nginx_access.log";
    WriteFile(live_log, "before snapshot\n");
    WriteFile(files.conf / "settings.json", "{}\n");
    // Mutating at tar launch is deterministic: snapshotting has finished but
    // tar has not read any input yet. The archive must retain the earlier data.
    const auto quoted_log = cosmo::util::ShellEscape(live_log.string());
    std::string mutation;
    std::string expected_live;
    SECTION("Append to the live log") {
        mutation      = "printf 'after snapshot\\n' >> " + quoted_log + "\n";
        expected_live = "before snapshot\nafter snapshot\n";
    }
    SECTION("Rotate and replace the live log") {
        mutation = "mv -- " + quoted_log + " " +
                   cosmo::util::ShellEscape((files.log / "nginx_access.log.1").string()) +
                   "\nprintf 'replacement log\\n' > " + quoted_log + "\n";
        expected_live = "replacement log\n";
    }
    ScopedTarWrapper wrapper(files.root,
                             mutation + "exec " + cosmo::util::ShellEscape(RealTar()) + " \"$@\"\n");

    std::string error;
    const bool result =
        cosmo::service::CreateLogArchive(files.log, files.conf, files.staging, files.archive, error);
    INFO(error);
    REQUIRE(result);
    CHECK(ReadFile(live_log) == expected_live);
    const auto extracted = files.Extract();
    CHECK(ReadFile(extracted / "log" / "nginx_access.log") == "before snapshot\n");
    CHECK_FALSE(fs::exists(extracted / "log" / "nginx_access.log.1"));
    CHECK(ReadFile(extracted / "conf" / "settings.json") == "{}\n");
    CHECK(fs::is_empty(files.staging));
}

TEST_CASE("Log export rejects a snapshot larger than its storage budget", "[log-archive]") {
    LogArchiveFiles files;
    const auto budget = cosmo::util::InspectStorageResourceBudget(files.staging.string());
    REQUIRE(budget.valid);
    REQUIRE(budget.usable_bytes < static_cast<std::uint64_t>(std::numeric_limits<off_t>::max()));
    const auto sparse_log = files.log / "oversized.log";
    WriteFile(sparse_log, "");
    const auto logical_size = budget.usable_bytes + 1;
    // resize_file creates a sparse extent, without writing the enormous payload.
    fs::resize_file(sparse_log, logical_size);
    struct stat status {};
    REQUIRE(stat(sparse_log.c_str(), &status) == 0);
    REQUIRE(status.st_blocks == 0);

    std::string error;
    CHECK_FALSE(cosmo::service::CreateLogArchive(files.log, files.conf, files.staging, files.archive, error));
    CHECK(error.find("Insufficient storage") != std::string::npos);
    CHECK_FALSE(fs::exists(files.archive));
    CHECK(fs::is_empty(files.staging));
    CHECK(fs::file_size(sparse_log) == logical_size);
}

TEST_CASE("Log export removes partial archives on every tar failure", "[log-archive]") {
    const int exit_code = GENERATE(1, 2);
    LogArchiveFiles files;
    WriteFile(files.log / "engine.log", "original contents\n");
    const std::string script = R"sh(archive=
while [ "$#" -gt 0 ]; do
    case "$1" in
        -f|--file|-cf|-cvf|-czf|-czvf) shift; archive=$1 ;;
        --file=*) archive=${1#--file=} ;;
    esac
    shift
done
[ -n "$archive" ]
printf 'partial archive' > "$archive"
printf 'injected tar failure\n' >&2
)sh";
    ScopedTarWrapper wrapper(files.root, script + "exit " + std::to_string(exit_code) + "\n");

    std::string error;
    CHECK_FALSE(cosmo::service::CreateLogArchive(files.log, files.conf, files.staging, files.archive, error));
    CHECK(error.find("injected tar failure") != std::string::npos);
    CHECK_FALSE(fs::exists(files.archive));
    CHECK(fs::is_empty(files.staging));
    CHECK(ReadFile(files.log / "engine.log") == "original contents\n");
}

TEST_CASE("Log export preserves symlinks without archiving their targets", "[log-archive]") {
    LogArchiveFiles files;
    WriteFile(files.root / "outside.log", "outside the requested source tree\n");
    fs::create_directory(files.root / "outside directory");
    WriteFile(files.root / "outside directory" / "outside.txt", "outside directory contents\n");
    fs::create_symlink("../outside.log", files.log / "linked.log");
    fs::create_directory_symlink("../outside directory", files.log / "linked directory");
    fs::create_symlink("missing.log", files.conf / "dangling.log");

    std::string error;
    const bool result =
        cosmo::service::CreateLogArchive(files.log, files.conf, files.staging, files.archive, error);
    INFO(error);
    REQUIRE(result);
    const auto extracted = files.Extract();
    CHECK(fs::is_symlink(extracted / "log" / "linked.log"));
    CHECK(fs::read_symlink(extracted / "log" / "linked.log") == "../outside.log");
    CHECK(fs::is_symlink(extracted / "log" / "linked directory"));
    CHECK(fs::read_symlink(extracted / "log" / "linked directory") == "../outside directory");
    CHECK(fs::is_symlink(extracted / "conf" / "dangling.log"));
    CHECK(fs::read_symlink(extracted / "conf" / "dangling.log") == "missing.log");
    CHECK_FALSE(fs::exists(extracted / "outside.log"));
    CHECK_FALSE(fs::exists(extracted / "outside directory"));
    CHECK(fs::is_empty(files.staging));
}

TEST_CASE("Log export rejects invalid source directories and special files", "[log-archive]") {
    LogArchiveFiles files;
    WriteFile(files.log / "engine.log", "original contents\n");

    SECTION("Missing log directory") {
        fs::remove_all(files.log);
    }
    SECTION("Config path is a regular file") {
        fs::remove(files.conf);
        WriteFile(files.conf, "not a directory\n");
    }
    SECTION("FIFO in a source directory") {
        REQUIRE(mkfifo((files.log / "pipe.log").c_str(), 0600) == 0);
    }

    std::string error;
    CHECK_FALSE(cosmo::service::CreateLogArchive(files.log, files.conf, files.staging, files.archive, error));
    CHECK_FALSE(error.empty());
    CHECK_FALSE(fs::exists(files.archive));
    CHECK(fs::is_empty(files.staging));
}

TEST_CASE("Log export cleans temporary files if archive publication fails", "[log-archive]") {
    LogArchiveFiles files;
    WriteFile(files.log / "engine.log", "original contents\n");
    fs::create_directory(files.archive);
    WriteFile(files.archive / "keep.txt", "existing destination\n");

    std::string error;
    CHECK_FALSE(cosmo::service::CreateLogArchive(files.log, files.conf, files.staging, files.archive, error));
    CHECK_FALSE(error.empty());
    CHECK(fs::is_empty(files.staging));
    CHECK(fs::is_directory(files.archive));
    CHECK(ReadFile(files.archive / "keep.txt") == "existing destination\n");
    CHECK(ReadFile(files.log / "engine.log") == "original contents\n");
}
