#include <array>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>

#include "catch_amalgamated.hpp"
#include "platform/SystemReboot.h"
#include "service/system/impl/PacketUpgrade.h"
#include "service/system/impl/SystemOperationServiceImpl.h"
#include "service/system/impl/UpgradeStorage.h"
#include "support/ScopedPathOverride.h"
#include "util/Exec.h"
#include "util/PathUtil.h"
#include "util/ResourceBudget.h"

namespace fs = std::filesystem;

namespace {

void CreateUpgradeLayout(const fs::path& root) {
    static constexpr std::array required_dirs{"bin", "files", "font", "lib", "scripts", "web"};
    for (const char* directory : required_dirs) {
        fs::create_directories(root / directory);
    }
    std::ofstream(root / "scripts" / "install.sh") << "#!/bin/sh\nexit 0\n";
}

fs::path AddUpgradeChecksumToName(const fs::path& archive, const fs::path& destination_dir,
                                  std::string_view version = "9.9.9") {
    std::string output;
    if (cosmo::util::Exec({"md5sum", archive.string()}, output) != 0) {
        return {};
    }
    std::istringstream stream(output);
    std::string md5;
    if (!(stream >> md5) || md5.size() != 32) {
        return {};
    }
    const auto destination = destination_dir / ("cosmo-V" + std::string(version) + "-" + md5 + ".tar.gz");
    std::error_code ec;
    fs::rename(archive, destination, ec);
    return ec ? fs::path{} : destination;
}

}  // namespace

TEST_CASE("System reboot support follows the inference backend", "[system][reboot]") {
#if defined(COSMO_NN_USE_SOPHON_BACKEND) || defined(COSMO_NN_USE_RKNN_BACKEND)
    CHECK(cosmo::platform::SupportsSystemReboot());
#else
    CHECK_FALSE(cosmo::platform::SupportsSystemReboot());
#endif
}

TEST_CASE("Factory reset preserves model authorization data", "[system][reset]") {
    const auto root        = fs::temp_directory_path() / "cosmo_factory_reset_test";
    const auto certificate = root / "model-guard" / "device-certificate.bin";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(certificate.parent_path());
    fs::create_directories(root / "conf");
    std::ofstream(certificate) << "certificate";
    std::ofstream(root / "conf" / "config.json") << "{}";
    std::ofstream(root / "temporary-file") << "temporary";

    CHECK_FALSE(cosmo::platform::ClearFactoryResetData(root.string()));
    CHECK(fs::is_regular_file(certificate));
    CHECK_FALSE(fs::exists(root / "conf"));
    CHECK_FALSE(fs::exists(root / "temporary-file"));
    fs::remove_all(root, ec);
}

TEST_CASE("SystemOperationServiceImpl: System operations", "[system][service]") {
    cosmo::service::SystemOperationServiceImpl sysOpSvc;

    SECTION("ExportLogs creates tar file successfully") {
        std::string testRoot = "/tmp/cosmo sysop;test";
        std::error_code cleanup_error;
        fs::remove_all(testRoot, cleanup_error);
        cosmo::test::ScopedPathOverride path_override(testRoot, testRoot);

        // Create directories that cosmo::path:: will return
        auto webDir = cosmo::path::GetWebLocalPath();
        auto logDir = cosmo::path::GetLogPath();
        auto cfgDir = cosmo::path::GetCfgPath();

        // Create some dummy old logs to be cleaned up
        std::ofstream(webDir + "/IedLog_old.tar").put('a');
        std::ofstream(logDir + "/dummy.log").put('b');
        std::ofstream(cfgDir + "/dummy.cfg").put('c');

        std::string fileName;
        std::string fileUrl;
        auto result = sysOpSvc.ExportLogs(fileName, fileUrl);

        REQUIRE(result == cosmo::util::ErrorEnum::Success);
        REQUIRE(fileName.find("IedLog") == 0);
        REQUIRE(fileName.find(".tar") != std::string::npos);
        const auto archive = fs::path(testRoot) / fs::path(fileUrl).relative_path();
        REQUIRE(fs::is_regular_file(archive));
        std::string listing;
        REQUIRE(cosmo::util::Exec({"tar", "-tf", archive.string()}, listing) == 0);
        REQUIRE(listing.find("dummy.log") != std::string::npos);
        REQUIRE(listing.find("dummy.cfg") != std::string::npos);

        // Cleanup
        fs::remove_all(testRoot);
    }
}

TEST_CASE("PacketUpgrade accepts cosmo tar.gz package names", "[system][upgrade]") {
    std::string md5sum;

    auto result = cosmo::UpgradeFileNameCheck("cosmo-V1.1.0-52d08574819464a735d4b0a90f26c924.tar.gz", md5sum);
    REQUIRE(result == cosmo::util::ErrorEnum::Success);
    REQUIRE(md5sum == "52d08574819464a735d4b0a90f26c924");

    result = cosmo::UpgradeFileNameCheck("cosmo-v1.1.0-52D08574819464A735D4B0A90F26C924.tar.gz", md5sum);
    REQUIRE(result == cosmo::util::ErrorEnum::Success);
    REQUIRE(md5sum == "52d08574819464a735d4b0a90f26c924");

    result = cosmo::UpgradeFileNameCheck("cosmo-V1.1.0-52d08574819464a735d4b0a90f26c924.mpkt", md5sum);
    REQUIRE(result == cosmo::util::ErrorEnum::UpgradeFileVerifyFailed);
}

TEST_CASE("PacketUpgrade rejects empty filename", "[system][upgrade]") {
    std::string md5sum;
    auto result = cosmo::UpgradeFileNameCheck("", md5sum);
    REQUIRE(result != cosmo::util::ErrorEnum::Success);
}

TEST_CASE("PacketUpgrade rejects random filename", "[system][upgrade]") {
    std::string md5sum;
    auto result = cosmo::UpgradeFileNameCheck("random.txt", md5sum);
    REQUIRE(result != cosmo::util::ErrorEnum::Success);
}

TEST_CASE("PacketUpgrade rejects missing md5", "[system][upgrade]") {
    std::string md5sum;
    auto result = cosmo::UpgradeFileNameCheck("cosmo-V1.0.0.tar.gz", md5sum);
    REQUIRE(result != cosmo::util::ErrorEnum::Success);
}

TEST_CASE("Upgrade storage requires two and a half package sizes", "[system][upgrade][storage]") {
    CHECK(cosmo::service::RequiredUpgradeSpaceBytes(0) == 0);
    CHECK(cosmo::service::RequiredUpgradeSpaceBytes(4) == 10);
    CHECK(cosmo::service::RequiredUpgradeSpaceBytes(5) == 13);
    CHECK(cosmo::service::RequiredUpgradeSpaceBytes(std::numeric_limits<std::uint64_t>::max()) ==
          std::numeric_limits<std::uint64_t>::max());
}

TEST_CASE("Upgrade storage cleanup removes only event images and videos", "[system][upgrade][storage]") {
    const auto root       = fs::temp_directory_path() / "cosmo_upgrade_storage_cleanup_test";
    const auto event_root = root / "event";
    const auto outside    = root / "outside.jpg";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(event_root / "2026/08/17");
    std::ofstream(event_root / "2026/08/17/alarm.jpg") << "image";
    std::ofstream(event_root / "2026/08/17/alarm.MP4") << "video";
    std::ofstream(event_root / "2026/08/17/alarm.json") << "metadata";
    std::ofstream(event_root / "2026/08/17/engine.log") << "log";
    std::ofstream(outside) << "outside";
    fs::create_symlink(outside, event_root / "2026/08/17/outside.jpg", ec);
    REQUIRE(!ec);

    const auto result = cosmo::service::DeleteEventMediaFiles(event_root);

    CHECK(result.error == cosmo::util::ErrorEnum::Success);
    CHECK(result.deleted_files == 2);
    CHECK(result.deleted_bytes == 10);
    CHECK_FALSE(fs::exists(event_root / "2026/08/17/alarm.jpg"));
    CHECK_FALSE(fs::exists(event_root / "2026/08/17/alarm.MP4"));
    CHECK(fs::is_regular_file(event_root / "2026/08/17/alarm.json"));
    CHECK(fs::is_regular_file(event_root / "2026/08/17/engine.log"));
    CHECK(fs::is_symlink(event_root / "2026/08/17/outside.jpg"));
    CHECK(fs::is_regular_file(outside));
    fs::remove_all(root, ec);
}

TEST_CASE("Upgrade storage rechecks available space after confirmed cleanup", "[system][upgrade][storage]") {
    const auto root       = fs::temp_directory_path() / "cosmo_upgrade_storage_recheck_test";
    const auto event_root = root / "event";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(event_root);
    std::ofstream(event_root / "alarm.jpg") << "image";
    std::ofstream(event_root / "alarm.json") << "metadata";

    cosmo::service::UpgradeSpaceStatus status;
    const auto inspect_result = cosmo::service::CheckUpgradeStorage(
        root, event_root, std::numeric_limits<std::uint64_t>::max(), false, status);
    REQUIRE(inspect_result == cosmo::util::ErrorEnum::Success);
    CHECK_FALSE(status.sufficient);
    CHECK(status.required_bytes == std::numeric_limits<std::uint64_t>::max());
    CHECK(status.event_media_bytes == 5);
    CHECK(fs::is_regular_file(event_root / "alarm.jpg"));

    const auto cleanup_result = cosmo::service::CheckUpgradeStorage(
        root, event_root, std::numeric_limits<std::uint64_t>::max(), true, status);
    REQUIRE(cleanup_result == cosmo::util::ErrorEnum::Success);
    CHECK_FALSE(status.sufficient);
    CHECK(status.deleted_media_files == 1);
    CHECK(status.deleted_media_bytes == 5);
    CHECK_FALSE(fs::exists(event_root / "alarm.jpg"));
    CHECK(fs::is_regular_file(event_root / "alarm.json"));
    fs::remove_all(root, ec);
}

TEST_CASE("PacketUpgrade validates archive boundaries before extraction", "[system][upgrade][archive]") {
    const auto root      = fs::temp_directory_path() / "cosmo_packet_upgrade_validation_test";
    const auto data_root = root / "data";
    const auto app_root  = root / "app";
    const auto staging   = root / "staging";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(staging);
    cosmo::test::ScopedPathOverride path_override(data_root.string(), app_root.string());

    SECTION("accepts a valid package with the expected layout") {
        const auto source = root / "valid_source";
        CreateUpgradeLayout(source);
        const auto unsigned_archive = staging / "valid.tar.gz";
        std::string output;
        REQUIRE(cosmo::util::Exec({"tar", "-czf", unsigned_archive.string(), "-C", source.string(), "."},
                                  output) == 0);
        const auto archive = AddUpgradeChecksumToName(unsigned_archive, staging);
        REQUIRE_FALSE(archive.empty());

        REQUIRE(cosmo::PacketUpgrade(archive) == cosmo::util::ErrorEnum::Success);
        const auto upgrade_root = fs::path(cosmo::path::GetUpgradePath());
        REQUIRE(fs::is_regular_file(upgrade_root / archive.filename()));
        REQUIRE(fs::is_directory(upgrade_root / "bin"));
        REQUIRE(fs::is_regular_file(upgrade_root / "scripts" / "install.sh"));
    }

    SECTION("rejects traversal without clearing the previous upgrade directory") {
        const auto source = root / "traversal_source";
        fs::create_directories(source);
        std::ofstream(source / "payload") << "unsafe";
        const auto unsigned_archive = staging / "traversal.tar.gz";
        std::string output;
        REQUIRE(cosmo::util::Exec({"tar", "-czf", unsigned_archive.string(), "--transform=s|^|../|", "-C",
                                   source.string(), "payload"},
                                  output) == 0);
        const auto archive = AddUpgradeChecksumToName(unsigned_archive, staging, "9.9.8");
        REQUIRE_FALSE(archive.empty());
        const auto upgrade_root = fs::path(cosmo::path::GetUpgradePath());
        std::ofstream(upgrade_root / "sentinel") << "keep";

        REQUIRE(cosmo::PacketUpgrade(archive) == cosmo::util::ErrorEnum::UpgradeFileVerifyFailed);
        REQUIRE(fs::is_regular_file(upgrade_root / "sentinel"));
        REQUIRE(fs::is_regular_file(archive));
        REQUIRE_FALSE(fs::exists(data_root / "payload"));
    }

    SECTION("rejects a package containing an unsafe symlink") {
        const auto source = root / "symlink_unsafe_source";
        CreateUpgradeLayout(source);
        // Target traverses above the archive root: must still be rejected even
        // though safe same-directory symlinks are now allowed.
        fs::create_symlink("../../etc/passwd", source / "bin" / "unsafe-link", ec);
        REQUIRE(!ec);
        const auto unsigned_archive = staging / "symlink_unsafe.tar.gz";
        std::string output;
        REQUIRE(cosmo::util::Exec({"tar", "-czf", unsigned_archive.string(), "-C", source.string(), "."},
                                  output) == 0);
        const auto archive = AddUpgradeChecksumToName(unsigned_archive, staging, "9.9.7");
        REQUIRE_FALSE(archive.empty());

        REQUIRE(cosmo::PacketUpgrade(archive) == cosmo::util::ErrorEnum::UpgradeFileVerifyFailed);
        REQUIRE(fs::is_regular_file(archive));
    }

    SECTION("accepts a package containing safe internal symlinks") {
        const auto source = root / "symlink_safe_source";
        CreateUpgradeLayout(source);
        // Shared-library versioning chain: libfoo.so -> libfoo.so.1 -> the real
        // file, all relative and confined to lib/. Must be accepted.
        std::ofstream(source / "lib" / "libfoo.so.1.0.0") << "payload";
        fs::create_symlink("libfoo.so.1.0.0", source / "lib" / "libfoo.so.1", ec);
        REQUIRE(!ec);
        fs::create_symlink("libfoo.so.1", source / "lib" / "libfoo.so", ec);
        REQUIRE(!ec);
        const auto unsigned_archive = staging / "symlink_safe.tar.gz";
        std::string output;
        REQUIRE(cosmo::util::Exec({"tar", "-czf", unsigned_archive.string(), "-C", source.string(), "."},
                                  output) == 0);
        const auto archive = AddUpgradeChecksumToName(unsigned_archive, staging, "9.9.4");
        REQUIRE_FALSE(archive.empty());

        REQUIRE(cosmo::PacketUpgrade(archive) == cosmo::util::ErrorEnum::Success);
        const auto upgrade_root = fs::path(cosmo::path::GetUpgradePath());
        REQUIRE(fs::is_symlink(upgrade_root / "lib" / "libfoo.so"));
        REQUIRE(fs::is_regular_file(upgrade_root / "lib" / "libfoo.so.1.0.0"));
    }

    SECTION("accepts a sparse member above the former fixed per-file ceiling when storage permits") {
        constexpr std::uintmax_t kFormerCeiling = 500ULL * 1024 * 1024;
        const auto budget = cosmo::util::InspectStorageResourceBudget(cosmo::path::GetUpgradePath());
        REQUIRE(budget.valid);
        if (budget.usable_bytes <= kFormerCeiling + 1024 * 1024) {
            WARN("Insufficient disposable storage budget to exercise the former 500 MiB boundary");
        } else {
            const auto source = root / "large_source";
            CreateUpgradeLayout(source);
            const auto sparse_file = source / "files" / "large-model.bin";
            {
                std::ofstream stream(sparse_file, std::ios::binary);
                REQUIRE(stream.good());
            }
            fs::resize_file(sparse_file, kFormerCeiling + 1, ec);
            REQUIRE(!ec);
            const auto unsigned_archive = staging / "large.tar.gz";
            std::string output;
            REQUIRE(cosmo::util::Exec(
                        {"tar", "--sparse", "-czf", unsigned_archive.string(), "-C", source.string(), "."},
                        output) == 0);
            const auto archive = AddUpgradeChecksumToName(unsigned_archive, staging, "9.9.6");
            REQUIRE_FALSE(archive.empty());

            REQUIRE(cosmo::PacketUpgrade(archive) == cosmo::util::ErrorEnum::Success);
            const auto upgrade_root = fs::path(cosmo::path::GetUpgradePath());
            REQUIRE(fs::file_size(upgrade_root / "files" / "large-model.bin") == kFormerCeiling + 1);
        }
    }

    SECTION("rejects an archive already placed inside the destructive output directory") {
        const auto source = root / "inside_source";
        CreateUpgradeLayout(source);
        const auto upgrade_root     = fs::path(cosmo::path::GetUpgradePath());
        const auto unsigned_archive = upgrade_root / "inside.tar.gz";
        std::string output;
        REQUIRE(cosmo::util::Exec({"tar", "-czf", unsigned_archive.string(), "-C", source.string(), "."},
                                  output) == 0);
        const auto archive = AddUpgradeChecksumToName(unsigned_archive, upgrade_root, "9.9.5");
        REQUIRE_FALSE(archive.empty());

        REQUIRE(cosmo::PacketUpgrade(archive) == cosmo::util::ErrorEnum::UpgradeFileVerifyFailed);
        REQUIRE(fs::is_regular_file(archive));
    }

    fs::remove_all(root, ec);
}

TEST_CASE("SystemOperationServiceImpl: ShowThreadDebugInfo does not crash", "[system][service]") {
    cosmo::service::SystemOperationServiceImpl sysOpSvc;
    REQUIRE_NOTHROW(sysOpSvc.ShowThreadDebugInfo());
}
