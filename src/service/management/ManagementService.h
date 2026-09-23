#pragma once

#include <filesystem>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>

#include "nlohmann/json.hpp"
#include "util/IRequestDispatcher.h"

namespace SQLite {
class Database;
}

namespace cosmo::service {

class ManagementError : public std::runtime_error {
public:
    explicit ManagementError(const std::string& code) : std::runtime_error(code) {}
};

// The adapter owns native validation and readback. IDs are reserved durably before
// invoking it. Apply/Remove/Activate must be safe to repeat after a process crash.
class IManagedResources {
public:
    virtual ~IManagedResources()                                                     = default;
    virtual nlohmann::json DeviceFacts()                                             = 0;
    virtual bool Exists(const std::string& kind, const std::string& local_id)        = 0;
    virtual void Apply(nlohmann::json& resource, const std::filesystem::path& blobs) = 0;
    virtual void Remove(const nlohmann::json& resource)                              = 0;
    virtual nlohmann::json Inspect(const nlohmann::json& resource)                   = 0;
    virtual void Activate(const nlohmann::json& resource, bool enabled)              = 0;
};

class IManagementService {
public:
    virtual ~IManagementService()                          = default;
    virtual nlohmann::json Handle(const std::string& method, const RequestDispatchContext& context,
                                  const std::string& body) = 0;
};

// A single durable journal couples operation identity, version reservations and
// upload ownership. Native effects are reconciled from PENDING on retry; SQLite
// success alone is never reported as a usable engine resource.
class ManagementService final : public IManagementService {
public:
    ManagementService(std::filesystem::path root, std::unique_ptr<IManagedResources> resources);
    ~ManagementService() override;
    nlohmann::json Handle(const std::string& method, const RequestDispatchContext& context,
                          const std::string& body) override;

private:
    void Open();
    void Save();
    void CheckOwner(const std::string& principal, const std::string& platform = {});
    nlohmann::json Prepare(const nlohmann::json& request, const std::string& principal);
    nlohmann::json Chunk(const RequestDispatchContext& context, const std::string& body);
    nlohmann::json Inventory(const nlohmann::json& request);
    nlohmann::json Operation(const std::string& method, const nlohmann::json& request,
                             const std::string& principal);
    nlohmann::json Execute(const std::string& operation_id);
    std::string ReserveId(const std::string& kind);
    void CheckReferences(const nlohmann::json& request, int depth = 0);
    nlohmann::json ReadNative(const nlohmann::json& resource);
    bool BlobValid(const std::string& hash, std::int64_t size);

    std::filesystem::path root_;
    std::unique_ptr<IManagedResources> resources_;
    std::unique_ptr<SQLite::Database> db_;
    nlohmann::json state_;
    std::mutex mutex_;
};

std::unique_ptr<IManagedResources> MakeNativeManagedResources();

}  // namespace cosmo::service
