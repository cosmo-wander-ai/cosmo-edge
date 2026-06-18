// ModelPathMapper — manages model algorithm code ↔ directory path mappings.
// Thread-safe. Extracted from ModelServiceImpl to reduce class size.
#pragma once

#include <map>
#include <shared_mutex>
#include <string>

namespace cosmo::service {

class ModelPathMapper {
public:
    ModelPathMapper() = default;

    ModelPathMapper(const ModelPathMapper&)            = delete;
    ModelPathMapper& operator=(const ModelPathMapper&) = delete;

    void Set(const std::string& algCode, const std::string& modelPath);
    std::string Get(const std::string& algCode);

    /// Resolve model config path and model file path for a given algorithm code.
    bool GetModelCfg(const std::string& algCode, std::string& cfgPath, std::string& modelPath);

    /// Overload that also resolves a word dictionary file (.txt).
    bool GetModelCfg(const std::string& algCode, std::string& cfgPath, std::string& modelPath,
                     std::string& wordDictPath);

private:
    std::shared_mutex mtx_;
    std::map<std::string, std::string> map_;
};

}  // namespace cosmo::service
