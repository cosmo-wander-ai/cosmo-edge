#include "service/ai/impl/VisualDecisionPolicy.h"

#include <cryptopp/sha.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <set>
#include <stdexcept>

namespace cosmo::service::visual {
namespace {
    void Require(bool valid) {
        if (!valid)
            throw std::invalid_argument("invalid_visual_acceptance");
    }
    std::string FileSha(const std::string& path) {
        std::ifstream file(path, std::ios::binary);
        Require(file.is_open());
        CryptoPP::SHA256 sha;
        std::array<char, 65536> block{};
        while (file.read(block.data(), block.size()) || file.gcount())
            sha.Update(reinterpret_cast<const uint8_t*>(block.data()), file.gcount());
        Require(file.eof());
        std::array<uint8_t, CryptoPP::SHA256::DIGESTSIZE> bytes{};
        sha.Final(bytes.data());
        std::string result;
        for (auto byte : bytes) {
            result += "0123456789abcdef"[byte >> 4];
            result += "0123456789abcdef"[byte & 15];
        }
        return result;
    }
    bool UnitNumber(const Json& value) {
        return value.is_number() && std::isfinite(value.get<double>()) && value.get<double>() >= 0 &&
               value.get<double>() <= 1;
    }
}  // namespace

void ValidateAcceptance(const Json& acceptance, const std::string& manifestSha,
                        const std::string& engineSha) {
    Require(Sha256Identity(manifestSha) && Sha256Identity(engineSha));
    Require(acceptance.at("schema") == 1 && acceptance.at("manifest_sha256") == manifestSha &&
            acceptance.at("engine_sha256") == engineSha);
    const auto& profiles = acceptance.at("profiles");
    Require(profiles.is_array() && profiles.size() <= 128);
    std::set<std::string> ids;
    for (const auto& profile : profiles) {
        const auto id = profile.at("id").get<std::string>();
        Require(Identity(id) && ids.insert(id).second && profile.at("status") == "accepted");
        Require(Identity(profile.at("evidence_ref").get<std::string>()));
        Require(Sha256Identity(profile.at("configuration_sha256").get<std::string>()));
        Require(profile.at("aggregation") == "all" || profile.at("aggregation") == "any");
        Require(profile.at("unknown") == "keep" || profile.at("unknown") == "drop");
        const auto& tasks = profile.at("task_ids");
        Require(tasks.is_array() && !tasks.empty() && tasks.size() <= 256);
        std::set<std::string> taskIds;
        for (const auto& task : tasks)
            Require(Identity(task.get<std::string>()) && taskIds.insert(task.get<std::string>()).second);
        const auto& questions = profile.at("questions");
        Require(questions.is_array() && !questions.empty() && questions.size() <= 512);
        std::set<std::string> hashes;
        for (const auto& rule : questions) {
            const auto hash = rule.at("compiled_sha256").get<std::string>();
            Require(Sha256Identity(hash) && hashes.insert(hash).second);
            Require(UnitNumber(rule.at("min_probability")) && UnitNumber(rule.at("min_margin")));
            const auto& positives = rule.at("positive_options");
            Require(positives.is_array() && !positives.empty() && positives.size() <= 16);
            std::set<std::string> labels;
            for (const auto& value : positives)
                Require(Identity(value.get<std::string>()) && labels.insert(value.get<std::string>()).second);
        }
    }
}

Json ReadAcceptance(const std::string& path, const std::string& sha, const std::string& manifestSha,
                    const std::string& enginePath) {
    if (path.empty() && sha.empty())
        return Json::object();
    Require(Sha256Identity(sha));
    std::ifstream input(path, std::ios::binary);
    Require(input.is_open());
    std::array<char, kMaxJson + 1> buffer{};
    input.read(buffer.data(), buffer.size());
    const std::string text(buffer.data(), static_cast<size_t>(input.gcount()));
    Require(text.size() <= kMaxJson &&
            Sha256(reinterpret_cast<const uint8_t*>(text.data()), text.size()) == sha);
    auto result = Parse(text);
    ValidateAcceptance(result, manifestSha, FileSha(enginePath));
    result["acceptance_sha256"] = sha;
    return result;
}

Json AcceptanceProfiles(const Json& acceptance) {
    Json result = Json::array();
    if (acceptance.contains("profiles"))
        for (const auto& profile : acceptance.at("profiles"))
            result.push_back({{"id", profile.at("id")},
                              {"task_ids", profile.at("task_ids")},
                              {"aggregation", profile.at("aggregation")},
                              {"unknown", profile.at("unknown")}});
    return result;
}

Json EvaluateDecision(const Json& acceptance, const VisualDecisionRequest& request, const std::string& task,
                      const Json& response) {
    Json result{{"schema", 1},
                {"mode", request.mode},
                {"profile_id", request.policyId},
                {"configuration_sha256", request.qualificationRevision},
                {"business_qualified", false},
                {"verdict", "unknown"},
                {"retain", true},
                {"filter_applied", false},
                {"reason", "review_only"}};
    if (request.mode != "filter")
        return result;
    result["reason"] = "unqualified_filtering_disabled";
    if (!acceptance.contains("profiles") || !response.contains("manifest_sha256") ||
        !response.at("manifest_sha256").is_string() ||
        acceptance.value("manifest_sha256", std::string()) !=
            response.value("manifest_sha256", std::string()))
        return result;
    const auto& profiles = acceptance.at("profiles");
    auto selected        = std::find_if(profiles.begin(), profiles.end(), [&](const auto& p) {
        return p.at("id") == request.policyId && p.at("status") == "accepted" &&
               p.at("configuration_sha256") == request.qualificationRevision &&
               std::find(p.at("task_ids").begin(), p.at("task_ids").end(), task) != p.at("task_ids").end();
    });
    if (selected == profiles.end() || request.questions.empty())
        return result;
    std::vector<const Json*> rules;
    for (const auto& question : request.questions) {
        const auto& allowed = selected->at("questions");
        auto rule           = std::find_if(allowed.begin(), allowed.end(), [&](const auto& r) {
            return r.at("compiled_sha256") == question.compiledSha256;
        });
        if (rule == allowed.end())
            return result;
        for (const auto& positive : rule->at("positive_options"))
            if (std::find(question.orderedOptions.begin(), question.orderedOptions.end(),
                          positive.get<std::string>()) == question.orderedOptions.end())
                return result;
        rules.push_back(&*rule);
    }
    result["business_qualified"] = true;
    result["acceptance_sha256"]  = acceptance.value("acceptance_sha256", std::string());
    result["evidence_ref"]       = selected->at("evidence_ref");
    result["aggregation"]        = selected->at("aggregation");
    result["unknown"]            = selected->at("unknown");
    // Technical failures always preserve the candidate. The unknown policy
    // applies to low-confidence completed predictions, never failed inference.
    result["reason"] = "incomplete_model_result";
    if (response.value("status", std::string()) != "completed" || !response.contains("items") ||
        response.at("items").size() != rules.size())
        return result;
    bool anyTrue = false, anyFalse = false, anyUnknown = false;
    for (size_t i = 0; i < rules.size(); ++i) {
        const auto& item     = response.at("items").at(i);
        const auto& question = request.questions[i];
        const auto& rule     = *rules[i];
        auto top             = std::find(question.orderedOptions.begin(), question.orderedOptions.end(),
                                         item.at("top1").get<std::string>());
        if (top == question.orderedOptions.end() || item.at("status") != "completed" ||
            item.at("compiled_sha256") != question.compiledSha256)
            return result;
        const auto index = std::distance(question.orderedOptions.begin(), top);
        const bool uncertain =
            item.at("probabilities").at(index).get<double>() < rule.at("min_probability").get<double>() ||
            item.at("top2_margin").get<double>() < rule.at("min_margin").get<double>();
        const bool positive =
            std::find(rule.at("positive_options").begin(), rule.at("positive_options").end(), *top) !=
            rule.at("positive_options").end();
        anyUnknown |= uncertain;
        anyTrue |= !uncertain && positive;
        anyFalse |= !uncertain && !positive;
    }
    const bool all            = selected->at("aggregation") == "all";
    const std::string verdict = all ? (anyFalse     ? "reject"
                                       : anyUnknown ? "unknown"
                                                    : "accept")
                                    : (anyTrue      ? "accept"
                                       : anyUnknown ? "unknown"
                                                    : "reject");
    const bool retain = verdict == "accept" || (verdict == "unknown" && selected->at("unknown") == "keep");
    result.update({{"verdict", verdict},
                   {"retain", retain},
                   {"filter_applied", !retain},
                   {"reason", verdict == "unknown" ? "low_confidence" : "qualified_decision"}});
    return result;
}
}  // namespace cosmo::service::visual
