#include "flow/common/VisualJudgment.h"

#include <algorithm>
#include <set>
#include <stdexcept>

#include "service/ai/impl/VisualDecisionProtocol.h"
#include "service/detail/ServiceRegistry.h"
#include "util/UuidUtil.h"

namespace cosmo {
namespace {
    namespace visual                  = service::visual;
    using Json                        = nlohmann::json;
    const std::string kQuestionPrefix = "visual.question.";
    struct ConfigError : std::runtime_error {
        using std::runtime_error::runtime_error;
    };
    std::string Hash(const std::string& text) {
        return visual::Sha256(reinterpret_cast<const uint8_t*>(text.data()), text.size());
    }
    void Require(bool value, const char* reason) {
        if (!value)
            throw ConfigError(reason);
    }
    std::vector<std::string> Selection(const std::string& text) {
        auto value = visual::Parse(text);
        Require(value.is_array() && !value.empty() && value.size() <= 8, "invalid_question_selection");
        auto result = value.get<std::vector<std::string>>();
        std::set<std::string> unique(result.begin(), result.end());
        Require(unique.size() == result.size(), "duplicate_question_selection");
        return result;
    }
    Json LegacyQuestion(const std::string& id, const std::string& prompt, bool advanced) {
        const auto content =
            advanced ? prompt
                     : "判断图片中是否存在【" + (prompt.empty() ? std::string("目标") : prompt) + "】目标";
        return {{"id", id}, {"version", 1}, {"type", "noul"}, {"instructions", content}};
    }
}  // namespace

void UpdateVisualParameters(VisualParameters& values, const std::vector<MsgDynamicKeyValue>& updates) {
    for (const auto& update : updates) {
        const auto key = update.key.ToString();
        if (key.rfind("visual.", 0) == 0)
            values[key] = update.value.ToString();
    }
}

VisualJudgment::VisualJudgment(const std::string& task, const std::string& prompt, bool advanced,
                               const VisualParameters& parameters, const std::vector<MsgTaskArea>& areas,
                               const std::optional<std::map<std::string, std::string>>& semanticPrompts) {
    const auto epoch = util::GenerateUUID();
    run_             = std::make_shared<service::VisualDecisionRun>(task, epoch, "invalid_configuration");
    try {
        Require(visual::Identity(task), "invalid_task_identity");
        std::map<std::string, Json> catalog;
        // One stable metadata field lets channel editors add/remove questions
        // without changing the scene's parameter ownership for every question ID.
        const auto packed = parameters.find("visual.catalog");
        if (packed != parameters.end() && !packed->second.empty()) {
            const auto value = visual::Parse(packed->second);
            Require(value.is_object() && value.at("questions").is_array(), "invalid_question_catalog");
            for (const auto& question : value.at("questions")) {
                Require(question.is_object() && question.at("id").is_string(), "invalid_question_catalog");
                Require(catalog.emplace(question.at("id").get<std::string>(), question).second,
                        "duplicate_question_identity");
            }
            if (!catalog.empty())
                bindings_[""] = Selection(value.at("default").dump());
        }
        for (const auto& [key, value] : parameters) {
            if (key.rfind(kQuestionPrefix, 0) == 0) {
                auto id       = key.substr(kQuestionPrefix.size());
                auto question = visual::Parse(value);
                Require(question.is_object() && question.at("id") == id, "question_key_identity_mismatch");
                Require(catalog.emplace(id, std::move(question)).second, "duplicate_question_identity");
            } else if (key == "visual.catalog") {
                continue;
            } else if (key == "visual.mode") {
                Require(value == "review", "unqualified_filtering_disabled");
            } else if (key == "visual.timeout_ms") {
                auto timeout = visual::Parse(value);
                Require(timeout.is_number_integer(), "invalid_visual_timeout");
                auto count = timeout.get<int64_t>();
                Require(count >= 1 && count <= 10000, "invalid_visual_timeout");
                timeout_ = std::chrono::milliseconds(count);
            } else
                Require(key == "visual.questions", "unknown_visual_parameter");
        }
        auto selection = parameters.find("visual.questions");
        if (selection != parameters.end())
            bindings_[""] = Selection(selection->second);
        else if (!bindings_.count("")) {
            Require(!catalog.count("legacy-default"), "reserved_question_identity");
            catalog["legacy-default"] = LegacyQuestion("legacy-default", prompt, advanced);
            bindings_[""]             = {"legacy-default"};
        }
        Require(areas.size() <= 128, "too_many_visual_areas");
        Json geometry = Json::array();
        std::set<std::string> areaIds;
        for (const auto& area : areas) {
            Require(visual::Identity(area.areaId) && areaIds.insert(area.areaId).second,
                    "invalid_visual_area_identity");
            // Include geometry in the revision so moving a region invalidates
            // queued/captured results even when its question text is unchanged.
            geometry.push_back({{"id", area.areaId}, {"box", area.pointBox}, {"points", area.points}});
            std::string localPrompt = prompt;
            bool localAdvanced = advanced, overridden = false;
            std::string selected;
            bool hasSelection = false;
            for (const auto& param : area.params) {
                const auto key = param.key.ToString();
                if (key == "visual.questions") {
                    Require(!hasSelection, "duplicate_area_question_selection");
                    selected     = param.value.ToString();
                    hasSelection = true;
                } else if (key == "keywords" || key == "prompt") {
                    localPrompt = param.value.ToString();
                    overridden  = true;
                } else if (key == "advanced_mode") {
                    const auto value = param.value.ToString();
                    Require(value == "true" || value == "false" || value == "1" || value == "0",
                            "invalid_area_prompt_mode");
                    localAdvanced = value == "true" || value == "1";
                    overridden    = true;
                } else
                    Require(key.rfind("visual.", 0) != 0, "unknown_visual_area_parameter");
            }
            if (hasSelection) {
                Require(!overridden, "ambiguous_area_question_configuration");
                bindings_[area.areaId] = Selection(selected);
            } else if (overridden) {
                auto id = "roi-" + Hash(area.areaId).substr(0, 24);
                Require(!catalog.count(id), "reserved_question_identity");
                catalog[id]            = LegacyQuestion(id, localPrompt, localAdvanced);
                bindings_[area.areaId] = {id};
            }
        }
        Require(!catalog.empty() && catalog.size() <= 32, "too_many_visual_questions");
        semanticFallback_ = semanticPrompts.has_value() && selection == parameters.end() &&
                            bindings_[""] == std::vector<std::string>{"legacy-default"};
        if (semanticFallback_) {
            Require(semanticPrompts->size() + catalog.size() <= 512, "too_many_semantic_questions");
            for (const auto& [key, instruction] : *semanticPrompts) {
                Require(!key.empty() && key.size() <= 1024 && !instruction.empty(),
                        "invalid_semantic_question");
                auto id = "semantic-" + Hash(key).substr(0, 24);
                Require(!catalog.count(id), "reserved_question_identity");
                catalog[id]            = LegacyQuestion(id, instruction, true);
                semanticBindings_[key] = {id};
            }
        }
        for (const auto& [area, ids] : bindings_)
            for (const auto& id : ids)
                Require(catalog.count(id) == 1, "unknown_question_binding");
        for (const auto& [id, question] : catalog) {
            Require(question.contains("version") && question.at("version").is_number_integer() &&
                        question.at("version").get<int64_t>() > 0 &&
                        question.at("version").get<int64_t>() <= 2147483647,
                    "invalid_question_version");
            specs_.push_back({id, question, ""});
        }
        const auto revision = Hash(Json{
            {"catalog", catalog},
            {"bindings", bindings_},
            {"semantic_bindings", semanticBindings_},
            {"semantic_fallback", semanticFallback_},
            {"geometry", geometry},
            {"timeout_ms", timeout_.count()},
            {"mode", "review"}}.dump());
        run_                = std::make_shared<service::VisualDecisionRun>(task, epoch, revision);
        prepared_ = service::ServiceRegistry::Instance().Get<service::IVisualQuestionService>().Prepare(
            specs_, run_, std::chrono::milliseconds(specs_.size() > 32 ? 600000 : 300000));
    } catch (const ConfigError& error) {
        failure_ = error.what();
    } catch (...) {
        failure_ = "invalid_visual_configuration";
    }
}

service::VisualDecisionResult VisualJudgment::Decide(const std::string& frameId, const std::string& roiId,
                                                     const std::string& areaId,
                                                     service::IVisualDecisionService::Prepare prepare,
                                                     const std::string& semanticKey) const {
    Json identity                       = {{"request_id", util::GenerateUUID()},
                                           {"task_id", run_->taskId},
                                           {"run_epoch", run_->runEpoch},
                                           {"config_revision", run_->configRevision},
                                           {"frame_id", frameId},
                                           {"roi_id", roiId},
                                           {"manifest_sha256", nullptr},
                                           {"items", Json::array()}};
    auto selected                       = bindings_.find(areaId);
    const std::vector<std::string>* ids = nullptr;
    bool missingSemantic                = false;
    if (selected != bindings_.end() && !areaId.empty())
        ids = &selected->second;
    else if (semanticFallback_ && !semanticKey.empty()) {
        auto semantic   = semanticBindings_.find(semanticKey);
        missingSemantic = semantic == semanticBindings_.end();
        if (!missingSemantic)
            ids = &semantic->second;
    } else {
        selected = bindings_.find("");
        if (selected != bindings_.end())
            ids = &selected->second;
    }
    if (ids) {
        for (const auto& id : *ids) {
            auto spec =
                std::find_if(specs_.begin(), specs_.end(), [&](const auto& s) { return s.itemId == id; });
            if (spec != specs_.end())
                identity["items"].push_back({{"item_id", id},
                                             {"question_id", spec->question.at("id")},
                                             {"question_version", spec->question.at("version")},
                                             {"compiled_sha256", nullptr}});
        }
    }
    auto unknown = [&](const std::string& reason) {
        service::VisualDecisionResult result{identity, visual::Failure(identity, reason)};
        auto& registry = service::ServiceRegistry::Instance();
        if (registry.Has<service::IVisualAuditService>()) {
            auto& audit  = registry.Get<service::IVisualAuditService>();
            result.audit = audit.Begin(identity);
            audit.Complete(result.audit, result.response);
        }
        return result;
    };
    if (!run_->Active())
        return unknown("stale_task_run");
    if (!failure_.empty())
        return unknown(failure_);
    if (missingSemantic)
        return unknown("unprepared_semantic_label");
    if (!ids)
        return unknown("missing_question_binding");
    if (!prepared_.valid() || prepared_.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready)
        return unknown("configuration_preparing");
    try {
        const auto& prepared = prepared_.get();
        if (!prepared.ready)
            return unknown(prepared.reason);
        service::VisualDecisionRequest request{frameId, roiId, {}};
        for (const auto& id : *ids) {
            auto ref = std::find_if(prepared.questions.begin(), prepared.questions.end(),
                                    [&](const auto& q) { return q.itemId == id; });
            if (ref == prepared.questions.end())
                return unknown("missing_prepared_question");
            request.questions.push_back(*ref);
        }
        return service::ServiceRegistry::Instance().Get<service::IVisualDecisionService>().Decide(
            request, run_, std::move(prepare), timeout_);
    } catch (...) {
        return unknown("visual_service_unavailable");
    }
}
}  // namespace cosmo
