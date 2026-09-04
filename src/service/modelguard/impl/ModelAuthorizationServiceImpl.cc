#include "service/modelguard/impl/ModelAuthorizationServiceImpl.h"

#include <filesystem>
#include <utility>

#include "util/Exec.h"
#include "util/PathUtil.h"
#include "util/UuidUtil.h"

namespace cosmo::service {
namespace {
    constexpr std::uintmax_t kDeviceRequestSize = 48;
    constexpr std::uintmax_t kCertificateSize   = 236;

    std::string CertificateStoreDirectory() {
        return (std::filesystem::path(path::GetBaseDir()) / "model-guard").string();
    }
}  // namespace

ModelAuthorizationServiceImpl::ModelAuthorizationServiceImpl(std::string provision_tool)
    : provision_tool_(std::move(provision_tool)) {}

bool ModelAuthorizationServiceImpl::ToolAvailable() const {
    std::error_code ec;
    const auto status = std::filesystem::status(provision_tool_, ec);
    return !ec && std::filesystem::is_regular_file(status) &&
           (status.permissions() & std::filesystem::perms::owner_exec) != std::filesystem::perms::none;
}

ModelAuthorizationStatus ModelAuthorizationServiceImpl::Status() {
    if (!ToolAvailable()) {
        return {};
    }
    std::string output;
    const int result =
        util::Exec({provision_tool_, "status", "--store-dir", CertificateStoreDirectory()}, output);
    if (result == 0 && output.rfind("valid ", 0) == 0) {
        return {true, true, "valid"};
    }
    for (const auto* state : {"certificate_unavailable", "certificate_rejected", "device_mismatch",
                              "identity_rejected", "resource_failure"}) {
        if (output.find(state) != std::string::npos) {
            return {true, false, state};
        }
    }
    return {true, false, "unknown"};
}

util::ErrorEnum ModelAuthorizationServiceImpl::CreateDeviceRequest(std::string& file_path,
                                                                   std::string& file_name) {
    file_path.clear();
    file_name.clear();
    if (!ToolAvailable()) {
        return util::ErrorEnum::OperationNotSupport;
    }
    const auto candidate = std::filesystem::path(path::GetTemporaryDirPath()) /
                           ("model-authorization-request-" + util::GenerateUUID() + ".cmpr");
    std::string output;
    if (util::Exec({provision_tool_, "request", "--output", candidate.string()}, output) != 0) {
        return util::ErrorEnum::Failed;
    }
    std::error_code ec;
    if (!std::filesystem::is_regular_file(candidate, ec) || ec ||
        std::filesystem::file_size(candidate, ec) != kDeviceRequestSize || ec) {
        std::filesystem::remove(candidate, ec);
        return util::ErrorEnum::FileAnalysisFailed;
    }
    file_path = candidate.string();
    file_name = "device-request.cmpr";
    return util::ErrorEnum::Success;
}

util::ErrorEnum ModelAuthorizationServiceImpl::InstallCertificate(const std::string& file_path) {
    if (!ToolAvailable()) {
        return util::ErrorEnum::OperationNotSupport;
    }
    std::error_code ec;
    if (!std::filesystem::is_regular_file(file_path, ec) || ec ||
        std::filesystem::file_size(file_path, ec) != kCertificateSize || ec) {
        return util::ErrorEnum::FileAnalysisFailed;
    }
    std::string output;
    return util::Exec({provision_tool_, "install", "--certificate", file_path, "--store-dir",
                       CertificateStoreDirectory()},
                      output) == 0
               ? util::ErrorEnum::Success
               : util::ErrorEnum::Failed;
}

}  // namespace cosmo::service
