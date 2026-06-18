#pragma once

#include <random>
#include <string>
#include <vector>

#include "bmlib_runtime.h"
#include "bmruntime_interface.h"

namespace cosmo::nn {
namespace qwen3vl {

    struct GenerationConfig {
        std::vector<int> eos_token_id;
        float repetition_penalty = 1.0f;
        float temperature        = 1.0f;
        int top_k                = 50;
        float top_p              = 1.0f;
        std::vector<std::string> stop_strings;
        /** Read from standalone generation_config.json file (backward compatible) */
        static GenerationConfig from_json(const std::string& path);
        /** Read from model config JSON (config_qwen3vl.json) config.generation section, consistent with
         * config_json convention */
        static GenerationConfig from_model_json(const std::string& path);
    };

    using ArrayInt   = std::vector<int>;
    using ArrayFloat = std::vector<float>;

    class Qwen3VLModel {
    public:
        Qwen3VLModel();
        ~Qwen3VLModel();
        /** @param model_config_json_path Model config JSON path (e.g. config_qwen3vl.json), when non-empty
         * reads from config.generation for generation params; tokenizer etc. loaded by runner from
         * config_dir, no directory passed here */
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

        /** Used by runner to copy device image blob to host, returns handle passable to bm_memcpy_d2s */
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
        bool is_dynamic       = false;
        bool vit_dynamic      = false;
        bool hybrid_fa_cache  = false;
        uint16_t mask_value   = 0;
        bool vit_run          = false;
        int num_deepstack     = 0;
        std::vector<int> visited_tokens;
        bool do_sample = false;
        std::vector<std::string> stop_strings;
        const int FA_INTERVAL = 4;
        float penalty         = 1.0f;
        float temperature     = 1.0f;
        int top_k             = 50;
        float top_p           = 1.0f;

    private:
        void net_launch(const bm_net_info_t* net, const std::vector<bm_tensor_t>& in_tensors,
                        std::vector<bm_tensor_t>& out_tensors);
        void net_launch_decode(int block_idx, int kv_offset, bm_device_mem_t& input_mem,
                               const int* position_id, std::vector<uint16_t>& attention_mask);
        void d2d(bm_device_mem_t& dst, bm_device_mem_t& src, int offset = 0, int size = 0);
        void init_by_names();
        int forward_first_with_kv(const ArrayInt& position_ids);
        int generate(bm_device_mem_t& logits_mem);
        int greedy_search(bm_device_mem_t& logits_mem);
        int penalty_sample(bm_device_mem_t& logits_mem);
        void init_tensors(const bm_net_info_t* net, std::vector<bm_tensor_t>& in_tensors,
                          std::vector<bm_tensor_t>& out_tensors, int stage = 0);
        bool is_fa_layer(int layer_idx) const {
            return !hybrid_fa_cache || (layer_idx + 1) % FA_INTERVAL == 0;
        }
        int first_kv_layer() const {
            return hybrid_fa_cache ? FA_INTERVAL - 1 : 0;
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
        const bm_net_info_t* net_add_         = nullptr;
        const bm_net_info_t* net_greedy_head_ = nullptr;
        const bm_net_info_t* net_sample_head_ = nullptr;
        bm_device_mem_t dev_buffer_           = {0};
        std::vector<bm_device_mem_t> deepstack_buffers_;
        std::vector<bm_device_mem_t> past_key_;
        std::vector<bm_device_mem_t> past_value_;
    };

}  // namespace qwen3vl
}  // namespace cosmo::nn
