#include "service/management/ManagementService.h"

#include <fcntl.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <fstream>
#include <set>

#include "SQLiteCpp/SQLiteCpp.h"

namespace cosmo::service {
namespace {
    using Json                      = nlohmann::json;
    namespace fs                    = std::filesystem;
    constexpr std::int64_t kMaxFile = 5LL * 1024 * 1024 * 1024;
    constexpr std::int64_t kReserve = 256LL * 1024 * 1024;
    constexpr std::size_t kChunk    = 1024 * 1024;

    void Require(bool condition, const char* code) {
        if (!condition) {
            throw ManagementError(code);
        }
    }

    std::string Hex(const unsigned char* data, std::size_t size) {
        const char* alphabet = "0123456789abcdef";
        std::string result;
        for (std::size_t i = 0; i < size; ++i) {
            result += alphabet[data[i] >> 4];
            result += alphabet[data[i] & 15];
        }
        return result;
    }

    std::string RandomId() {
        std::array<unsigned char, 16> bytes{};
        Require(RAND_bytes(bytes.data(), bytes.size()) == 1, "RANDOM_FAILED");
        return Hex(bytes.data(), bytes.size());
    }

    std::string Digest(std::istream& input) {
        auto ctx = std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)>(EVP_MD_CTX_new(), EVP_MD_CTX_free);
        Require(ctx && EVP_DigestInit_ex(ctx.get(), EVP_sha256(), nullptr) == 1, "HASH_FAILED");
        std::array<char, 65536> buffer{};
        while (input.read(buffer.data(), buffer.size()) || input.gcount()) {
            Require(EVP_DigestUpdate(ctx.get(), buffer.data(), input.gcount()) == 1, "HASH_FAILED");
        }
        Require(input.eof() && !input.bad(), "READ_FAILED");
        std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
        unsigned int size = 0;
        Require(EVP_DigestFinal_ex(ctx.get(), digest.data(), &size) == 1, "HASH_FAILED");
        return Hex(digest.data(), size);
    }

    bool IsHash(const std::string& value) {
        return value.size() == 64 && value.find_first_not_of("0123456789abcdef") == std::string::npos;
    }

    std::string Text(const Json& j, const char* key) {
        Require(j.contains(key) && j.at(key).is_string(), "INVALID_REQUEST");
        auto value = j.at(key).get<std::string>();
        Require(!value.empty() && value.size() <= 256, "INVALID_REQUEST");
        return value;
    }

    std::string Key(const Json& value) {
        return Json::array({Text(value, "externalId"), Text(value, "versionId")}).dump();
    }

    std::int64_t Now() {
        return std::chrono::duration_cast<std::chrono::seconds>(
                   std::chrono::system_clock::now().time_since_epoch())
            .count();
    }

    void Sync(const fs::path& path) {
        int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
        Require(fd >= 0, "STORAGE_FAILED");
        int result = ::fsync(fd);
        ::close(fd);
        Require(result == 0, "STORAGE_FAILED");
    }

    Json Result(const Json& operation) {
        return operation.at("result");
    }
}  // namespace

ManagementService::ManagementService(fs::path root, std::unique_ptr<IManagedResources> resources)
    : root_(std::move(root)), resources_(std::move(resources)) {
    Require(resources_ != nullptr, "MISSING_NATIVE_ADAPTER");
}

ManagementService::~ManagementService() = default;

void ManagementService::Open() {
    if (db_) {
        return;
    }
    fs::create_directories(root_ / "blobs");
    fs::create_directories(root_ / "uploads");
    fs::permissions(root_, fs::perms::owner_all, fs::perm_options::replace);
    db_ = std::make_unique<SQLite::Database>((root_ / "state.sqlite").string(),
                                             SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
    db_->exec("PRAGMA journal_mode=WAL");
    db_->exec("PRAGMA synchronous=FULL");
    db_->exec("CREATE TABLE IF NOT EXISTS journal(id INTEGER PRIMARY KEY CHECK(id=1), body TEXT NOT NULL)");
    SQLite::Statement read(*db_, "SELECT body FROM journal WHERE id=1");
    if (read.executeStep()) {
        state_ = Json::parse(read.getColumn(0).getString());
        Require(state_.at("schema") == 1, "UNSUPPORTED_STATE_VERSION");
    } else {
        state_ = {{"schema", 1},
                  {"incarnation", RandomId()},
                  {"platform", ""},
                  {"principal", ""},
                  {"nextId", 7000000},
                  {"resources", Json::object()},
                  {"operations", Json::object()},
                  {"uploads", Json::object()}};
        Save();
        Sync(root_);
    }
}

void ManagementService::Save() {
    SQLite::Statement write(*db_, "INSERT OR REPLACE INTO journal(id,body) VALUES(1,?)");
    write.bind(1, state_.dump());
    write.exec();
}

void ManagementService::CheckOwner(const std::string& principal, const std::string& platform) {
    Require(!principal.empty(), "AUTHENTICATED_HTTP_REQUIRED");
    Require(state_["principal"] == "" || state_["principal"] == principal, "OWNER_CONFLICT");
    Require(platform.empty() || state_["platform"] == "" || state_["platform"] == platform,
            "PLATFORM_CONFLICT");
}

bool ManagementService::BlobValid(const std::string& hash, std::int64_t size) {
    auto path = root_ / "blobs" / hash;
    if (!fs::is_regular_file(path) || fs::file_size(path) != static_cast<std::uint64_t>(size)) {
        return false;
    }
    std::ifstream input(path, std::ios::binary);
    return Digest(input) == hash;
}

Json ManagementService::Prepare(const Json& request, const std::string& principal) {
    auto hash = Text(request, "hash");
    Require(IsHash(hash) && request.contains("size") && request["size"].is_number_integer(), "INVALID_FILE");
    auto size = request["size"].get<std::int64_t>();
    Require(size >= 0 && size <= kMaxFile, "INVALID_FILE_SIZE");
    Text(request, "name");  // Display metadata only; never a local path.
    if (BlobValid(hash, size)) {
        return {{"complete", true}, {"hash", hash}};
    }
    auto& uploads         = state_["uploads"];
    std::size_t count     = 0;
    std::int64_t reserved = 0;
    for (auto it = uploads.begin(); it != uploads.end();) {
        if (it.value()["expiresAt"].get<std::int64_t>() < Now()) {
            fs::remove(root_ / "uploads" / it.key());
            it = uploads.erase(it);
            continue;
        }
        auto& u = it.value();
        if (u["principal"] == principal && u["hash"] == hash && u["size"] == size) {
            auto path           = root_ / "uploads" / it.key();
            std::int64_t offset = fs::exists(path) ? static_cast<std::int64_t>(fs::file_size(path)) : 0;
            Require(offset <= size, "UPLOAD_CORRUPTED");
            if (!fs::exists(path)) {
                std::ofstream output(path, std::ios::binary);
                output.close();
                Require(!output.fail(), "STORAGE_FAILED");
                Sync(path);
                Sync(root_ / "uploads");
            }
            u["expiresAt"] = Now() + 86400;
            Save();
            return {{"uploadId", it.key()}, {"offset", offset}};
        }
        if (u["principal"] == principal && !u.value("complete", false)) {
            ++count;
        }
        auto path    = root_ / "uploads" / it.key();
        auto written = fs::exists(path) ? static_cast<std::int64_t>(fs::file_size(path)) : 0;
        if (!u.value("complete", false)) {
            reserved += std::max<std::int64_t>(0, u["size"].get<std::int64_t>() - written);
        }
        ++it;
    }
    Require(count < 8, "UPLOAD_QUOTA");
    Require(fs::space(root_).available >= static_cast<std::uint64_t>(size + reserved + kReserve),
            "STORAGE_RESERVE_REACHED");
    auto id   = RandomId();
    auto path = root_ / "uploads" / id;
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    Require(output.good(), "STORAGE_FAILED");
    output.close();
    Sync(path);
    if (size == 0) {
        std::ifstream input(path, std::ios::binary);
        Require(Digest(input) == hash, "HASH_MISMATCH");
        fs::rename(path, root_ / "blobs" / hash);
        Sync(root_ / "blobs");
        return {{"complete", true}, {"hash", hash}};
    }
    uploads[id] = {{"principal", principal}, {"hash", hash}, {"size", size}, {"expiresAt", Now() + 86400}};
    Save();
    return {{"uploadId", id}, {"offset", 0}};
}

Json ManagementService::Chunk(const RequestDispatchContext& context, const std::string& body) {
    Require(!body.empty() && body.size() <= kChunk, "INVALID_CHUNK_SIZE");
    auto& uploads = state_["uploads"];
    Require(uploads.contains(context.upload_id), "UPLOAD_NOT_FOUND");
    auto& upload = uploads.at(context.upload_id);
    Require(upload["principal"] == context.principal, "UPLOAD_OWNER_CONFLICT");
    Require(upload["expiresAt"].get<std::int64_t>() >= Now(), "UPLOAD_EXPIRED");
    Require(!context.upload_offset.empty() && context.upload_offset.size() <= 12 &&
                context.upload_offset.find_first_not_of("0123456789") == std::string::npos,
            "INVALID_OFFSET");
    auto offset   = std::stoll(context.upload_offset);
    auto size     = upload["size"].get<std::int64_t>();
    auto hash     = upload["hash"].get<std::string>();
    auto path     = root_ / "uploads" / context.upload_id;
    bool complete = BlobValid(hash, size);
    if (complete) {
        path = root_ / "blobs" / hash;
    }
    auto current = static_cast<std::int64_t>(fs::file_size(path));
    Require(offset <= current && offset + static_cast<std::int64_t>(body.size()) <= size, "OFFSET_MISMATCH");
    if (offset < current || complete) {
        Require(offset + static_cast<std::int64_t>(body.size()) <= current, "OFFSET_MISMATCH");
        std::ifstream input(path, std::ios::binary);
        input.seekg(offset);
        std::string existing(body.size(), '\0');
        input.read(existing.data(), existing.size());
        Require(input.gcount() == static_cast<std::streamsize>(body.size()) && existing == body,
                "CHUNK_CONFLICT");
    } else {
        Require(fs::space(root_).available >= body.size() + kReserve, "STORAGE_RESERVE_REACHED");
        std::ofstream output(path, std::ios::binary | std::ios::app);
        output.write(body.data(), body.size());
        output.close();
        Require(!output.fail(), "STORAGE_FAILED");
        Sync(path);
        current += body.size();
    }
    Json result = {{"offset", current}};
    if (current == size) {
        if (!complete) {
            std::ifstream input(path, std::ios::binary);
            if (Digest(input) != hash) {
                fs::remove(path);
                uploads.erase(context.upload_id);
                Save();
                throw ManagementError("HASH_MISMATCH");
            }
            fs::rename(path, root_ / "blobs" / hash);
            Sync(root_ / "blobs");
            Sync(root_ / "uploads");
        }
        result["hash"]     = hash;
        upload["complete"] = true;
    }
    upload["expiresAt"] = Now() + 86400;
    Save();
    return result;
}

Json ManagementService::Inventory(const Json& request) {
    Require(request.contains("resourceIds") && request["resourceIds"].is_array() &&
                request["resourceIds"].size() <= 500,
            "INVALID_RESOURCE_IDS");
    auto rows = Json::array();
    for (const auto& r : state_["resources"]) {
        if (!request["resourceIds"].empty() &&
            std::find(request["resourceIds"].begin(), request["resourceIds"].end(), r["externalId"]) ==
                request["resourceIds"].end()) {
            continue;
        }
        Json row = {{"externalId", r["externalId"]}, {"versionId", r["versionId"]}, {"hash", r["hash"]},
                    {"localId", r["localId"]},       {"kind", r["kind"]},           {"state", r["state"]}};
        if (r["state"] == "READY") {
            auto actual = ReadNative(r);
            try {
                CheckReferences(r);
            } catch (const ManagementError&) {
                actual["state"] = "DEPENDENCY_UNAVAILABLE";
            }
            row["state"] = actual.value("state", "MISSING");
            if (r["kind"] == "task") {
                row["runtimeState"] = actual.value("runtimeState", "unknown");
            }
        }
        rows.push_back(std::move(row));
    }
    return {{"incarnation", state_["incarnation"]}, {"resources", rows}};
}

Json ManagementService::ReadNative(const Json& resource) {
    auto actual = resources_->Inspect(resource);
    if (actual.value("state", "") == "READY" && resource.contains("nativeFingerprint") &&
        actual.value("fingerprint", "") != resource["nativeFingerprint"]) {
        actual["state"] = "DRIFTED";
    }
    return actual;
}

void ManagementService::CheckReferences(const Json& request, int depth) {
    Require(depth <= 64, "DEPENDENCY_DEPTH_LIMIT");
    Require(request["references"].is_array() && request["references"].size() <= 500, "INVALID_REFERENCES");
    std::set<std::string> seen;
    for (const auto& ref : request["references"]) {
        auto key = Key(ref);
        Require(seen.insert(key).second, "DUPLICATE_REFERENCE");
        Require(state_["resources"].contains(key), "DEPENDENCY_MISSING");
        auto& stored = state_["resources"][key];
        Require(stored["state"] == "READY" && stored["kind"] == ref.at("kind") &&
                    stored["localId"] == ref.at("localId"),
                "DEPENDENCY_MISMATCH");
        Require(ReadNative(stored).value("state", "") == "READY", "DEPENDENCY_UNAVAILABLE");
        CheckReferences(stored, depth + 1);
    }
}

std::string ManagementService::ReserveId(const std::string& kind) {
    for (int attempt = 0; attempt < 1000000; ++attempt) {
        auto number = state_["nextId"].get<std::int64_t>();
        Require(number < 10000000, "LOCAL_ID_EXHAUSTED");
        state_["nextId"] = number + 1;
        auto id          = kind == "channel" || kind == "schedule" ? RandomId() : std::to_string(number);
        if (!resources_->Exists(kind == "task" ? "scene" : kind, id)) {
            return id;
        }
    }
    throw ManagementError("LOCAL_ID_EXHAUSTED");
}

Json ManagementService::Operation(const std::string& method, const Json& request,
                                  const std::string& principal) {
    auto id          = Text(request, "operationId");
    auto& operations = state_["operations"];
    if (operations.contains(id)) {
        auto& op = operations[id];
        Require(op["principal"] == principal && op["method"] == method && op["request"] == request,
                "OPERATION_CONFLICT");
        return op["result"]["state"] == "PENDING" ? Execute(id) : Result(op);
    }
    for (const auto& op : operations) {
        Require(op["result"]["state"] != "PENDING", "RECOVERY_REQUIRED");
    }
    Require(request.at("incarnation") == state_["incarnation"], "INCARNATION_MISMATCH");
    auto key = Key(request);
    if (method == "applyoperation") {
        auto platform = Text(request, "platformId");
        CheckOwner(principal, platform);
        Require(
            request.at("expiresAt").is_number_integer() && request["expiresAt"].get<std::int64_t>() >= Now(),
            "OPERATION_EXPIRED");
        auto kind = Text(request, "kind");
        Require(
            kind == "model" || kind == "scene" || kind == "channel" || kind == "schedule" || kind == "task",
            "UNSUPPORTED_KIND");
        auto action = Text(request, "action");
        Require(action == "apply" || action == "remove", "UNSUPPORTED_ACTION");
        Require(IsHash(Text(request, "hash")), "INVALID_HASH");
        if (action == "apply") {
            Require(request.at("config").is_object() && request.at("files").is_array(), "INVALID_CONFIG");
            CheckReferences(request);
            for (const auto& file : request["files"]) {
                auto hash = Text(file, "hash");
                Require(IsHash(hash) && file.at("size").is_number_integer() &&
                            file["size"].get<std::int64_t>() >= 0 &&
                            BlobValid(hash, file["size"].get<std::int64_t>()),
                        "FILE_UNVERIFIED");
                Text(file, "name");
            }
            Require(kind != "task" || request.at("activationPolicy") == "prepare", "PREPARE_REQUIRED");
            if (state_["resources"].contains(key)) {
                const auto& old = state_["resources"][key];
                for (const char* field : {"hash", "kind", "name", "config", "files", "references"}) {
                    Require(old.at(field) == request.at(field), "VERSION_CONFLICT");
                }
            } else {
                Json resource = request;
                resource.erase("executionLayout");
                resource.erase("executionId");
                resource.erase("nativeFingerprint");
                resource["localId"] = ReserveId(kind);
                resource["state"]   = "PREPARING";
                // A task's private execution scene isolates version-specific parameters.
                if (kind == "task") {
                    resource["executionId"] = resource["localId"];
                    for (const auto& ref : request["references"]) {
                        if (ref["kind"] == "scene") {
                            resource["executionLayout"] = state_["resources"][Key(ref)]["config"];
                        }
                    }
                }
                state_["resources"][key] = std::move(resource);
            }
        } else {
            for (const auto& resource : state_["resources"]) {
                for (const auto& ref : resource["references"]) {
                    Require(Key(ref) != key, "RESOURCE_IN_USE");
                }
            }
            if (state_["resources"].contains(key)) {
                Require(state_["resources"][key]["kind"] == kind &&
                            state_["resources"][key]["hash"] == request["hash"],
                        "VERSION_CONFLICT");
            }
        }
        state_["platform"]  = platform;
        state_["principal"] = principal;
    } else {
        Require(!state_["platform"].get<std::string>().empty(), "PLATFORM_NOT_BOUND");
        Require(request.at("enabled").is_boolean(), "INVALID_ENABLED");
        Require(state_["resources"].contains(key) && state_["resources"][key]["kind"] == "task" &&
                    (!request["enabled"].get<bool>() || state_["resources"][key]["state"] == "READY"),
                "TASK_NOT_PREPARED");
    }
    operations[id] = {{"method", method},
                      {"request", request},
                      {"principal", principal},
                      {"result", {{"state", "PENDING"}}}};
    Save();  // Intent and native ID must reach durable storage before any effect.
    return Execute(id);
}

Json ManagementService::Execute(const std::string& id) {
    auto& op           = state_["operations"].at(id);
    const auto request = op.at("request");
    auto key           = Key(request);
    try {
        if (op["method"] == "taskactivate") {
            auto& target = state_["resources"].at(key);
            if (request["enabled"].get<bool>()) {
                CheckReferences(target);
                Require(ReadNative(target).value("state", "") == "READY", "TASK_NOT_PREPARED");
            }
            // Stop every old execution first. An interrupted switch resumes from
            // this journal entry and can never leave two versions enabled.
            for (auto& resource : state_["resources"]) {
                if (resource["kind"] == "task" && resource["externalId"] == request["externalId"] &&
                    (Key(resource) != key || !request["enabled"].get<bool>())) {
                    resources_->Activate(resource, false);
                }
            }
            resources_->Activate(target, request["enabled"].get<bool>());
            const auto actual  = ReadNative(target);
            const auto runtime = actual.value("runtimeState", "unknown");
            if (request["enabled"].get<bool>() && runtime == "failed") {
                resources_->Activate(target, false);
                throw ManagementError("TASK_START_FAILED");
            }
            if ((request["enabled"].get<bool>() && runtime != "running" && runtime != "scheduled") ||
                (!request["enabled"].get<bool>() && runtime != "stopped" &&
                 actual.value("state", "") != "MISSING")) {
                Save();
                return Result(op);
            }
        } else if (request["action"] == "remove") {
            if (state_["resources"].contains(key)) {
                resources_->Remove(state_["resources"][key]);
                state_["resources"].erase(key);
            }
        } else {
            auto& resource = state_["resources"].at(key);
            CheckReferences(request);
            Require(request.at("expiresAt").get<std::int64_t>() >= Now() ||
                        ReadNative(resource).value("state", "") == "READY",
                    "OPERATION_EXPIRED");
            if (resource["state"] != "READY" || ReadNative(resource).value("state", "") != "READY") {
                resources_->Apply(resource, root_ / "blobs");
            }
            const auto actual = ReadNative(resource);
            Require(actual.value("state", "") == "READY", "NATIVE_READBACK_FAILED");
            if (actual.contains("fingerprint"))
                resource["nativeFingerprint"] = actual["fingerprint"];
            resource["state"] = "READY";
        }
        op["result"] = {{"state", "SUCCEEDED"}};
    } catch (const Json::exception&) {
        op["result"] = {{"state", "FAILED"}, {"error", "INVALID_NATIVE_CONFIG"}};
        if (op["method"] == "applyoperation" && request["action"] == "apply" &&
            state_["resources"].contains(key)) {
            state_["resources"][key]["state"] = "FAILED";
        }
    } catch (const ManagementError& error) {
        op["result"] = {{"state", "FAILED"}, {"error", error.what()}};
        if (op["method"] == "applyoperation" && request["action"] == "apply" &&
            state_["resources"].contains(key)) {
            state_["resources"][key]["state"] = "FAILED";
        }
    }
    // Unexpected storage/process errors leave the operation PENDING for recovery.
    Save();
    return Result(op);
}

Json ManagementService::Handle(const std::string& method, const RequestDispatchContext& context,
                               const std::string& body) {
    std::lock_guard lock(mutex_);
    Require(context.transport == RequestTransport::kHttp && !context.principal.empty(),
            "AUTHENTICATED_HTTP_REQUIRED");
    Require(context.http_method == (method == "uploadchunk" ? "PUT" : "POST"), "METHOD_NOT_ALLOWED");
    try {
        Open();
        CheckOwner(context.principal);
        if (method == "uploadchunk") {
            return Chunk(context, body);
        }
        Require(body.size() <= 4 * 1024 * 1024, "REQUEST_TOO_LARGE");
        auto request = Json::parse(body);
        Require(request.is_object(), "INVALID_REQUEST");
        if (method == "capabilities") {
            auto result                  = resources_->DeviceFacts();
            result["managementProtocol"] = 1;
            result["incarnation"]        = state_["incarnation"];
            result["features"]           = {"idempotent-apply", "resource-inventory", "versioned-resources",
                                            "staged-task"};
            for (const auto& feature : result.value("nativeFeatures", Json::array()))
                result["features"].push_back(feature);
            result.erase("nativeFeatures");
            return result;
        }
        if (method == "prepareresource") {
            return Prepare(request, context.principal);
        }
        if (method == "resourceinventory") {
            CheckOwner(context.principal, Text(request, "platformId"));
            return Inventory(request);
        }
        if (method == "operationstatus") {
            auto id = Text(request, "operationId");
            if (!state_["operations"].contains(id)) {
                return {{"state", "NOT_FOUND"}};
            }
            auto& op = state_["operations"][id];
            Require(op["principal"] == context.principal, "OWNER_CONFLICT");
            return op["result"]["state"] == "PENDING" ? Execute(id) : Result(op);
        }
        Require(method == "applyoperation" || method == "taskactivate", "UNKNOWN_METHOD");
        return Operation(method, request, context.principal);
    } catch (...) {
        db_.reset();
        state_ = Json();
        throw;
    }
}
}  // namespace cosmo::service
