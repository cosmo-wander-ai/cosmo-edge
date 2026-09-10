#include "service/onvif/impl/OnvifServiceImpl.h"

#include <curl/curl.h>
#include <fcntl.h>
#include <openssl/rand.h>
#include <sys/stat.h>
#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <stdexcept>

#include "util/CipherUtil.h"
#include "util/Log.h"
#include "util/PathUtil.h"
#include "util/UuidUtil.h"

namespace cosmo::service {
namespace {
    using Json = nlohmann::json;
    std::string Random(size_t size) {
        std::string value(size, '\0');
        if (RAND_bytes(reinterpret_cast<unsigned char*>(value.data()), value.size()) != 1)
            throw std::runtime_error("crypto_error");
        return value;
    }
    void AtomicWrite(const std::string& path, const std::string& data) {
        const auto temp = path + "." + util::GenerateUUID();
        const int fd    = open(temp.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
        if (fd < 0)
            throw std::runtime_error("storage_error");
        size_t offset = 0;
        while (offset < data.size()) {
            const auto count = write(fd, data.data() + offset, data.size() - offset);
            if (count <= 0) {
                close(fd);
                unlink(temp.c_str());
                throw std::runtime_error("storage_error");
            }
            offset += count;
        }
        const bool synced = fsync(fd) == 0;
        close(fd);
        if (!synced || rename(temp.c_str(), path.c_str()) != 0) {
            unlink(temp.c_str());
            throw std::runtime_error("storage_error");
        }
        const auto parent   = std::filesystem::path(path).parent_path().string();
        const int directory = open(parent.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        if (directory >= 0) {
            fsync(directory);
            close(directory);
        }
    }
    Json PublicConfig(const onvif::Config& c) {
        return {{"endpoint", c.endpoint},
                {"username", c.username},
                {"profileToken", c.profileToken},
                {"videoSourceToken", c.videoSourceToken},
                {"uuid", c.uuid},
                {"hasPassword", !c.password.empty()},
                {"rtspUsername", c.rtspUsername},
                {"separateRtspCredentials", c.separateRtspCredentials},
                {"hasRtspPassword", !c.rtspPassword.empty()}};
    }
    struct Slot {
        std::atomic<int>& active;
        explicit Slot(std::atomic<int>& value) : active(value) {
            if (active.fetch_add(1) >= 4) {
                --active;
                throw std::runtime_error("busy");
            }
        }
        ~Slot() {
            --active;
        }
    };
    void ReadField(const Json& request, const char* key, std::string& value, size_t maximum = 1024) {
        if (request.contains(key))
            value = request.at(key).get<std::string>();
        if (value.size() > maximum || value.find('\0') != std::string::npos)
            throw std::runtime_error("invalid_parameter");
    }
}  // namespace

void OnvifServiceImpl::Init() {
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK)
        throw std::runtime_error("network_error");
    directory_ = path::GetCfgPath("onvif");
    std::filesystem::create_directories(directory_);
    chmod(directory_.c_str(), 0700);
    const auto key_path = directory_ + "/key";
    if (std::filesystem::exists(key_path)) {
        std::ifstream file(key_path, std::ios::binary);
        key_.assign(std::istreambuf_iterator<char>(file), {});
        if (key_.size() != 32)
            throw std::runtime_error("ONVIF credential key is invalid");
    } else {
        if (std::filesystem::exists(directory_ + "/sources.json"))
            throw std::runtime_error("ONVIF credential key is missing; restore configuration backup");
        key_ = Random(32);
        AtomicWrite(key_path, key_);
    }
    const auto sources = directory_ + "/sources.json";
    if (!std::filesystem::exists(sources)) {
        ready_ = true;
        return;
    }
    if (std::filesystem::file_size(sources) > 4 * 1024 * 1024)
        throw std::runtime_error("storage_error");
    std::ifstream file(sources);
    const auto root = Json::parse(file);
    if (!root.is_object() || root.size() > 256)
        throw std::runtime_error("storage_error");
    std::map<std::string, Entry> loaded;
    for (const auto& item : root.items()) {
        const auto& j = item.value();
        Entry entry;
        auto& c                   = entry.config;
        c.endpoint                = j.at("endpoint");
        c.username                = j.at("username");
        c.profileToken            = j.at("profileToken");
        c.videoSourceToken        = j.value("videoSourceToken", "");
        c.uuid                    = j.value("uuid", "");
        c.rtspUsername            = j.value("rtspUsername", "");
        c.separateRtspCredentials = j.value("separateRtspCredentials", false);
        const auto nonce          = util::DecBase64(j.at("nonce"));
        const auto secrets        = util::DecAesGcmNoPadding(util::DecBase64(j.at("secrets")), key_, nonce);
        if (secrets.empty())
            throw std::runtime_error("ONVIF credential decryption failed");
        const auto credentials = Json::parse(secrets);
        c.password             = credentials.at("password");
        c.rtspPassword         = credentials.value("rtspPassword", "");
        loaded.emplace(item.key(), std::move(entry));
    }
    entries_ = std::move(loaded);
    ready_   = true;
}
onvif::Config OnvifServiceImpl::ReadRequest(const Json& request, const std::string& source) const {
    onvif::Config config;
    if (!source.empty()) {
        std::lock_guard<std::mutex> lock(entries_mtx_);
        const auto it = entries_.find(source);
        if (it == entries_.end())
            throw std::runtime_error("source_not_found");
        config = it->second.config;
    }
    ReadField(request, "endpoint", config.endpoint, 2048);
    ReadField(request, "username", config.username, 256);
    ReadField(request, "password", config.password);
    ReadField(request, "profileToken", config.profileToken, 256);
    ReadField(request, "videoSourceToken", config.videoSourceToken, 256);
    ReadField(request, "uuid", config.uuid, 256);
    ReadField(request, "rtspUsername", config.rtspUsername, 256);
    ReadField(request, "rtspPassword", config.rtspPassword);
    config.separateRtspCredentials = request.value("separateRtspCredentials", config.separateRtspCredentials);
    config.endpoint                = onvif::NormalizeEndpoint(config.endpoint);
    if (config.username.empty())
        throw std::runtime_error("invalid_parameter");
    return config;
}
Json OnvifServiceImpl::Interfaces() const {
    return onvif::Interfaces();
}
Json OnvifServiceImpl::Discover(const std::string& address, int timeoutMs) {
    Slot slot(active_requests_);
    return onvif::Discover(address, timeoutMs);
}
Json OnvifServiceImpl::Probe(const Json& request) {
    Slot slot(active_requests_);
    auto config = ReadRequest(request, request.value("source", ""));
    return onvif::Client(config).Probe();
}
void OnvifServiceImpl::Persist(const std::map<std::string, Entry>& entries) const {
    if (key_.size() != 32)
        throw std::runtime_error("storage_error");
    Json root = Json::object();
    for (const auto& item : entries) {
        const auto& c      = item.second.config;
        auto j             = PublicConfig(c);
        const auto nonce   = Random(12);
        const Json secrets = {{"password", c.password}, {"rtspPassword", c.rtspPassword}};
        j["nonce"]         = util::EncBase64(nonce);
        j["secrets"]       = util::EncBase64(util::EncAesGcmNoPadding(secrets.dump(), key_, nonce));
        root[item.first]   = std::move(j);
    }
    AtomicWrite(directory_ + "/sources.json", root.dump(2));
}
std::string OnvifServiceImpl::Save(const Json& request, const std::string& source) {
    if (!ready_)
        throw std::runtime_error("storage_error");
    // Serialize configuration transactions only; readers and network work never take writer_mtx_.
    std::lock_guard<std::mutex> writer(writer_mtx_);
    auto config = ReadRequest(request, source);
    if (config.profileToken.empty())
        throw std::runtime_error("no_profile_selected");
    const auto id = source.empty() ? "onvif://" + util::GenerateUUID() : source;
    std::map<std::string, Entry> candidate;
    {
        std::lock_guard<std::mutex> lock(entries_mtx_);
        candidate = entries_;
    }
    if (source.empty() && candidate.size() >= 256)
        throw std::runtime_error("source_limit");
    for (const auto& item : candidate) {
        if (item.first != id && item.second.config.endpoint == config.endpoint &&
            item.second.config.profileToken == config.profileToken)
            throw std::runtime_error("duplicate_source");
    }
    Entry entry;
    entry.config   = std::move(config);
    entry.revision = candidate.count(id) ? candidate.at(id).revision + 1 : 1;
    candidate[id]  = entry;
    Persist(candidate);
    {
        std::lock_guard<std::mutex> lock(entries_mtx_);
        entries_[id] = std::move(entry);
    }
    return id;
}
Json OnvifServiceImpl::Describe(const std::string& source) const {
    std::lock_guard<std::mutex> lock(entries_mtx_);
    const auto it = entries_.find(source);
    if (it == entries_.end())
        throw std::runtime_error("source_not_found");
    auto j               = PublicConfig(it->second.config);
    j["source"]          = source;
    j["connectionError"] = it->second.error;
    return j;
}
void OnvifServiceImpl::Remove(const std::string& source) {
    if (!ready_)
        throw std::runtime_error("storage_error");
    std::lock_guard<std::mutex> writer(writer_mtx_);
    std::map<std::string, Entry> candidate;
    {
        std::lock_guard<std::mutex> lock(entries_mtx_);
        candidate = entries_;
    }
    if (!candidate.erase(source))
        return;
    Persist(candidate);
    {
        std::lock_guard<std::mutex> lock(entries_mtx_);
        entries_.erase(source);
    }
}
uint64_t OnvifServiceImpl::Revision(const std::string& source) const {
    std::lock_guard<std::mutex> lock(entries_mtx_);
    const auto it = entries_.find(source);
    return it == entries_.end() ? 0 : it->second.revision;
}
OnvifSourceResult OnvifServiceImpl::Resolve(const std::string& source, const std::atomic<bool>& running) {
    OnvifSourceResult result;
    onvif::Config config;
    {
        std::lock_guard<std::mutex> lock(entries_mtx_);
        auto it = entries_.find(source);
        if (it == entries_.end()) {
            result.error = "source_not_found";
            return result;
        }
        auto& entry     = it->second;
        result.revision = entry.revision;
        if (!entry.uri.empty()) {
            result.mediaUrl = entry.uri;
            return result;
        }
        if (entry.resolving || std::chrono::steady_clock::now() < entry.retryAfter) {
            result.error = entry.error.empty() ? "busy" : entry.error;
            return result;
        }
        entry.resolving = true;
        config          = entry.config;
    }
    try {
        Slot slot(active_requests_);
        result.mediaUrl = onvif::Client(config, &running).StreamUri();
    } catch (const std::exception& error) {
        result.error = error.what();
    }
    {
        std::lock_guard<std::mutex> lock(entries_mtx_);
        auto it = entries_.find(source);
        if (it == entries_.end() || it->second.revision != result.revision || !running.load()) {
            if (it != entries_.end() && it->second.revision == result.revision)
                it->second.resolving = false;
            return {{}, "configuration_changed", 0};
        }
        auto& entry     = it->second;
        entry.resolving = false;
        entry.uri       = result.mediaUrl;
        entry.error     = result.error;
        entry.retryAfter =
            std::chrono::steady_clock::now() + std::chrono::seconds(result.error == "unauthorized" ? 60 : 10);
    }
    return result;
}
void OnvifServiceImpl::Invalidate(const std::string& source) {
    std::lock_guard<std::mutex> lock(entries_mtx_);
    auto it = entries_.find(source);
    if (it == entries_.end() || it->second.uri.empty())
        return;
    it->second.uri.clear();
    it->second.retryAfter = std::chrono::steady_clock::now() + std::chrono::seconds(10);
}
}  // namespace cosmo::service
