#include "service/gb28181/impl/GbConfigStore.h"

#include <fcntl.h>
#include <openssl/rand.h>
#include <sys/stat.h>
#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <stdexcept>

#include "service/gb28181/impl/GbSipProtocol.h"
#include "util/CipherUtil.h"

namespace cosmo::service::gb {
namespace {
    std::string RandomBytes(size_t size) {
        std::string value(size, '\0');
        if (RAND_bytes(reinterpret_cast<unsigned char*>(value.data()), size) != 1)
            throw std::runtime_error("crypto_error");
        return value;
    }
    void Write(const std::string& path, const std::string& data) {
        const auto temporary = path + "." + RandomHex();
        const int fd = open(temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
        if (fd < 0)
            throw std::runtime_error("storage_error");
        size_t offset = 0;
        while (offset < data.size()) {
            const auto count = write(fd, data.data() + offset, data.size() - offset);
            if (count <= 0) {
                close(fd);
                unlink(temporary.c_str());
                throw std::runtime_error("storage_error");
            }
            offset += count;
        }
        const bool synced = fsync(fd) == 0;
        close(fd);
        if (!synced || rename(temporary.c_str(), path.c_str()) != 0) {
            unlink(temporary.c_str());
            throw std::runtime_error("storage_error");
        }
        const int parent =
            open(std::filesystem::path(path).parent_path().c_str(), O_DIRECTORY | O_RDONLY | O_CLOEXEC);
        if (parent >= 0) {
            fsync(parent);
            close(parent);
        }
    }
    std::string Read(const std::string& path) {
        if (std::filesystem::file_size(path) > 1024 * 1024)
            throw std::runtime_error("storage_error");
        std::ifstream input(path, std::ios::binary);
        if (!input)
            throw std::runtime_error("storage_error");
        return {std::istreambuf_iterator<char>(input), {}};
    }
}  // namespace
ConfigStore::ConfigStore(std::string directory) : directory_(std::move(directory)) {}
nlohmann::json ConfigStore::Load() {
    std::filesystem::create_directories(directory_);
    if (chmod(directory_.c_str(), 0700) != 0)
        throw std::runtime_error("storage_error");
    const auto path = directory_ + "/settings.json";
    if (std::filesystem::exists(directory_ + "/key"))
        key_ = Read(directory_ + "/key");
    else {
        if (std::filesystem::exists(path))
            throw std::runtime_error("storage_error");
        key_ = RandomBytes(32);
        Write(directory_ + "/key", key_);
    }
    if (key_.size() != 32)
        throw std::runtime_error("storage_error");
    if (!std::filesystem::exists(path))
        return nlohmann::json::object();
    const auto envelope  = nlohmann::json::parse(Read(path));
    const auto plaintext = util::DecAesGcmNoPadding(util::DecBase64(envelope.at("data")), key_,
                                                    util::DecBase64(envelope.at("nonce")));
    if (plaintext.empty())
        throw std::runtime_error("storage_error");
    return nlohmann::json::parse(plaintext);
}
void ConfigStore::Save(const nlohmann::json& config) const {
    if (key_.size() != 32)
        throw std::runtime_error("storage_error");
    const auto nonce = RandomBytes(12);
    const nlohmann::json envelope{
        {"version", 1},
        {"nonce", util::EncBase64(nonce)},
        {"data", util::EncBase64(util::EncAesGcmNoPadding(config.dump(), key_, nonce))}};
    Write(directory_ + "/settings.json", envelope.dump());
}
}  // namespace cosmo::service::gb
