/// @file ILicenseManagement.h
/// @brief License management interface — device info export and license
///        upload/validation.
///        ISP split from IDeviceInfoService.
///        Consumed by api/MessageSystemHandlerOps for license upload/download.
#pragma once

#include <string>

#include "util/ErrorCode.h"

namespace cosmo::service {

/// License authorization status DTO.
struct LicenseAuthInfo {
    bool bValid{false};     ///< Whether the license is valid.
    int authDay{0};         ///< Number of authorized days remaining.
    std::string authDate;   ///< Authorization start date string.
    std::string validDate;  ///< License expiry date string.
};

/// Manages device license operations: exporting device identity for
/// license generation and uploading license files for activation.
class ILicenseManagement {
public:
    virtual ~ILicenseManagement() = default;

    /// Export device identification data for offline license generation.
    /// @param fileName [out] Generated file name.
    /// @param fileUrl  [out] Web-accessible URL to download the file.
    /// @return ErrorEnum::kSuccess on success.
    virtual cosmo::util::ErrorEnum DownloadDeviceInfo(std::string& fileName, std::string& fileUrl) = 0;

    /// Upload and activate a license file.
    /// @param filePath Path to the uploaded license file.
    /// @return ErrorEnum::kSuccess on success.
    virtual cosmo::util::ErrorEnum UploadLicense(std::string filePath) = 0;

    /// Get the current license authorization status.
    /// @return License auth info with validity, dates, and remaining days.
    virtual LicenseAuthInfo GetAuthServiceStatus() = 0;
};

}  // namespace cosmo::service
