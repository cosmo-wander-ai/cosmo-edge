// AlgorithmValidator — Validate algorithm name

#include "service/algorithm/impl/AlgorithmValidator.h"

#include "service/algorithm/impl/AlgorithmJsonCodec.h"
#include "service/algorithm/impl/AlgorithmPacketLoader.h"
#include "service/detail/ServiceRegistry.h"
#include "service/model/IModelQuery.h"
#include "util/FileUtil.h"
#include "util/JsonFileUtil.h"
#include "util/JsonStructUtil.h"
#include "util/Keys.h"
#include "util/Log.h"
#include "util/PathUtil.h"

namespace cosmo::service::detail {

cosmo::util::ErrorEnum AlgorithmValidator::ValidateAlgorithmName(const std::string& algorithmName) {
    if (!cosmo::path::IsSafePathComponent(algorithmName) || algorithmName.find('_') != std::string::npos) {
        LOG_WARN("Invalid algorithm name: {}", algorithmName);
        return cosmo::util::ErrorEnum::InvalidParam;
    }
    return cosmo::util::ErrorEnum::Success;
}

cosmo::util::ErrorEnum AlgorithmValidator::ParseAndValidatePacket(const std::string& unZipFile,
                                                                  algorithm::AlgorithmPacketInfo& cfgInfo) {
    auto content        = cosmo::util::ReadFile(unZipFile);
    const auto document = nlohmann::json::parse(content, nullptr, false);
    if (document.is_object() && document.contains("algorithmId")) {
        // The scene editor/exporter writes layout JSON with string-valued codes.
        // Use the same layout parser as startup so an exported scene can be
        // imported without losing its question catalog or workflow parameters.
        std::string code;
        if (!AlgorithmPacketLoader::ParsePacketFromJson(document, unZipFile, code, cfgInfo))
            return cosmo::util::ErrorEnum::Failed;
    } else if (!cosmo::util::DecodeJson(content, cfgInfo)) {
        return cosmo::util::ErrorEnum::Failed;
    }

    cfgInfo.processdata = std::make_shared<cosmo::ActionAlg>();
    if (!cosmo::util::DecodeJson(NormalizeAlgorithmProcessdataParamValues(cfgInfo.algorithmProcessdata),
                                 cfgInfo.processdata->workFlow)) {
        return cosmo::util::ErrorEnum::Failed;
    }

    cfgInfo.processdata->algorithmCode       = cfgInfo.id;
    cfgInfo.processdata->algorithmName       = cfgInfo.algorithmName;
    cfgInfo.processdata->algorithmUpdateTime = cfgInfo.algorithmUpdateTime;
    cfgInfo.processdata->category            = std::to_string(cfgInfo.algorithmCategory);

    if (!cosmo::util::DecodeJson(cfgInfo.algorithmMetadata, cfgInfo.metadata)) {
        return cosmo::util::ErrorEnum::ActionAlgArrangeConfigFail;
    }

    ValidateModels(cfgInfo);

    return cosmo::util::ErrorEnum::Success;
}

void AlgorithmValidator::ValidateModels(algorithm::AlgorithmPacketInfo& cfgInfo) {
    int has_unread_task = false;
    if (cfgInfo.processdata) {
        for (auto& workFlow : cfgInfo.processdata->workFlow) {
            for (auto& param : workFlow.configObject.params) {
                if (cosmo::key::ATOM_CODE == param.key.ToString()) {
                    algorithm::AlgorithmModelInfo info;
                    info.modelCode = param.value;
                    info.bActive   = ServiceRegistry::Instance().Get<IModelQuery>().ModelValid(info.modelCode,
                                                                                               info.modelName);
                    if (!info.bActive) {
                        has_unread_task = true;
                    }
                    cfgInfo.modelInfo.models.push_back(info);
                }
            }
        }
    }
    if (has_unread_task) {
        cfgInfo.modelInfo.bActive = false;
    } else {
        cfgInfo.modelInfo.bActive = true;
    }
}

void AlgorithmValidator::ValidateLocalModels(algorithm::AlgorithmPacketInfo& cfgInfo) {
    int has_unread_task = false;
    for (auto& info : cfgInfo.modelInfo.models) {
        info.bActive =
            ServiceRegistry::Instance().Get<IModelQuery>().ModelValid(info.modelCode, info.modelName);
        if (!info.bActive) {
            has_unread_task = true;
        }
    }
    if (has_unread_task) {
        cfgInfo.modelInfo.bActive = false;
    } else {
        cfgInfo.modelInfo.bActive = true;
    }
}

}  // namespace cosmo::service::detail
