#include "nn/device/sophon/qwen3vl/qwen3vl_config.h"

namespace cosmo::nn {
namespace qwen3vl {

    std::vector<int> Maker::make_next_position_id() {
        config_.max_pos += 1;
        return {config_.max_pos, config_.max_pos, config_.max_pos};
    }

}  // namespace qwen3vl
}  // namespace cosmo::nn
