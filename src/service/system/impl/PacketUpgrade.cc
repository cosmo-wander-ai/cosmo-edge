// PacketUpgrade — Packet Upgrade implementation.

#include "service/system/impl/PacketUpgrade.h"

#include <unistd.h>

#include <array>
#include <fstream>
#include <regex>
#include <string>

#include "service/detail/ServiceRegistry.h"

#define CRYPTOPP_ENABLE_NAMESPACE_WEAK 1
#include "cryptopp/cryptlib.h"
#include "cryptopp/files.h"
#include "cryptopp/hex.h"
#include "cryptopp/md5.h"
#include "util/ErrorCode.h"
#include "util/Exec.h"
#include "util/PathUtil.h"
#include "util/StringUtil.h"

namespace cosmo {

namespace {

    std::string Md5SumFile(const std::string& filePath) {
        try {
            CryptoPP::Weak::MD5 hash;
            CryptoPP::byte digest[CryptoPP::Weak::MD5::DIGESTSIZE];

            // NOLINT: CryptoPP pipeline takes ownership
            CryptoPP::FileSource file(
                filePath.c_str(), true,
                new CryptoPP::HashFilter(hash, new CryptoPP::ArraySink(digest, sizeof(digest))));

            std::string hex_digest;
            CryptoPP::HexEncoder encoder;
            // NOLINT: CryptoPP pipeline takes ownership
            encoder.Attach(new CryptoPP::StringSink(hex_digest));
            encoder.Put(digest, sizeof(digest));
            encoder.MessageEnd();

            return hex_digest;
        } catch (const CryptoPP::Exception& e) {
            std::cerr << "CryptoPP exception: " << e.what() << std::endl;
            return "";
        } catch (const std::exception& e) {
            std::cerr << "Standard exception: " << e.what() << std::endl;
            return "";
        }
    }

    bool HasExpectedPackageLayout(const fs::path& packageRoot) {
        static constexpr std::array requiredDirs{"bin", "files", "font", "lib", "scripts", "web"};
        std::error_code ec;
        for (const char* dir : requiredDirs) {
            if (!fs::is_directory(packageRoot / dir, ec)) {
                LOG_ERRO("upgrade package missing required directory: {}", (packageRoot / dir).string());
                return false;
            }
        }
        return true;
    }

    bool FindExpectedPackageLayout(const fs::path& extractRoot, fs::path& packageRoot) {
        if (HasExpectedPackageLayout(extractRoot)) {
            packageRoot = extractRoot;
            return true;
        }

        std::error_code ec;
        fs::path candidate;
        int dir_count = 0;
        for (const auto& entry : fs::directory_iterator(extractRoot, ec)) {
            if (entry.is_directory(ec)) {
                candidate = entry.path();
                ++dir_count;
            }
        }

        if (dir_count == 1 && HasExpectedPackageLayout(candidate)) {
            packageRoot = candidate;
            return true;
        }

        return false;
    }

}  // namespace

// Filename format: cosmo-V<major>.<minor>.<patch>-<md5>.tar.gz
util::ErrorEnum UpgradeFileNameCheck(std::string file_name, std::string& md5sum) {
    std::smatch matchResult;
    // Match: cosmo-V1.0.0-<32-char-md5>.tar.gz
    static const std::regex regExp(R"(^cosmo-[Vv]([0-9]+\.[0-9]+\.[0-9]+)-([0-9a-fA-F]{32})\.tar\.gz$)");
    if (!regex_match(file_name, matchResult, regExp)) {
        LOG_ERRO("upgrade file name not match: {}", file_name);
        return util::ErrorEnum::UpgradeFileVerifyFailed;
    }

    std::string file_version = matchResult[1].str();
    md5sum                   = util::ToLower(matchResult[2].str());

    LOG_INFO("Upgrade package: version={}, md5={}", file_version, md5sum);
    return util::ErrorEnum::Success;
}

util::ErrorEnum PacketUpgrade(const fs::path& filePath) {
    fs::path upgradeFileDir(cosmo::path::GetUpgradePath());
    std::error_code ec;
    remove_all(upgradeFileDir, ec);
    if (!create_directories(upgradeFileDir, ec)) {
        LOG_ERRO("cannot create directory {}", upgradeFileDir);
        return util::ErrorEnum::FileOpenFailed;
    }

    std::string file_name = filePath.filename();
    std::string file_name_md5sum;
    auto ret = UpgradeFileNameCheck(file_name, file_name_md5sum);
    if (ret != util::ErrorEnum::Success) {
        return ret;
    }

    auto upgrade_file_name = upgradeFileDir / filePath.filename();
    rename(filePath, upgrade_file_name, ec);
    LOG_INFO("{} -> {} result: {}", filePath.c_str(), upgrade_file_name.c_str(), ec.message());
    if (ec) {
        remove_all(upgradeFileDir, ec);
        return util::ErrorEnum::FileMoveFailed;
    }

    // Check MD5
    auto md5Str = Md5SumFile(upgrade_file_name);
    if (md5Str.empty() || (util::ToLower(md5Str) != file_name_md5sum)) {
        LOG_ERRO("upgrade package md5 error, expected={}, actual={}", file_name_md5sum, md5Str);
        remove_all(upgradeFileDir, ec);
        return util::ErrorEnum::UpgradeFileNotMatch;
    }

    // Extract tar.gz
    std::string extract_cmd = fmt::format("tar -xzf {} -C {}", util::ShellEscape(upgrade_file_name.string()),
                                          util::ShellEscape(upgradeFileDir.string()));
    std::string output;
    int exit_code = util::Exec(extract_cmd, output);
    if (exit_code != 0) {
        LOG_ERRO("extract upgrade package failed: {}", output);
        remove_all(upgradeFileDir, ec);
        return util::ErrorEnum::UpgradeFileVerifyFailed;
    }
    LOG_INFO("upgrade package extracted to {}", upgradeFileDir.c_str());

    fs::path packageRoot;
    if (!FindExpectedPackageLayout(upgradeFileDir, packageRoot)) {
        remove_all(upgradeFileDir, ec);
        return util::ErrorEnum::UpgradeFileVerifyFailed;
    }
    LOG_INFO("upgrade package layout root: {}", packageRoot.string());

    // Caller (SystemOperationServiceImpl::Upgrade) is responsible for
    // triggering a reboot after this function returns successfully.
    return util::ErrorEnum::Success;
}

}  // namespace cosmo
