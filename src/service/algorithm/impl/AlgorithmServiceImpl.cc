// AlgorithmServiceImpl — Algorithm Service Impl implementation.

#include "service/algorithm/impl/AlgorithmServiceImpl.h"

#include <algorithm>
#include <filesystem>
#include <unordered_set>

#include "nlohmann/json.hpp"
#include "service/algorithm/IActionService.h"
#include "service/algorithm/impl/AlgorithmCodeGenerator.h"
#include "service/algorithm/impl/AlgorithmJsonCodec.h"
#include "service/algorithm/impl/AlgorithmLayoutMng.h"
#include "service/algorithm/impl/AlgorithmPacketLoader.h"
#include "service/algorithm/impl/AlgorithmValidator.h"
#include "service/detail/ServiceRegistry.h"
#include "service/model/IModelService.h"
#include "util/DateTimeFormat.h"
#include "util/FileUtil.h"
#include "util/JsonFileUtil.h"
#include "util/Log.h"
#include "util/PaginationHelper.h"
#include "util/PathUtil.h"

namespace cosmo::service {

namespace {

    bool IsSamePath(const std::string& lhs, const std::string& rhs) {
        if (lhs.empty() || rhs.empty())
            return lhs == rhs;

        std::error_code lhs_ec;
        std::error_code rhs_ec;
        auto lhs_path = std::filesystem::weakly_canonical(std::filesystem::path(lhs), lhs_ec);
        auto rhs_path = std::filesystem::weakly_canonical(std::filesystem::path(rhs), rhs_ec);
        if (lhs_ec)
            lhs_path = std::filesystem::absolute(std::filesystem::path(lhs), lhs_ec).lexically_normal();
        if (rhs_ec)
            rhs_path = std::filesystem::absolute(std::filesystem::path(rhs), rhs_ec).lexically_normal();
        return lhs_path == rhs_path;
    }

}  // namespace

// ---------------------------------------------------------------------------
// Layout delegates (pure forwarding to AlgorithmLayoutMng)
// ---------------------------------------------------------------------------

cosmo::util::ErrorEnum AlgorithmServiceImpl::LayoutSave(const algorithm::LayoutSaveReq& req) {
    return detail::AlgorithmLayoutMng::LayoutSave(req);
}

cosmo::util::ErrorEnum AlgorithmServiceImpl::GetLayoutDetail(const std::string& id,
                                                             const std::string& filePath,
                                                             algorithm::LayoutDetailResult& outResult) {
    return detail::AlgorithmLayoutMng::GetLayoutDetail(id, filePath, outResult);
}

cosmo::util::ErrorEnum AlgorithmServiceImpl::GetLayoutList(const std::string& supplier, int usage,
                                                           const std::string& filePath,
                                                           algorithm::LayoutListResult& outResult) {
    return detail::AlgorithmLayoutMng::GetLayoutList(supplier, usage, filePath, outResult);
}

cosmo::util::ErrorEnum AlgorithmServiceImpl::LayoutExportSingle(
    const std::string& code, const std::string& name, const std::string& category,
    const std::string& supplier, const std::string& versionId, algorithm::LayoutExportResult& outResult) {
    return detail::AlgorithmLayoutMng::LayoutExportSingle(code, name, category, supplier, versionId,
                                                          outResult);
}

cosmo::util::ErrorEnum AlgorithmServiceImpl::LayoutExportAll(const std::string& algorithmName,
                                                             const std::string& supplier,
                                                             const std::string& algorithmUsage,
                                                             const std::string& algorithmCategory,
                                                             const std::vector<std::string>& algorithmIds,
                                                             algorithm::LayoutExportResult& outResult) {
    return detail::AlgorithmLayoutMng::LayoutExportAll(algorithmName, supplier, algorithmUsage,
                                                       algorithmCategory, algorithmIds, outResult);
}

cosmo::util::ErrorEnum AlgorithmServiceImpl::GetAtomicActionList(
    int actionUsage, const std::string& filePath, algorithm::AtomicActionListResult& outResult) {
    return detail::AlgorithmLayoutMng::GetAtomicActionList(actionUsage, filePath, outResult);
}

// ---------------------------------------------------------------------------
// Path accessors (delegates to PathService)
// ---------------------------------------------------------------------------

std::string AlgorithmServiceImpl::GetAlgorithmPath() {
    return cosmo::path::GetAlgorithmPath();
}

std::string AlgorithmServiceImpl::GetActionsJsonPath() {
    return cosmo::path::GetActionsJsonPath();
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void AlgorithmServiceImpl::Init() {
    auto zip_packets = detail::AlgorithmPacketLoader::LoadFromZipDirectory(cosmo::path::GetAlgorithmPath());
    for (auto& pkt : zip_packets) {
        if (!pkt.id.empty()) {
            algorithm_packets_.insert_or_assign(pkt.id, std::move(pkt));
        }
    }

    // Load from JSON layout directory and merge new algorithms (I/O outside lock)
    auto loaded_packets =
        detail::AlgorithmPacketLoader::LoadFromJsonDirectory(cosmo::path::GetAlgorithmPath());
    {
        std::lock_guard<std::shared_mutex> lock(mtx_);
        for (auto& loaded : loaded_packets) {
            if (algorithm_packets_.find(loaded.id) == algorithm_packets_.end()) {
                algorithm_packets_.emplace(loaded.id, std::move(loaded));
            }
        }
    }

    LOG_INFO("AlgorithmServiceImpl Init, total algorithms: {}", algorithm_packets_.size());
}

AlgorithmServiceImpl::~AlgorithmServiceImpl() {
    LOG_INFO("{}", "AlgorithmServiceImpl destroyed");
}

// ---------------------------------------------------------------------------
// CRUD operations
// ---------------------------------------------------------------------------

cosmo::util::ErrorEnum AlgorithmServiceImpl::Add(const std::string& filePath) {
    auto unzip_file = detail::AlgorithmPacketLoader::UnzipPackageFile(filePath);
    if (unzip_file.empty()) {
        LOG_INFO("Unzip {} Failed", filePath);
        return cosmo::util::ErrorEnum::UnZipFileFailed;
    }
    auto algs = detail::AlgorithmPacketLoader::LoadFromZipDirectory(unzip_file);
    for (auto& alg : algs) {
        auto temp = cosmo::util::FileMove(alg.filePath, cosmo::path::GetAlgorithmPath());
        if (!temp) {
            LOG_WARN("Move {} to file {} Failed", alg.filePath, cosmo::path::GetAlgorithmPath());
            return cosmo::util::ErrorEnum::FileMoveFailed;
        }
        alg.filePath =
            (std::filesystem::path(cosmo::path::GetAlgorithmPath()) / cosmo::util::GetFileName(alg.filePath))
                .string();
        Add(alg);
    }

    return cosmo::util::ErrorEnum::Success;
}

cosmo::util::ErrorEnum AlgorithmServiceImpl::Add(const algorithm::AlgorithmPacketInfo& modelCfg) {
    std::lock_guard<std::shared_mutex> lock(mtx_);
    auto it = algorithm_packets_.find(modelCfg.id);
    if (it != algorithm_packets_.end()) {
        if (!IsSamePath(it->second.filePath, modelCfg.filePath)) {
            cosmo::util::RemoveFile(it->second.filePath);
        }
        it->second = modelCfg;
    } else {
        algorithm_packets_.emplace(modelCfg.id, modelCfg);
    }
    return cosmo::util::ErrorEnum::Success;
}

cosmo::util::ErrorEnum AlgorithmServiceImpl::Delete(const std::string& algorithmId) {
    std::lock_guard<std::shared_mutex> lock(mtx_);
    auto it = algorithm_packets_.find(algorithmId);
    if (it != algorithm_packets_.end()) {
        // Delete the algorithm JSON file from the layout directory
        const std::string algorithm_layout_dir = cosmo::path::GetAlgorithmPath();
        std::string fp = cosmo::util::FindPrefixedJsonFile(algorithm_layout_dir, algorithmId);
        if (!fp.empty()) {
            if (remove(fp.c_str()) == 0)
                LOG_INFO("Deleted algorithm file: {}", fp);
        }
        // Delete the ZIP package if present
        cosmo::util::RemovePath(it->second.filePath);
        algorithm_packets_.erase(it);
        return cosmo::util::ErrorEnum::Success;
    }
    return cosmo::util::ErrorEnum::AlgorithmNotExist;
}

cosmo::util::ErrorEnum AlgorithmServiceImpl::Update(const std::string& algorithmId,
                                                    const std::string& algorithmName, int algorithmCategory,
                                                    const std::string& remark) {
    std::lock_guard<std::shared_mutex> lock(mtx_);
    auto it = algorithm_packets_.find(algorithmId);
    if (it != algorithm_packets_.end()) {
        // Update in-memory data
        it->second.algorithmName     = algorithmName;
        it->second.algorithmCategory = algorithmCategory;
        it->second.remark            = remark;
        // Update the JSON file and rename it (single source of truth)
        std::string new_file_path =
            detail::UpdateAlgorithmJsonFile(algorithmId, algorithmName, algorithmCategory, remark);
        if (!new_file_path.empty()) {
            // Reload from the (possibly renamed) file to keep in-memory state consistent
            std::string out_code;
            algorithm::AlgorithmPacketInfo reloaded;
            if (detail::AlgorithmPacketLoader::ReloadFromFile(new_file_path, out_code, reloaded) ==
                cosmo::util::ErrorEnum::Success) {
                reloaded.algorithmName     = algorithmName;
                reloaded.algorithmCategory = algorithmCategory;
                reloaded.remark            = remark;
                it->second                 = reloaded;
            }
        }
        return cosmo::util::ErrorEnum::Success;
    }
    return cosmo::util::ErrorEnum::AlgorithmNotExist;
}

cosmo::util::ErrorEnum AlgorithmServiceImpl::AddFromJson(const std::string& algorithmName,
                                                         int algorithmCategory, int algorithmUsage,
                                                         const std::string& remark,
                                                         const std::string& eventType,
                                                         const std::string& filePath) {
    cosmo::util::ErrorEnum val_res = detail::AlgorithmValidator::ValidateAlgorithmName(algorithmName);
    if (val_res != cosmo::util::ErrorEnum::Success) {
        return val_res;
    }

    // Generate ID and insert under write lock to eliminate race condition
    std::string algorithm_code;
    int check_type;
    int64_t ms_timestamp;
    {
        std::lock_guard<std::shared_mutex> lock(mtx_);
        std::unordered_set<std::string> usedIds;
        for (const auto& [key, _] : algorithm_packets_)
            usedIds.insert(key);

        algorithm_code = detail::GenerateAlgorithmCode(0, usedIds);
        if (algorithm_code.empty())
            return cosmo::util::ErrorEnum::SysErr;

        usedIds.insert(algorithm_code);
        const std::string check_type_code = detail::GenerateAlgorithmCode(7919, usedIds);
        if (check_type_code.empty())
            return cosmo::util::ErrorEnum::SysErr;
        check_type = std::stoi(check_type_code);

        ms_timestamp = static_cast<int64_t>(cosmo::util::GetCurrentDateTime().ToTimeStamp()) * 1000;

        algorithm::AlgorithmPacketInfo newPacket;
        newPacket.id                   = algorithm_code;
        newPacket.algorithmCode        = std::stoi(algorithm_code);
        newPacket.algorithmName        = algorithmName;
        newPacket.algorithmCategory    = algorithmCategory;
        newPacket.algorithmUsage       = algorithmUsage;
        newPacket.remark               = remark;
        newPacket.eventType            = eventType;
        newPacket.status               = 1;
        newPacket.createTime           = ms_timestamp;
        newPacket.updateTime           = ms_timestamp;
        newPacket.confVersionId        = "default-" + algorithm_code;
        newPacket.confVersionName      = "默认";
        newPacket.algorithmMetadata    = detail::kDefaultAlgorithmMetadata;
        newPacket.algorithmProcessdata = "[]";
        newPacket.atomicList           = "[]";
        algorithm_packets_.emplace(algorithm_code, std::move(newPacket));
    }

    // Write layout file (I/O outside lock)
    std::string algorithm_dir = filePath.empty() ? cosmo::path::GetAlgorithmPath() : filePath;
    if (!cosmo::util::CreateDir(algorithm_dir)) {
        LOG_WARN("Failed to create algorithm directory: {}", algorithm_dir);
        return cosmo::util::ErrorEnum::Failed;
    }
    uint64_t file_timestamp = cosmo::util::GetCurrentDateTime().ToYMDHMSInt();
    std::string layout_file_path =
        (std::filesystem::path(algorithm_dir) /
         (algorithm_code + "_" + algorithmName + "_" + std::to_string(file_timestamp) + ".json"))
            .string();
    nlohmann::json layoutDoc;
    detail::BuildDefaultLayoutJson(layoutDoc, algorithm_code, algorithmName, algorithmCategory,
                                   algorithmUsage, check_type, ms_timestamp, remark);
    if (cosmo::util::JsonFileUtil::WriteJsonFile(layout_file_path, layoutDoc) !=
        cosmo::util::ErrorEnum::Success)
        LOG_WARN("Failed to create default layout file: {}", layout_file_path);
    else
        LOG_INFO("Created default layout file: {}", layout_file_path);
    return cosmo::util::ErrorEnum::Success;
}

// ---------------------------------------------------------------------------
// Query operations
// ---------------------------------------------------------------------------

std::vector<algorithm::AlgorithmPacketInfo> AlgorithmServiceImpl::Query(
    const std::string& algorithmUsage, const std::string& algorithmName, const std::string& supplier,
    const std::string& algorithmId, const std::string& algorithmCategory, int pageNum, int pageSize,
    size_t& total) {
    std::vector<algorithm::AlgorithmPacketInfo> infos;
    if ((pageNum < 1) || (pageSize < 1)) {
        return infos;
    }

    // Phase 1: Copy filtered snapshot under lock
    std::vector<algorithm::AlgorithmPacketInfo> filtered_infos;
    {
        std::shared_lock<std::shared_mutex> lock(mtx_);
        for (const auto& [key, info] : algorithm_packets_) {
            if (!algorithmUsage.empty()) {
                if (algorithmUsage != std::to_string(info.algorithmUsage)) {
                    continue;
                }
            }
            if (!algorithmName.empty()) {
                if (std::string::npos == info.algorithmName.find(algorithmName)) {
                    continue;
                }
            }
            if (!supplier.empty()) {
                if (supplier != info.supplier) {
                    continue;
                }
            }
            if (!algorithmId.empty()) {
                if (std::string::npos == info.id.find(algorithmId)) {
                    continue;
                }
            }
            if (!algorithmCategory.empty()) {
                if (algorithmCategory != std::to_string(info.algorithmCategory)) {
                    continue;
                }
            }
            filtered_infos.push_back(info);
        }
    }  // release lock

    // Phase 2: Sort, paginate, validate models outside lock
    std::sort(filtered_infos.begin(), filtered_infos.end(),
              [](const algorithm::AlgorithmPacketInfo& a, const algorithm::AlgorithmPacketInfo& b) {
                  return a.updateTime > b.updateTime;
              });

    total = filtered_infos.size();
    return cosmo::util::PaginationHelper::PaginateKnownTotal(
        filtered_infos.begin(), filtered_infos.end(), pageNum, pageSize, total,
        [this](const algorithm::AlgorithmPacketInfo& info) {
            auto copied_info = info;
            detail::AlgorithmValidator::ValidateLocalModels(copied_info);
            return copied_info;
        });
}

cosmo::ActionAlgPtr AlgorithmServiceImpl::GetAlgorithm(const std::string& algorithmId) {
    // Phase 1: Copy needed fields under lock, then query action service outside lock
    int usage = -1;
    std::string update_time;
    {
        std::shared_lock<std::shared_mutex> lock(mtx_);
        auto it = algorithm_packets_.find(algorithmId);
        if (it != algorithm_packets_.end()) {
            usage       = it->second.algorithmUsage;
            update_time = it->second.algorithmUpdateTime;
        }
    }

    if (usage >= 0) {
        auto& action_svc = ServiceRegistry::Instance().Get<IActionService>();
        cosmo::ActionAlgPtr alg;
        if (usage == 2) {
            alg = action_svc.GetPicActionAlg(algorithmId, update_time);
            if (!alg)
                alg = action_svc.GetPicActionAlgByCode(algorithmId);
        } else {
            alg = action_svc.GetActionAlg(algorithmId, update_time);
            if (!alg)
                alg = action_svc.GetActionAlgByCode(algorithmId);
        }
        if (alg)
            return alg;
    }

    // Phase 2: Try reload from file, then query again
    const std::string algorithm_layout_dir = cosmo::path::GetAlgorithmPath();
    std::string fp = cosmo::util::FindPrefixedJsonFile(algorithm_layout_dir, algorithmId);
    if (!fp.empty()) {
        if (ReloadAlgorithmFromFile(fp) == cosmo::util::ErrorEnum::Success) {
            int reloaded_usage = -1;
            {
                std::shared_lock<std::shared_mutex> lock(mtx_);
                auto it = algorithm_packets_.find(algorithmId);
                if (it != algorithm_packets_.end()) {
                    reloaded_usage = it->second.algorithmUsage;
                }
            }
            if (reloaded_usage >= 0) {
                auto& action_svc = ServiceRegistry::Instance().Get<IActionService>();
                if (reloaded_usage == 2)
                    return action_svc.GetPicActionAlgByCode(algorithmId);
                else
                    return action_svc.GetActionAlgByCode(algorithmId);
            }
        }
    }
    return {};
}

std::string AlgorithmServiceImpl::GetAlgorithmName(const std::string& algorithmId) {
    std::shared_lock<std::shared_mutex> lock(mtx_);
    auto it = algorithm_packets_.find(algorithmId);
    if (it != algorithm_packets_.end()) {
        return it->second.algorithmName;
    }
    return "";
}

std::string AlgorithmServiceImpl::GetMetaData(const std::string& algorithmId) {
    std::shared_lock<std::shared_mutex> lock(mtx_);
    auto it = algorithm_packets_.find(algorithmId);
    if (it != algorithm_packets_.end()) {
        return it->second.algorithmMetadata;
    }
    return "";
}

std::vector<std::string> AlgorithmServiceImpl::GetAlgorithmsByModelId(const std::string& modelId) {
    std::vector<std::string> algorithms;
    std::shared_lock<std::shared_mutex> lock(mtx_);
    for (const auto& [key, info] : algorithm_packets_) {
        bool has_model = std::any_of(info.modelInfo.models.begin(), info.modelInfo.models.end(),
                                     [&modelId](const auto& model) { return model.modelCode == modelId; });
        if (has_model) {
            algorithms.push_back(key);
        }
    }

    return algorithms;
}

cosmo::util::ErrorEnum AlgorithmServiceImpl::ReloadAlgorithmFromFile(const std::string& filePath) {
    std::string algorithm_code;
    algorithm::AlgorithmPacketInfo newPacket;
    auto result = detail::AlgorithmPacketLoader::ReloadFromFile(filePath, algorithm_code, newPacket);
    if (result != cosmo::util::ErrorEnum::Success)
        return result;

    std::lock_guard<std::shared_mutex> lock(mtx_);
    algorithm_packets_.insert_or_assign(algorithm_code, std::move(newPacket));

    return cosmo::util::ErrorEnum::Success;
}

// ---------------------------------------------------------------------------
// Specialized queries
// ---------------------------------------------------------------------------

namespace {

    constexpr int kCategoryPersonFlow  = 8;
    constexpr int kCategoryVehicleFlow = 11;

}  // namespace

std::vector<algorithm::AlgorithmLocalInfo> AlgorithmServiceImpl::GetPassFlowAlgorithms() {
    std::vector<algorithm::AlgorithmLocalInfo> result;
    std::shared_lock<std::shared_mutex> lock(mtx_);
    for (const auto& [key, info] : algorithm_packets_) {
        if (info.algorithmCategory == kCategoryPersonFlow || info.algorithmCategory == kCategoryVehicleFlow) {
            algorithm::AlgorithmLocalInfo item;
            item.algorithmId       = key;
            item.algorithmName     = info.algorithmName;
            item.algorithmCategory = info.algorithmCategory;
            item.remark            = info.remark;
            result.push_back(item);
        }
    }
    return result;
}

}  // namespace cosmo::service
