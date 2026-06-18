// Algorithm JSON serialization/deserialization utilities
// Extracted from AlgorithmServiceImpl to reduce method bloat.

#pragma once

#include <cstdint>
#include <string>

#include "nlohmann/json_fwd.hpp"

namespace cosmo::service::detail {

/// Default algorithm metadata JSON string
extern const char kDefaultAlgorithmMetadata[];

/// Convert numeric values in configObject.params[].value to strings in algorithmProcessdata JSON,
/// preventing cosmo::util::DecodeJson parse failures
std::string NormalizeAlgorithmProcessdataParamValues(const std::string& processDataJson);

/// Update algorithmName/remark/algorithmCategory in algorithm JSON file and rename the file
/// Returns the updated file path (empty string if file does not exist)
std::string UpdateAlgorithmJsonFile(const std::string& algorithmId, const std::string& algorithmName,
                                    int algorithmCategory, const std::string& remark);

/// Build default layout JSON document for AIBox platform
void BuildDefaultLayoutJson(nlohmann::json& outDoc, const std::string& algorithmCode,
                            const std::string& algorithmName, int algorithmCategory, int algorithmUsage,
                            int checkType, int64_t msTimestamp, const std::string& remark);

}  // namespace cosmo::service::detail
