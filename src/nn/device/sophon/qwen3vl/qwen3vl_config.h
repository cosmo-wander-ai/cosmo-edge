#pragma once

#include <vector>

namespace cosmo::nn {
namespace qwen3vl {

    struct Config {
        int SEQLEN             = 0;
        int MAX_PREFILL_LENGTH = 0;
        int MAX_INPUT_LENGTH   = 0;
        int total_length       = 0;
        int max_pos            = 0;
        int MAX_PATCHES        = 0;
        int MAX_PIXELS         = 0;
        int MIN_PIXELS         = 0;
        std::vector<int> grid_thw;
        int media_offset        = 0;
        int media_size          = 0;
        int spatial_merge_size  = 2;
        int patch_size          = 16;
        int temporal_patch_size = 2;
        float video_ratio       = 0.25f;
        float video_fps         = 1.0f;
    };

    class Maker {
    public:
        explicit Maker(Config& config) : config_(config) {}
        std::vector<int> make_next_position_id();

    private:
        Config& config_;
    };

}  // namespace qwen3vl
}  // namespace cosmo::nn
