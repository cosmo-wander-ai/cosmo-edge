#pragma once
#include <nlohmann/json.hpp>
#include <string>

namespace cosmo::service::gb {
// Private, atomic encrypted configuration. The key and ciphertext must be backed up together.
class ConfigStore {
public:
    explicit ConfigStore(std::string directory);
    nlohmann::json Load();
    void Save(const nlohmann::json& config) const;

private:
    std::string directory_, key_;
};
}  // namespace cosmo::service::gb
