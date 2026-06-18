// License compatibility facade. CWAI no longer validates legacy .lic content.

#include "util/LicenseManager.h"

#include <filesystem>

#include "util/ErrorCode.h"
#include "util/JsonStructUtil.h"
#include "util/Log.h"

namespace cosmo::util {

LicenseManager::LicenseManager(std::string license_sn, std::string license_path)
    : license_sn_(std::move(license_sn)), license_path_(std::move(license_path)) {
    LicLoad();
}

LicenseManager::~LicenseManager() {}

cosmo::util::ErrorEnum LicenseManager::ReadLicFile(const std::string& file_name,
                                                   LicenseServiceInfo* auth_info) const {
    if (!auth_info)
        return cosmo::util::ErrorEnum::FileAnalysisFailed;
    *auth_info          = LicenseServiceInfo{};
    auth_info->has_bus_ = true;
    LOG_INFO("Skip legacy license content validation for {}", file_name);
    return cosmo::util::ErrorEnum::Success;
}

cosmo::util::ErrorEnum LicenseManager::LicLoad() {
    auto file_name = (std::filesystem::path(license_path_) / GetLicenseFileName()).string();
    LicenseServiceInfo auth_info;
    auto ret = ReadLicFile(file_name, &auth_info);
    if (ret != cosmo::util::ErrorEnum::Success) {
        return ret;
    }

    std::lock_guard<std::shared_mutex> lock(mtx_);
    license_info_ = auth_info;
    LOG_INFO("{}", "Legacy license validation disabled");
    return cosmo::util::ErrorEnum::Success;
}

LicenseStatus LicenseManager::Status(std::string* /*date*/) const {
    // NOTE: License validation currently disabled — always returns success.
    return LicenseStatus::kSuccess;
}

std::string LicenseManager::StatusString() const {
    std::string date;
    LicenseStatus status = Status(&date);
    switch (status) {
        case LicenseStatus::kNone:
            return "未授权";
        case LicenseStatus::kSuccess:
            return "已授权 " + date;
        case LicenseStatus::kOverDate:
            return "授权过期";
        case LicenseStatus::kCountLimit:
            return "授权数量不够";
        default:
            return "未授权";
    }
}

bool LicenseManager::IsServiceAuthed() const {
    // NOTE: License validation currently disabled — always returns true.
    return true;
}

LicenseServiceInterfaceInfo LicenseManager::GetAuthInfo() const {
    LicenseServiceInterfaceInfo info;
    info.is_valid_ = IsServiceAuthed();
    std::shared_lock<std::shared_mutex> lock(mtx_);
    info.auth_date_  = license_info_.from_date_;
    info.valid_date_ = license_info_.to_date_;
    info.auth_day_   = license_info_.auth_day_;

    return info;
}

}  // namespace cosmo::util

#include <nlohmann/json.hpp>

#include "util/LimitedTypeJson.h"

// Auto-generated JSON serialization
namespace cosmo::util {
void to_json(nlohmann::json& j, const ServiceDetail& s) {
    j = nlohmann::json{{"authCount", s.auth_count_},
                       {"authDay", s.auth_day_},
                       {"serviceCategoryType", s.service_category_type_},
                       {"subServiceType", s.sub_service_type_},
                       {"subServiceName", s.sub_service_name_}};
}

void from_json(const nlohmann::json& j, ServiceDetail& s) {
    if (j.contains("authCount") && !j["authCount"].is_null())
        j.at("authCount").get_to(s.auth_count_);
    if (j.contains("authDay") && !j["authDay"].is_null())
        j.at("authDay").get_to(s.auth_day_);
    if (j.contains("serviceCategoryType") && !j["serviceCategoryType"].is_null())
        j.at("serviceCategoryType").get_to(s.service_category_type_);
    if (j.contains("subServiceType") && !j["subServiceType"].is_null())
        j.at("subServiceType").get_to(s.sub_service_type_);
    if (j.contains("subServiceName") && !j["subServiceName"].is_null())
        j.at("subServiceName").get_to(s.sub_service_name_);
}

void to_json(nlohmann::json& j, const LicenseContent& c) {
    j = nlohmann::json{
        {"authDate", c.auth_date_}, {"deviceSn", c.device_sn_}, {"serviceDetail", c.service_detail_}};
}

void from_json(const nlohmann::json& j, LicenseContent& c) {
    if (j.contains("authDate") && !j["authDate"].is_null())
        j.at("authDate").get_to(c.auth_date_);
    if (j.contains("deviceSn") && !j["deviceSn"].is_null())
        j.at("deviceSn").get_to(c.device_sn_);
    if (j.contains("serviceDetail") && !j["serviceDetail"].is_null())
        j.at("serviceDetail").get_to(c.service_detail_);
}

}  // namespace cosmo::util
