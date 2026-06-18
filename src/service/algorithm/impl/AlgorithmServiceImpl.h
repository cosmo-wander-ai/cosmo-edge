#pragma once

#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "service/algorithm/IAlgorithmService.h"
#include "util/dto/AlgorithmPacketDto.h"

namespace cosmo::service {

/// Algorithm management service — handles CRUD operations and query for algorithm packets.
/// Loading/parsing logic is delegated to detail::AlgorithmPacketLoader.
/// Layout operations are delegated to cosmo::service::detail::AlgorithmLayoutMng.
class AlgorithmServiceImpl : public IAlgorithmService {
public:
    AlgorithmServiceImpl() = default;
    ~AlgorithmServiceImpl() override;

    void Init() override;

    cosmo::util::ErrorEnum Add(const std::string& filePath) override;
    cosmo::util::ErrorEnum Add(const algorithm::AlgorithmPacketInfo& modelCfg) override;
    cosmo::util::ErrorEnum AddFromJson(const std::string& algorithmName, int algorithmCategory,
                                       int algorithmUsage, const std::string& remark,
                                       const std::string& eventType, const std::string& filePath) override;

    // Layout configuration operations (delegated to AlgorithmLayoutMng)
    cosmo::util::ErrorEnum LayoutSave(const algorithm::LayoutSaveReq& req) override;
    cosmo::util::ErrorEnum GetLayoutDetail(const std::string& id, const std::string& filePath,
                                           algorithm::LayoutDetailResult& outResult) override;
    cosmo::util::ErrorEnum GetLayoutList(const std::string& supplier, int usage, const std::string& filePath,
                                         algorithm::LayoutListResult& outResult) override;
    cosmo::util::ErrorEnum LayoutExportSingle(const std::string& code, const std::string& name,
                                              const std::string& category, const std::string& supplier,
                                              const std::string& versionId,
                                              algorithm::LayoutExportResult& outResult) override;
    cosmo::util::ErrorEnum LayoutExportAll(const std::string& algorithmName, const std::string& supplier,
                                           const std::string& algorithmUsage,
                                           const std::string& algorithmCategory,
                                           const std::vector<std::string>& algorithmIds,
                                           algorithm::LayoutExportResult& outResult) override;
    cosmo::util::ErrorEnum GetAtomicActionList(int actionUsage, const std::string& filePath,
                                               algorithm::AtomicActionListResult& outResult) override;

    cosmo::util::ErrorEnum Delete(const std::string& algorithmId) override;
    cosmo::util::ErrorEnum Update(const std::string& algorithmId, const std::string& algorithmName,
                                  int algorithmCategory, const std::string& remark) override;

    std::vector<algorithm::AlgorithmPacketInfo> Query(const std::string& algorithmUsage,
                                                      const std::string& algorithmName,
                                                      const std::string& supplier,
                                                      const std::string& algorithmId,
                                                      const std::string& algorithmCategory, int pageNum,
                                                      int pageSize, size_t& total) override;

    cosmo::ActionAlgPtr GetAlgorithm(const std::string& algorithmId) override;
    std::string GetAlgorithmName(const std::string& algorithmId) override;
    std::string GetMetaData(const std::string& algorithmId) override;

    std::vector<std::string> GetAlgorithmsByModelId(const std::string& modelId) override;
    cosmo::util::ErrorEnum ReloadAlgorithmFromFile(const std::string& filePath) override;

    std::string GetAlgorithmPath() override;
    std::string GetActionsJsonPath() override;

    std::vector<algorithm::AlgorithmLocalInfo> GetPassFlowAlgorithms() override;

private:
    std::shared_mutex mtx_;
    std::unordered_map<std::string, algorithm::AlgorithmPacketInfo> algorithm_packets_;
};

}  // namespace cosmo::service
