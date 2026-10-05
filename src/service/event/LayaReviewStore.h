#pragma once
#include <mutex>
#include <nlohmann/json.hpp>
#include <string>
namespace cosmo {
// A separate versioned audit store: existing alarm schema and cleanup are unchanged.
class LayaReviewStore {
public:
    explicit LayaReviewStore(std::string path);
    static LayaReviewStore& Instance();
    bool Begin(const nlohmann::json& record);
    bool Finish(const std::string& id, const nlohmann::json& result);
    bool Publication(const std::string& id, const std::string& status);
    nlohmann::json Page(const std::string& eventId, int page, int size);

private:
    void Init();
    std::string path_;
    std::mutex mutex_;
    bool initialized_{false};
};
}  // namespace cosmo
