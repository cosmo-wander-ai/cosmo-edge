// LLM inference service interface — provides thread-safe access
// to the shared Qwen3VL large language model instance.

#pragma once

#include <string>
#include <vector>

#include "infer/Qwen3VLUnify.h"
#include "media/VideoFrame.h"
#include "service/detail/ServiceRegistry.h"
#include "util/ErrorCode.h"

namespace cosmo::service {

/// Thread-safe access to the shared Qwen3VL LLM inference model.
///
/// The BM1688 SoC can only host one Qwen3VL model instance due to NPU memory
/// constraints. This service manages lazy initialization, reference-counted
/// lifecycle (worker start/stop), and serialized inference calls. Replaces
/// the former cosmo::Qwen3VLSharedInst singleton.
class ILlmInferService {
public:
    virtual ~ILlmInferService() = default;

    /// Ensures the model is loaded (thread-safe, lazy init on first call).
    [[nodiscard]] virtual bool EnsureInit(const std::string& atomic_code) = 0;

    /// Returns true if the model instance is currently loaded.
    [[nodiscard]] virtual bool IsInitialized() const = 0;

    /// Thread-safe Generate call (internally serializes inference).
    virtual cosmo::util::ErrorEnum Generate(const std::vector<VideoFramePtr>& images,
                                            const std::vector<std::string>& prompts,
                                            const cosmo::Qwen3VLGenerationParam& gen_param,
                                            std::vector<cosmo::Qwen3VLResult>& results) = 0;

    /// Returns the maximum batch size supported by the loaded model.
    virtual cosmo::util::ErrorEnum GetMaxBatchSize(size_t& value) const = 0;

    /// Releases the model instance and frees VRAM; allows re-initialization.
    virtual void Reset() = 0;

    /// Called when a video analysis worker starts using the LLM.
    virtual void NotifyWorkerStart() = 0;

    /// Called when a video analysis worker stops; auto-resets when count reaches zero.
    virtual void NotifyWorkerStop() = 0;
};

}  // namespace cosmo::service
