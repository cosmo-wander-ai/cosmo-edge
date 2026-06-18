// License compatibility facade. CWAI no longer validates legacy .lic content.

#pragma once

#include <chrono>
#include <iosfwd>
#include <map>
#include <nlohmann/json_fwd.hpp>
#include <shared_mutex>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#include "util/DateTimeFormat.h"
#include "util/ErrorCode.h"

namespace cosmo::util {

struct ServiceDetail {
    int auth_count_{0};  // Authorized channels; device only checks if > 0
    int auth_day_{0};    // Authorized days
    int service_category_type_{0};
    int sub_service_type_{0};  // Fixed to 10000. Others are ignored.
    std::string sub_service_name_;
};

void to_json(nlohmann::json& j, const ServiceDetail& s);
void from_json(const nlohmann::json& j, ServiceDetail& s);

struct LicenseContent {
    std::string auth_date_;
    std::string device_sn_;
    std::vector<ServiceDetail> service_detail_;
};

void to_json(nlohmann::json& j, const LicenseContent& c);
void from_json(const nlohmann::json& j, LicenseContent& c);

struct LicenseServiceInfo {
    std::string auth_date_;
    bool has_bus_{false};
    int auth_day_{0};           // Authorized days
    int auth_count_{0};         // Authorized channels; device only checks if > 0
    util::YMDDate valid_date_;  // Expiration date

    std::string from_date_;  // Authorization start date
    std::string to_date_;    // Authorization end date
};

enum class LicenseStatus {
    kNone = 0,    // Unauthorized
    kSuccess,     // Successfully authorized
    kCountLimit,  // Insufficient authorized channels
    kOverDate     // Expired
};

struct LicenseServiceInterfaceInfo {
    bool is_valid_{false};
    std::string auth_date_;
    std::string valid_date_;
    int auth_day_{0};  // Authorized days
    // int auth_count_{0}; // Authorized channels; device only checks if > 0
};

/// License compatibility facade. Legacy .lic content is no longer decrypted or validated.
/// Owned by DeviceInfoServiceImpl; lifecycle managed through ServiceRegistry.
class LicenseManager {
public:
    /// @param license_sn  Device license serial number used for validation.
    /// @param license_path Directory path where the license file is located.
    explicit LicenseManager(std::string license_sn, std::string license_path);
    ~LicenseManager();

    LicenseManager(const LicenseManager&)            = delete;
    LicenseManager& operator=(const LicenseManager&) = delete;

    cosmo::util::ErrorEnum LicLoad();
    cosmo::util::ErrorEnum ReadLicFile(const std::string& file_name, LicenseServiceInfo* auth_info) const;
    std::string GetLicenseFileName() const {
        return license_file_name_;
    };

    LicenseServiceInterfaceInfo GetAuthInfo() const;

    LicenseStatus Status(std::string* date) const;
    std::string StatusString() const;
    bool IsServiceAuthed() const;

private:
    mutable std::shared_mutex mtx_;
    std::string license_sn_;
    std::string license_path_;
    std::string license_file_name_{"cwai.lic"};
    LicenseServiceInfo license_info_;
};
}  // namespace cosmo::util
