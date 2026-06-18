#pragma once

#include <string>
#include <vector>

namespace cosmo::nn {
namespace qwen3vl {

    // Build prompt text with image placeholders
    std::string BuildImagePrompt(const std::string& input_str, const std::vector<std::vector<int>>& grid_thw,
                                 bool append_empty_think = false);

    // Find indices equal to pad_id in input_ids
    std::vector<int> FindTokenOffset(const std::vector<int>& input_ids, int pad_id);

    // Generate 3D position_ids for prefill (consistent with get_rope_index)
    std::vector<std::vector<int>> GetRopeIndex(const std::vector<int>& input_ids,
                                               const std::vector<std::vector<int>>& grid_thw, int pad_id,
                                               int id_vision_start, int spatial_merge_size,
                                               int tokens_per_second);

    // rot_pos: consistent with pipeline ChatPipe::rot_pos, returns flattened position_ids list
    std::vector<std::vector<int>> RotPos(const std::vector<std::vector<int>>& grid_thw,
                                         int spatial_merge_size);

    // fast_pos_embed_interpolate: compute VIT positional encoding interpolation indices and weights
    void FastPosEmbedInterpolate(const std::vector<int>& grid_thw, int num_grid, int spatial_merge_size,
                                 std::vector<int>& idx_out, std::vector<float>& weight_out);

}  // namespace qwen3vl
}  // namespace cosmo::nn
