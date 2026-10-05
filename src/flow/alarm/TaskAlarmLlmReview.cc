// TaskAlarmLlmReview.cc — LLM-based alarm review.
// Implementation partition of TaskAlarm (declared in flow/alarm/TaskAlarm.h).

#include <algorithm>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <sstream>

#include "flow/alarm/AlarmReviewRoi.h"
#include "flow/alarm/TaskAlarm.h"
#include "flow/alarm/TaskAlarmInternalTypes.h"
#include "flow/common/LlmYesNoJudge.h"
#include "flow/qwen3vl/OpenAiVlmClient.h"
#include "media/VideoFrame.h"
#include "service/ai/ILlmInferService.h"
#include "service/detail/ServiceRegistry.h"
#include "service/media/IVideoFrameCodec.h"
#include "service/media/IVideoFrameOSD.h"
#include "service/media/IVideoFrameTransform.h"
#include "util/Log.h"

static constexpr const char* kTag = "TaskAlarm ";

namespace cosmo {

// ===================== LLM review related methods (matches old a9313d10/30fbf1bc) =====================
static const Qwen3VLGenerationParam kLlmReviewGenParam{
    true,  // do_sample
    10,    // top_k
    0.7f,  // top_p
    0.3f   // temperature
};

bool TaskAlarm::InitLlmReviewer() {
    if (m_param.llmOpenAiConfig.Enabled()) {
        if (m_param.llmOpenAiConfig.base_url.empty() || m_param.llmOpenAiConfig.model.empty()) {
            LOG_WARN("{}[{}] LLM Review: OpenAI config incomplete, baseUrl/model required", kTag, task_id);
            return false;
        }
        return true;
    }
    if (service::ServiceRegistry::Instance().Get<service::ILlmInferService>().IsInitialized())
        return true;
    if (m_param.llmAtomicCode.empty()) {
        LOG_WARN("{}[{}] LLM Review: llmAtomicCode is empty", kTag, task_id);
        return false;
    }
    bool ok = service::ServiceRegistry::Instance().Get<service::ILlmInferService>().EnsureInit(
        m_param.llmAtomicCode);
    if (ok)
        LOG_INFO("{}[{}] LLM Review: Qwen3VL shared instance ready. AtomicCode:{}", kTag, task_id,
                 m_param.llmAtomicCode);
    else
        LOG_WARN("{}[{}] LLM Review: Qwen3VL shared instance init failed. AtomicCode:{}", kTag, task_id,
                 m_param.llmAtomicCode);
    return ok;
}

std::string TaskAlarm::BuildLlmReviewPrompt(const DataAlarmUnit& alarmUnit) {
    std::ostringstream ss;
    ss << "告警审核。图片为按告警框裁剪后的目标图。";

    // Priority: user-defined review content > upstream classify label > algorithm name > generic
    // description (matches Qwen3VLWorker judgement prompt style: answer yes/no only)
    if (!m_param.llmReviewContent.empty()) {
        ss << "判断图片中是否存在【" << m_param.llmReviewContent
           << "】目标或对应行为，回答是或者否,不要换行，不要其他内容。";
    } else {
        std::string label;
        if (!alarmUnit.confidence.empty() && !alarmUnit.confidence[0].label.empty()) {
            label = alarmUnit.confidence[0].label;
        } else if (!alarmUnit.attrRsts.empty() && !alarmUnit.attrRsts[0].label.empty()) {
            label = alarmUnit.attrRsts[0].label;
        }

        if (!label.empty()) {
            ss << "判断图片中是否存在【" << label
               << "】目标或对应行为，回答是或者否,不要换行，不要其他内容。";
        } else {
            std::string algName = GetAlgName();
            if (!algName.empty()) {
                ss << "判断图片中是否存在【" << algName
                   << "】相关的有效目标或对应行为，回答是或者否,不要换行，不要其他内容。";
            } else {
                ss << "判断图片中是否存在有效目标或对应行为，回答是或者否,不要换行，不要其他内容。";
            }
        }
    }
    return ss.str();
}

TaskAlarm::LlmReviewResult TaskAlarm::ParseLlmReviewResult(const std::string& text) {
    LlmReviewResult result;
    switch (ParseJudgeYesNo(text)) {
        case LlmJudgeYesNo::Yes:
            result.is_valid     = true;
            result.parseSuccess = true;
            return result;
        case LlmJudgeYesNo::No:
            result.is_valid     = false;
            result.parseSuccess = true;
            return result;
        case LlmJudgeYesNo::Unknown:
        default:
            LOG_WARN("{}LLM Review: Unrecognized answer (expected 是/否): {}", kTag, text);
            return result;
    }
}

bool TaskAlarm::LlmReviewAlarm(const DataAlarmUnit& alarmUnit, const VideoFramePtr& frame) {
    if (!frame || !frame->Active()) {
        LOG_WARN("{}[{}] LLM Review: frame is null, skip review (allow alarm)", kTag, task_id);
        return true;
    }
    if (!InitLlmReviewer()) {
        LOG_WARN("{}[{}] LLM Review: Init failed, skip review (allow alarm)", kTag, task_id);
        return true;
    }

    auto roi      = PrepareAlarmReviewRoi(frame, alarmUnit.box);
    auto bgrFrame = roi.frame;
    if (!VideoFrameValid(bgrFrame)) {
        LOG_WARN("{}[{}] LLM Review: ROI preparation failed, skip (allow alarm)", kTag, task_id);
        return true;
    }

    std::string prompt = BuildLlmReviewPrompt(alarmUnit);
    LOG_INFO("{}[{}] LLM Review: prompt:{}", kTag, task_id, prompt);
    std::vector<VideoFramePtr> images = {bgrFrame};
    std::vector<std::string> prompts  = {prompt};
    std::vector<Qwen3VLResult> results;
    auto ret =
        m_param.llmOpenAiConfig.Enabled()
            ? OpenAiVlmClient::Generate(m_param.llmOpenAiConfig, images, prompts, kLlmReviewGenParam, results)
            : service::ServiceRegistry::Instance().Get<service::ILlmInferService>().Generate(
                  images, prompts, kLlmReviewGenParam, results);
    if (ret != util::ErrorEnum::Success || results.empty()) {
        LOG_WARN("{}[{}] LLM Review: Generate failed, error:{}, skip (allow alarm)", kTag, task_id,
                 static_cast<int>(ret));
        return true;
    }
    auto reviewResult = ParseLlmReviewResult(results[0].text);
    LOG_INFO("{}[{}] LLM Review: is_valid={} (determines whether to report)", kTag, task_id,
             reviewResult.is_valid);
    return reviewResult.is_valid;
}
// ===================== LLM review related methods end =====================

}  // namespace cosmo
