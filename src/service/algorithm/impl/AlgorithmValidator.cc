// AlgorithmValidator — Validate algorithm name

#include "service/algorithm/impl/AlgorithmValidator.h"

#include "service/detail/ServiceRegistry.h"
#include "service/model/IModelQuery.h"
#include "service/model/IModelService.h"
#include "util/FileUtil.h"
#include "util/JsonFileUtil.h"
#include "util/JsonStructUtil.h"
#include "util/Keys.h"
#include "util/Log.h"

namespace cosmo::service::detail {

cosmo::util::ErrorEnum AlgorithmValidator::ValidateAlgorithmName(const std::string& algorithmName) {
    if (algorithmName.find('_') != std::string::npos) {
        LOG_WARN("Algorithm name contains underscore: {}", algorithmName);
        return cosmo::util::ErrorEnum::InvalidParam;
    }
    return cosmo::util::ErrorEnum::Success;
}

cosmo::util::ErrorEnum AlgorithmValidator::ParseAndValidatePacket(const std::string& unZipFile,
                                                                  algorithm::AlgorithmPacketInfo& cfgInfo) {
    auto content = cosmo::util::ReadFile(unZipFile);
    if (!cosmo::util::DecodeJson(content, cfgInfo)) {
        return cosmo::util::ErrorEnum::Failed;
    }

    cfgInfo.processdata = std::make_shared<cosmo::ActionAlg>();
    if (!cosmo::util::DecodeJson(cfgInfo.algorithmProcessdata, cfgInfo.processdata->workFlow)) {
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
                    info.bActive   = ServiceRegistry::Instance().Get<IModelService>().ModelValid(
                          info.modelCode, info.modelName);
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
            ServiceRegistry::Instance().Get<IModelService>().ModelValid(info.modelCode, info.modelName);
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
