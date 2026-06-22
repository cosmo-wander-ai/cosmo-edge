// Linkage service implementation — manages alarm output device
// strategies with async alarm dispatch.

#pragma once

#include <nlohmann/json_fwd.hpp>
#include <shared_mutex>
#include <string>
#include <vector>

#include "linkage/LinkAgeBaseCommon.h"
#include "linkage/LinkAgeTask.h"
#include "service/infra/ILinkageService.h"
#include "util/AsyncQueue.h"

namespace cosmo {
struct LinkageAlarm {
    std::string channel_id;
    std::string alg_id;
};

struct LinkageStrategyConfigUnit {
    bool is_open{true};  // Enabled on creation
    std::string strategy_id;
    std::string name;
    int64_t timestamp{0};
    int64_t create_timestamp{0};

    cosmo::linkage::LinkageStrategyWorkflow
        strategy;  // Parsed strategy content (not persisted; saved separately on dispatch)
    cosmo::linkage::LinkAgeTaskPtr task{nullptr};  // Strategy task
    friend void to_json(nlohmann::json& j, const LinkageStrategyConfigUnit& v);
    friend void from_json(const nlohmann::json& j, LinkageStrategyConfigUnit& v);
};

struct LinkageStrategyConfig {
    std::vector<LinkageStrategyConfigUnit> strategies;
    friend void to_json(nlohmann::json& j, const LinkageStrategyConfig& v);
    friend void from_json(const nlohmann::json& j, LinkageStrategyConfig& v);
};
}  // namespace cosmo

namespace cosmo::service {

class LinkageServiceImpl : public ILinkageService {
public:
    LinkageServiceImpl();
    ~LinkageServiceImpl() override;

    cosmo::util::ErrorEnum Add(const std::string& name, const std::string& work_flow,
                               std::string& id) override;
    cosmo::util::ErrorEnum Delete(std::string& id) override;
    cosmo::util::ErrorEnum Update(const std::string& name, const std::string& id,
                                  const std::string& work_flow) override;
    std::vector<cosmo::LinkageStrategyOutputUnit> Query(int page_num, int page_size, const std::string& name,
                                                        size_t& total) override;
    cosmo::util::ErrorEnum Switch(std::string& id, bool enable) override;
    bool ReadSupportedStorage(int& total_size, std::vector<cosmo::StorageList>& vec_data) override;

    bool IsAudioDeviceInUse(const std::string& audio_device_id) override;
    bool IsAudioFileInUse(const std::string& audio_file_id) override;

    bool Alarm(const std::string& channel_id, const std::string& alg_id) override;

private:
    void LoadConfig();
    bool LoadSupportedStorageFromJson(const std::string& file_path);
    void SaveConfig();
    void SaveStrategy(const std::string& id, const std::string& linkage_strategy);
    void RmvStrategy(const std::string& id);
    std::string ReadStrategy(const std::string& id);

    cosmo::linkage::LinkAgeTaskPtr MakeTask(const std::string& name,
                                            cosmo::linkage::LinkageStrategyWorkflow& strategy);

    void DoAlarm(const std::string& channel_id, const std::string& alg_id);

    mutable std::shared_mutex mtx_;
    std::string conf_file_path_{"linkAge"};
    std::string conf_file_name_{"linkAgeList.json"};
    cosmo::LinkageStrategyConfig config_;
    std::vector<cosmo::StorageList> support_storages_;

    cosmo::AsyncQueue<cosmo::LinkageAlarm> async_queue_;
};

}  // namespace cosmo::service
