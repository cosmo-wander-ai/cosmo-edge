#pragma once

#include "service/ai/impl/VisualDecisionProtocol.h"

namespace cosmo::service::visual {
// Operator-installed acceptance is separate from model predictions. It binds
// the engine, model manifest, exact tasks and compiled question identities.
Json ReadAcceptance(const std::string& path, const std::string& sha, const std::string& manifestSha,
                    const std::string& enginePath = "/proc/self/exe");
void ValidateAcceptance(const Json& acceptance, const std::string& manifestSha, const std::string& engineSha);
Json EvaluateDecision(const Json& acceptance, const VisualDecisionRequest& request, const std::string& task,
                      const Json& response);
Json AcceptanceProfiles(const Json& acceptance);
}  // namespace cosmo::service::visual
