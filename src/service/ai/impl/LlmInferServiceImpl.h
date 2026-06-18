// LLM inference service implementation — wraps the Qwen3VL model
// with thread-safe lifecycle management and serialized inference.
// Logic migrated from cosmo::Qwen3VLSharedInst.

#pragma once

#include <atomic>
#include <mutex>
#include <string>

#include "infer/Qwen3VLUnify.h"
#include "service/ai/ILlmInferService.h"

namespace cosmo::service {

class LlmInferServiceImpl : public ILlmInferService {
public:
    LlmInferServiceImpl()           = default;
    ~LlmInferServiceImpl() override = default;

    LlmInferServiceImpl(const LlmInferServiceImpl&)            = delete;
    LlmInferServiceImpl& operator=(const LlmInferServiceImpl&) = delete;

    bool EnsureInit(const std::string& atomic_code) override;
    [[nodiscard]] bool IsInitialized() const override;
    cosmo::util::ErrorEnum Generate(const std::vector<VideoFramePtr>& images,
                                    const std::vector<std::string>& prompts,
                                    const cosmo::Qwen3VLGenerationParam& gen_param,
                                    std::vector<cosmo::Qwen3VLResult>& results) override;
    cosmo::util::ErrorEnum GetMaxBatchSize(size_t& value) const override;
    void Reset() override;
    void NotifyWorkerStart() override;
    void NotifyWorkerStop() override;

private:
    mutable std::mutex lifecycle_mtx_;         // Protects inst_, init state
    mutable std::mutex infer_mtx_;             // Serializes inference calls (AISDK is not thread-safe)
    cosmo::Qwen3VLUnifyPtr inst_;              // The sole model instance
    std::string atomic_code_;                  // Currently loaded algorithm code
    bool init_failed_{false};                  // Once set, no retry until Reset()
    std::atomic<int> active_worker_count_{0};  // Active video analysis workers
};

}  // namespace cosmo::service
