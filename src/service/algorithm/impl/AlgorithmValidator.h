#pragma once

#include <string>

#include "util/dto/AlgorithmPacketDto.h"

namespace cosmo::service::detail {

class AlgorithmValidator {
public:
    /**
     * @brief Validate algorithm name
     */
    static cosmo::util::ErrorEnum ValidateAlgorithmName(const std::string& algorithmName);

    /**
     * @brief Parse algorithm package config from unzip path and validate model reference
     */
    static cosmo::util::ErrorEnum ParseAndValidatePacket(const std::string& unZipFile,
                                                         algorithm::AlgorithmPacketInfo& cfgInfo);

    /**
     * @brief Validate if models depended on by workflow are locally valid
     */
    static void ValidateModels(algorithm::AlgorithmPacketInfo& cfgInfo);

    /**
     * @brief Validate local stored model configuration
     */
    static void ValidateLocalModels(algorithm::AlgorithmPacketInfo& cfgInfo);
};

}  // namespace cosmo::service::detail
