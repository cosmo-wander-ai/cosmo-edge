#pragma once

#include <cstddef>

namespace cosmo::vlm_evaluation {

// Validate the observed deployment count without requiring cross-platform token equality.
inline bool TokenCountsWithinBudget(int input_tokens, int output_tokens, int context_length,
                                    int max_new_tokens) {
    return input_tokens > 0 && output_tokens >= 0 && max_new_tokens > 0 && context_length > max_new_tokens &&
           input_tokens <= context_length - max_new_tokens && output_tokens <= max_new_tokens;
}

// NORMAL callbacks carry generated IDs; FINISH may carry a non-token sentinel.
inline bool CallbackOutputWithinBudget(size_t callback_tokens, int max_new_tokens) {
    return max_new_tokens > 0 && callback_tokens <= static_cast<size_t>(max_new_tokens);
}

// Reaching a budget does not establish why the SDK finished or imply truncation.
inline bool OutputBudgetReached(size_t callback_tokens, int max_new_tokens) {
    return max_new_tokens > 0 && callback_tokens >= static_cast<size_t>(max_new_tokens);
}

}  // namespace cosmo::vlm_evaluation
