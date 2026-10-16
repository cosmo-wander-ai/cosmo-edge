#pragma once

#include <filesystem>
#include <string>

namespace cosmo::service {

// Archive private, bounded copies of log/config files so active appends cannot
// invalidate tar. The temporary and destination directories must be on the same
// filesystem for atomic publication. Failure leaves no partial archive.
bool CreateLogArchive(const std::filesystem::path& log_path, const std::filesystem::path& config_path,
                      const std::filesystem::path& temporary_root, const std::filesystem::path& archive_path,
                      std::string& error);

}  // namespace cosmo::service
