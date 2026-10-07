#pragma once

#include <future>

#include "service/ai/IVisualDecisionService.h"

namespace cosmo::service {

struct VisualQuestionSpec {
    std::string itemId;
    // choice criteria are an ordered [{label,description}, ...] array; never an
    // unordered JSON object. noul criteria retain explicit false/true meanings.
    nlohmann::json question;
    std::string textState;
};

struct VisualQuestionPreparation {
    bool ready{false};
    bool cacheHit{false};
    std::string reason;
    std::string manifestSha256;
    std::string inputSha256;
    std::vector<VisualQuestionRef> questions;
};

class IVisualQuestionService {
public:
    virtual ~IVisualQuestionService() = default;
    // Asynchronous configuration preparation, never called for every frame.
    // A single compiler process owns the large tokenizer allocation. The caller
    // activates all returned refs atomically through run->CommitIfCurrent().
    // Up to 512 catalog entries; native compiler jobs remain <=32 questions and
    // share one absolute deadline. No partial catalog is activated.
    virtual std::shared_future<VisualQuestionPreparation> Prepare(
        std::vector<VisualQuestionSpec> questions, std::shared_ptr<VisualDecisionRun> run,
        std::chrono::milliseconds timeout = std::chrono::milliseconds(60000)) = 0;
};

}  // namespace cosmo::service
