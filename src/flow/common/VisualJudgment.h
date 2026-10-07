#pragma once

#include <map>
#include <memory>
#include <string>

#include "service/ai/IVisualQuestionService.h"
#include "util/dto/TaskAreaTypes.h"

namespace cosmo {

using VisualParameters = std::map<std::string, std::string>;

// Persisted through the ordinary task/area parameter DTOs. Each question has
// its own bounded value; never serialize the entire catalog into one field.
void UpdateVisualParameters(VisualParameters& values, const std::vector<MsgDynamicKeyValue>& updates);

// One immutable configuration and preparation future, shared by all frames in
// this task run. Invalidate before a config/area update, removal, or stop.
class VisualJudgment {
public:
    VisualJudgment(const std::string& task, const std::string& prompt, bool advanced,
                   const VisualParameters& parameters, const std::vector<MsgTaskArea>& areas);
    void Invalidate() {
        run_->Invalidate();
    }
    std::shared_ptr<service::VisualDecisionRun> Run() const {
        return run_;
    }
    service::VisualDecisionResult Decide(const std::string& frameId, const std::string& roiId,
                                         const std::string& areaId,
                                         service::IVisualDecisionService::Prepare prepare) const;

private:
    std::shared_ptr<service::VisualDecisionRun> run_;
    std::vector<service::VisualQuestionSpec> specs_;
    std::map<std::string, std::vector<std::string>> bindings_;
    std::shared_future<service::VisualQuestionPreparation> prepared_;
    std::string failure_;
    std::chrono::milliseconds timeout_{1500};
};

}  // namespace cosmo
