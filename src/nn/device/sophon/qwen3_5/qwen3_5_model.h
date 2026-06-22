#pragma once

#include <random>
#include <string>
#include <vector>

#include "bmlib_runtime.h"
#include "bmruntime_interface.h"

// Reuse Qwen3VL GenerationConfig definition
#include "nn/device/sophon/qwen3vl/qwen3vl_model.h"

namespace cosmo::nn {
namespace qwen3_5 {

    using ArrayInt   = std::vector<int>;
    using ArrayFloat = std::vector<float>;

    /**
     * Qwen3.5 model inference engine.
     * Same interface as Qwen3VLModel, but internally handles hybrid Attention architecture:
     * - In each FA_INTERVAL group, first 3 layers are Linear Attention, 4th is Full Attention
     * - No deepstack (net_add_) sub-network
     * - ViT uses 2D positional encoding (position_ids dimension is hw*2)
     */
    class Qwen3_5Model {
    public:
        Qwen3_5Model();
        ~Qwen3_5Model();

        void init(int dev_id, const std::string& model_path, bool do_sample,
                  const std::string& model_config_json_path = "");
        void deinit();
        void forward_embed(const ArrayInt& tokens);
        void forward_vit(const float* pixel_values, const ArrayInt& position_ids, const ArrayInt& pos_idx,
                         const ArrayFloat& pos_weight, const ArrayInt& grid_thw, int vit_offset);
        int forward_first(const ArrayInt& position_ids);
        int forward_next(const ArrayInt& position_ids);
        bool check_stop(const std::string& text);
        void clear_history();

        void* get_bm_handle() const {
            return static_cast<void*>(bm_handle_);
        }

        int token_length      = 0;
        int history_length    = 0;
        int SEQLEN            = 0;
        int MAX_INPUT_LENGTH  = 0;
        int PREFILL_KV_LENGTH = 0;
        int HIDDEN_SIZE       = 0;
        int KV_BYTES          = 0;
        int NUM_LAYERS        = 0;
        int VIT_DIMS          = 0;
        int MAX_PATCHES       = 0;
        int MAX_PIXELS        = 0;
        int max_pos           = 0;
        bool lmhead_with_topk = false;
        bool support_history  = false;
        uint16_t mask_value   = 0;
        std::vector<int> visited_tokens;
        bool do_sample = false;
        std::vector<std::string> stop_strings;
        float penalty     = 1.0f;
        float temperature = 1.0f;
        int top_k         = 50;
        float top_p       = 1.0f;

        static constexpr int FA_INTERVAL = 4;

    private:
        void net_launch(const bm_net_info_t* net, const std::vector<bm_tensor_t>& in_tensors,
                        std::vector<bm_tensor_t>& out_tensors);
        void net_launch_decode(int block_idx, int kv_offset, bm_device_mem_t& input_mem,
                               const int* position_id, std::vector<uint16_t>& attention_mask);
        void d2d(bm_device_mem_t& dst, bm_device_mem_t& src, int offset = 0, int size = 0);
        void init_by_names();
        int generate(bm_device_mem_t& logits_mem);
        int greedy_search(bm_device_mem_t& logits_mem);
        int penalty_sample(bm_device_mem_t& logits_mem);
        void init_tensors(const bm_net_info_t* net, std::vector<bm_tensor_t>& in_tensors,
                          std::vector<bm_tensor_t>& out_tensors, int stage = 0);
        inline bool is_FA(int layer_idx) const {
            return (layer_idx + 1) % FA_INTERVAL == 0;
        }

        std::mt19937 sgen_;
        bm_handle_t bm_handle_ = nullptr;
        void* p_bmrt_          = nullptr;
        std::vector<const bm_net_info_t*> net_blocks_;
        std::vector<const bm_net_info_t*> net_blocks_cache_;
        const bm_net_info_t* net_embed_       = nullptr;
        const bm_net_info_t* net_embed_cache_ = nullptr;
        const bm_net_info_t* net_lm_          = nullptr;
        const bm_net_info_t* net_vit_         = nullptr;
        // Note: Qwen3.5 has no net_add_ (deepstack)
        const bm_net_info_t* net_greedy_head_ = nullptr;
        const bm_net_info_t* net_sample_head_ = nullptr;
        bm_device_mem_t dev_buffer_           = {0};
        std::vector<bm_device_mem_t> past_key_;
        std::vector<bm_device_mem_t> past_value_;
    };

}  // namespace qwen3_5
}  // namespace cosmo::nn
