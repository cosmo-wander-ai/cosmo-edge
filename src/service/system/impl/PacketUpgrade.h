#pragma once

#include <chrono>
#include <filesystem>
#include <string>

#include "util/ErrorCode.h"

namespace cosmo {
namespace fs = std::filesystem;
util::ErrorEnum UpgradeFileNameCheck(std::string fileName, std::string& md5sum);
util::ErrorEnum PacketUpgrade(const fs::path& filePath);
}  // namespace cosmo
