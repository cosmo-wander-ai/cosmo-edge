#pragma once

#include <chrono>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "service/ai/IVisualDecisionService.h"

namespace cosmo::service::visual {

using Json                             = nlohmann::json;
using Clock                            = std::chrono::steady_clock;
inline constexpr const char* kProtocol = "cosmo-visual-decision-v1";
inline constexpr const char* kProfile  = "laya-256p-256s-v1";
inline constexpr size_t kMaxJson       = 64 * 1024;
inline constexpr size_t kMaxImage      = 2 * 1024 * 1024;

struct Release {
    std::string manifestSha256;
    Json modelHashes;
    bool Valid() const;
};

bool Identity(const std::string& value);
bool Sha256Identity(const std::string& value);
std::string Sha256(const uint8_t* data, size_t size);
Json Parse(const std::string& encoded);
Release ReadRelease(const std::string& path, const std::string& expectedSha256);
bool ValidQuestion(const VisualQuestionRef& question);
Json ItemIdentity(const VisualQuestionRef& question);
Json Failure(const Json& request, const std::string& reason);
Json ValidateResponse(const Json& request, const std::vector<VisualQuestionRef>& questions,
                      const Release& release, const std::string& imageSha256, const Json& response);
Json Exchange(const std::string& socketPath, const Json& request, const std::vector<uint8_t>& jpeg,
              Clock::time_point deadline);
int64_t MonotonicMilliseconds();

}  // namespace cosmo::service::visual
