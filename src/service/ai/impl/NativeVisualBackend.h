#pragma once

#include "service/ai/ILlmInferService.h"
#include "service/ai/impl/VisualDecisionServiceImpl.h"
#include "service/ai/impl/VisualQuestionServiceImpl.h"

namespace cosmo::service {
// Reuses the standard model-repository binding and the shared local VLM instance.
VisualQuestionServiceImpl::NativeCompiler NativeVisualCompiler(ILlmInferService& llm);
VisualDecisionServiceImpl::NativeInference NativeVisualInference(ILlmInferService& llm);
}  // namespace cosmo::service
