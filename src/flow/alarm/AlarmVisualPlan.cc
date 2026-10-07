#include "flow/alarm/AlarmVisualPlan.h"

namespace cosmo {
AlarmReviewSubject SelectAlarmReviewSubject(const DataAlarmUnit& alarm, const std::string& custom,
                                            const std::string& algorithmName) {
    if (!custom.empty())
        return {custom, "custom"};
    if (!alarm.confidence.empty() && !alarm.confidence.front().label.empty())
        return {alarm.confidence.front().label, "confidence_label"};
    if (!alarm.attrRsts.empty() && !alarm.attrRsts.front().label.empty())
        return {alarm.attrRsts.front().label, "attribute_label"};
    if (!algorithmName.empty())
        return {algorithmName, "algorithm_name"};
    return {"", "generic"};
}

std::string AlarmReviewInstruction(const AlarmReviewSubject& subject) {
    const std::string prefix = "告警审核。图片为按告警框裁剪后的目标图。";
    if (subject.source == "generic")
        return prefix + "判断图片中是否存在有效目标或对应行为。";
    return prefix + "判断图片中是否存在【" + subject.text + "】" +
           (subject.source == "algorithm_name" ? "相关的有效目标或对应行为。" : "目标或对应行为。");
}

namespace {
    std::optional<std::map<std::string, std::string>> LabelPrompts(const std::string& custom,
                                                                   const std::vector<std::string>& labels) {
        if (!custom.empty())
            return std::nullopt;
        std::map<std::string, std::string> result;
        for (const auto& label : labels)
            if (!label.empty())
                result.emplace(label, AlarmReviewInstruction({label, "confidence_label"}));
        return result;
    }
}  // namespace

AlarmVisualPlan::AlarmVisualPlan(const std::string& task, const std::string& custom,
                                 const std::string& algorithmName,
                                 const std::vector<std::string>& modelLabels,
                                 const VisualParameters& parameters, const std::vector<MsgTaskArea>& areas)
    : custom_(custom),
      algorithmName_(algorithmName),
      judgment_(task, AlarmReviewInstruction(SelectAlarmReviewSubject({}, custom, algorithmName)), true,
                parameters, areas, LabelPrompts(custom, modelLabels)) {}

AlarmReviewSubject AlarmVisualPlan::Subject(const DataAlarmUnit& alarm) const {
    return SelectAlarmReviewSubject(alarm, custom_, algorithmName_);
}

service::VisualDecisionResult AlarmVisualPlan::Decide(
    const DataAlarmUnit& alarm, const std::string& frameId, const std::string& roiId,
    service::IVisualDecisionService::Prepare prepare) const {
    const auto subject     = Subject(alarm);
    const auto semanticKey = subject.source == "confidence_label" || subject.source == "attribute_label"
                                 ? subject.text
                                 : std::string();
    return judgment_.Decide(frameId, roiId, alarm.areaId, std::move(prepare), semanticKey);
}
}  // namespace cosmo
