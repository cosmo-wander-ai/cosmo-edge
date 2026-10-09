#pragma once

#include "flow/common/AlgAlarmTypes.h"
#include "flow/common/VisualJudgment.h"

namespace cosmo {
struct AlarmReviewSubject {
    std::string text;
    std::string source;
};

// Preserve the legacy review selection order without parsing generated text.
AlarmReviewSubject SelectAlarmReviewSubject(const DataAlarmUnit& alarm, const std::string& custom,
                                            const std::string& algorithmName);
std::string AlarmReviewInstruction(const AlarmReviewSubject& subject);

// Prepared once when alarm configuration/model context changes. Every known
// upstream label is compiled before activation; per-frame calls only select it.
class AlarmVisualPlan {
public:
    AlarmVisualPlan(const std::string& task, const std::string& custom, const std::string& algorithmName,
                    const std::vector<std::string>& modelLabels, const VisualParameters& parameters,
                    const std::vector<MsgTaskArea>& areas, const std::string& atomicCode = "");
    void Invalidate() {
        judgment_.Invalidate();
    }
    std::shared_ptr<service::VisualDecisionRun> Run() const {
        return judgment_.Run();
    }
    service::VisualDecisionResult Decide(const DataAlarmUnit& alarm, const std::string& frameId,
                                         const std::string& roiId,
                                         service::IVisualDecisionService::Prepare prepare) const;
    AlarmReviewSubject Subject(const DataAlarmUnit& alarm) const;

private:
    const std::string custom_;
    const std::string algorithmName_;
    VisualJudgment judgment_;
};
}  // namespace cosmo
