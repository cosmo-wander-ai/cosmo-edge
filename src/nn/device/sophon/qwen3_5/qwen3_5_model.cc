#include "nn/device/sophon/qwen3_5/qwen3_5_model.h"

#include <assert.h>

#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>

#include "bmlib_runtime.h"
#include "bmruntime_interface.h"

namespace cosmo::nn {
namespace qwen3_5 {

    static void empty(bm_handle_t& bm_handle, bm_device_mem_t& mem) {
        int value = 0;
        auto ret  = bm_memset_device_ext(bm_handle, &value, 1, mem);
        assert(BM_SUCCESS == ret);
    }

    static void empty_net(bm_handle_t& bm_handle, const bm_net_info_t* net, int stage = 0) {
        for (int i = 0; i < net->input_num; i++)
            empty(bm_handle, net->stages[stage].input_mems[i]);
        for (int i = 0; i < net->output_num; i++)
            empty(bm_handle, net->stages[stage].output_mems[i]);
    }

    static bool ends_with(const std::string& str, const std::string& suffix) {
        if (str.size() < suffix.size())
            return false;
        return std::equal(suffix.rbegin(), suffix.rend(), str.rbegin());
    }

    Qwen3_5Model::Qwen3_5Model() : sgen_(std::random_device{}()) {}

    Qwen3_5Model::~Qwen3_5Model() {
        deinit();
    }

    void Qwen3_5Model::init_tensors(const bm_net_info_t* net, std::vector<bm_tensor_t>& in_tensors,
                                    std::vector<bm_tensor_t>& out_tensors, int stage) {
        in_tensors.resize(net->input_num);
        out_tensors.resize(net->output_num);
        for (int i = 0; i < net->input_num; i++) {
            bmrt_tensor_with_device(&in_tensors[i], net->stages[stage].input_mems[i], net->input_dtypes[i],
                                    net->stages[stage].input_shapes[i]);
        }
        for (int i = 0; i < net->output_num; i++) {
            bmrt_tensor_with_device(&out_tensors[i], net->stages[stage].output_mems[i], net->output_dtypes[i],
                                    net->stages[stage].output_shapes[i]);
        }
    }

    bool Qwen3_5Model::check_stop(const std::string& text) {
        for (const auto& stop_str : stop_strings) {
            if (ends_with(text, stop_str))
                return true;
        }
        return false;
    }

    void Qwen3_5Model::net_launch(const bm_net_info_t* net, const std::vector<bm_tensor_t>& in_tensors,
                                  std::vector<bm_tensor_t>& out_tensors) {
        (void)bmrt_launch_tensor_ex(p_bmrt_, net->name, in_tensors.data(), net->input_num, out_tensors.data(),
                                    net->output_num, true, false);
    }

    void Qwen3_5Model::net_launch_decode(int idx, int kv_offset, bm_device_mem_t& input_mem,
                                         const int* pos_id, std::vector<uint16_t>& attention_mask) {
        const bm_net_info_t* net = net_blocks_cache_[idx];
        std::vector<bm_tensor_t> in_tensors;
        std::vector<bm_tensor_t> out_tensors;
        init_tensors(net, in_tensors, out_tensors);
        in_tensors[0].device_mem = input_mem;
        bm_memcpy_s2d(bm_handle_, in_tensors[1].device_mem, (void*)pos_id);
        bm_memcpy_s2d(bm_handle_, in_tensors[2].device_mem, (void*)attention_mask.data());
        out_tensors[1].device_mem =
            bm_mem_from_device(past_key_[idx].u.device.device_addr + kv_offset, KV_BYTES);
        out_tensors[2].device_mem =
            bm_mem_from_device(past_value_[idx].u.device.device_addr + kv_offset, KV_BYTES);
        net_launch(net, in_tensors, out_tensors);
    }

    void Qwen3_5Model::d2d(bm_device_mem_t& dst, bm_device_mem_t& src, int offset, int size) {
        if (!size)
            size = bm_mem_get_device_size(src);
        bm_memcpy_d2d_byte(bm_handle_, dst, offset, src, 0, size);
    }

    void Qwen3_5Model::clear_history() {
        if (!support_history)
            return;
        for (int i = 0; i < NUM_LAYERS; i++) {
            empty(bm_handle_, past_key_[i]);
            empty(bm_handle_, past_value_[i]);
        }
        history_length = 0;
    }

    // ========== Key differences from Qwen3VLModel::init_by_names ==========
    // 1. Does not search for "add" network
    // 2. Uses net_blocks_cache_[FA_INTERVAL-1] to read SEQLEN/KV_BYTES (only FA layers have full KV)
    // 3. support_history determined from FA layer
    void Qwen3_5Model::init_by_names() {
        auto is_exist = [](const char* name, const char** names, int num) {
            for (int i = 0; i < num; i++)
                if (strcmp(name, names[i]) == 0)
                    return true;
            return false;
        };
        net_embed_       = bmrt_get_network_info(p_bmrt_, "embedding");
        net_embed_cache_ = bmrt_get_network_info(p_bmrt_, "embedding_cache");
        net_vit_         = bmrt_get_network_info(p_bmrt_, "vit");
        net_lm_          = bmrt_get_network_info(p_bmrt_, "lm_head");
        // Qwen3.5: no "add" network
        const char** net_names = nullptr;
        int num_nets           = bmrt_get_network_number(p_bmrt_);
        bmrt_get_network_names(p_bmrt_, &net_names);
        net_greedy_head_ = nullptr;
        // 4 nets: embedding, embedding_cache, vit, lm_head
        int num_blocks = num_nets - 4;
        if (is_exist("greedy_head", net_names, num_nets)) {
            net_greedy_head_ = bmrt_get_network_info(p_bmrt_, "greedy_head");
            num_blocks--;
        }
        net_sample_head_ = nullptr;
        if (is_exist("sample_head", net_names, num_nets)) {
            net_sample_head_ = bmrt_get_network_info(p_bmrt_, "sample_head");
            num_blocks--;
        }
        NUM_LAYERS = num_blocks / 2;
        for (int i = 0; i < NUM_LAYERS; i++) {
            std::string block_name = "block_" + std::to_string(i);
            std::string cache_name = "block_cache_" + std::to_string(i);
            if (!is_exist(block_name.c_str(), net_names, num_nets) ||
                !is_exist(cache_name.c_str(), net_names, num_nets)) {
                NUM_LAYERS = i;
                break;
            }
            net_blocks_.push_back(bmrt_get_network_info(p_bmrt_, block_name.c_str()));
            net_blocks_cache_.push_back(bmrt_get_network_info(p_bmrt_, cache_name.c_str()));
        }
        free(net_names);
        if (net_embed_cache_->output_dtypes[0] == BM_FLOAT16)
            mask_value = 0xF0E2;
        else if (net_embed_cache_->output_dtypes[0] == BM_BFLOAT16)
            mask_value = 0xC61C;
        else
            throw std::runtime_error("Invalid attention dtype");
        // Qwen3.5: determine support_history and read SEQLEN from FA layer
        int fa_idx        = FA_INTERVAL - 1;
        support_history   = net_blocks_[fa_idx]->input_num == 5;
        history_length    = 0;
        lmhead_with_topk  = net_lm_->stages[0].output_shapes[0].dims[1] == 1;
        MAX_INPUT_LENGTH  = net_embed_->stages[0].input_shapes[0].dims[1];
        HIDDEN_SIZE       = net_lm_->stages[0].input_shapes[0].dims[1];
        SEQLEN            = net_blocks_cache_[fa_idx]->stages[0].input_shapes[3].dims[1];
        MAX_PATCHES       = net_vit_->stages[0].input_shapes[0].dims[0];
        MAX_PIXELS        = MAX_PATCHES * 16 * 16;
        VIT_DIMS          = net_vit_->stages[0].input_shapes[0].dims[1];
        KV_BYTES          = bm_mem_get_device_size(net_blocks_cache_[fa_idx]->stages[0].output_mems[1]);
        PREFILL_KV_LENGTH = 0;
        if (support_history)
            PREFILL_KV_LENGTH = net_blocks_[fa_idx]->stages[0].input_shapes[3].dims[1];
    }

    void Qwen3_5Model::init(int dev_id, const std::string& model_path, bool do_sample_,
                            const std::string& model_config_json_path) {
        bm_status_t status = bm_dev_request(&bm_handle_, dev_id);
        assert(BM_SUCCESS == status);
        p_bmrt_ = bmrt_create(bm_handle_);
        assert(p_bmrt_ != nullptr);
        bmrt_set_flags(p_bmrt_, BM_RUNTIME_SHARE_MEM);
        bool ret = bmrt_load_bmodel(p_bmrt_, model_path.c_str());
        assert(ret);
        bm_thread_sync(bm_handle_);
        init_by_names();
        visited_tokens.resize(SEQLEN);
        // ========== KV cache init: branch by is_FA ==========
        past_key_.resize(NUM_LAYERS);
        past_value_.resize(NUM_LAYERS);
        for (int i = 0; i < NUM_LAYERS; i++) {
            if (is_FA(i)) {
                // Full Attention layer: standard KV cache
                past_key_[i]   = net_blocks_cache_[i]->stages[0].input_mems[3];
                past_value_[i] = net_blocks_cache_[i]->stages[0].input_mems[4];
            } else {
                // Linear Attention layer: conv state / recurrent state
                past_key_[i]   = net_blocks_cache_[i]->stages[0].input_mems[1];
                past_value_[i] = net_blocks_cache_[i]->stages[0].input_mems[2];
            }
            empty(bm_handle_, past_key_[i]);
            empty(bm_handle_, past_value_[i]);
        }
        size_t buffer_size = bm_mem_get_device_size(net_embed_->stages[0].output_mems[0]);
        status             = bm_malloc_device_byte(bm_handle_, &dev_buffer_, buffer_size);
        assert(BM_SUCCESS == status);
        // Qwen3.5: no deepstack buffer
        do_sample = do_sample_;
        if (do_sample && net_sample_head_) {
            qwen3vl::GenerationConfig gen_config;
            if (!model_config_json_path.empty())
                gen_config = qwen3vl::GenerationConfig::from_model_json(model_config_json_path);
            penalty     = gen_config.repetition_penalty;
            temperature = gen_config.temperature;
            top_k       = gen_config.top_k;
            top_p       = gen_config.top_p;
            if (!gen_config.stop_strings.empty())
                stop_strings = gen_config.stop_strings;
            bm_memcpy_s2d(bm_handle_, net_sample_head_->stages[0].input_mems[2], (void*)&penalty);
            bm_memcpy_s2d(bm_handle_, net_sample_head_->stages[0].input_mems[3], (void*)&temperature);
            bm_memcpy_s2d(bm_handle_, net_sample_head_->stages[0].input_mems[4], (void*)&top_k);
            bm_memcpy_s2d(bm_handle_, net_sample_head_->stages[0].input_mems[5], (void*)&top_p);
        }
    }

    void Qwen3_5Model::deinit() {
        if (!p_bmrt_)
            return;
        bm_free_device(bm_handle_, dev_buffer_);
        bmrt_destroy(p_bmrt_);
        bm_dev_free(bm_handle_);
        p_bmrt_    = nullptr;
        bm_handle_ = nullptr;
    }

    int Qwen3_5Model::greedy_search(bm_device_mem_t& logits_mem) {
        std::vector<bm_tensor_t> in_tensors;
        std::vector<bm_tensor_t> out_tensors;
        init_tensors(net_greedy_head_, in_tensors, out_tensors);
        in_tensors[0].device_mem = logits_mem;
        net_launch(net_greedy_head_, in_tensors, out_tensors);
        int token = 0;
        bm_memcpy_d2s(bm_handle_, (void*)&token, out_tensors[0].device_mem);
        return token;
    }

    int Qwen3_5Model::penalty_sample(bm_device_mem_t& logits_mem) {
        std::vector<bm_tensor_t> in_tensors;
        std::vector<bm_tensor_t> out_tensors;
        init_tensors(net_sample_head_, in_tensors, out_tensors);
        in_tensors[0].device_mem = logits_mem;
        bm_memcpy_s2d_partial(bm_handle_, in_tensors[1].device_mem, (void*)visited_tokens.data(),
                              token_length * sizeof(int));
        in_tensors[1].shape.dims[1] = token_length;
        net_launch(net_sample_head_, in_tensors, out_tensors);
        std::vector<float> probs(top_k);
        std::vector<int> tokens(top_k);
        bm_memcpy_d2s_partial_offset(bm_handle_, probs.data(), out_tensors[0].device_mem,
                                     top_k * sizeof(float), 0);
        bm_memcpy_d2s_partial_offset(bm_handle_, tokens.data(), out_tensors[1].device_mem,
                                     top_k * sizeof(float), 0);
        std::discrete_distribution<> dist(probs.begin(), probs.end());
        return tokens[dist(sgen_)];
    }

    int Qwen3_5Model::generate(bm_device_mem_t& logits_mem) {
        if (lmhead_with_topk) {
            int token = 0;
            bm_memcpy_d2s_partial(bm_handle_, (void*)&token, logits_mem, sizeof(int));
            return token;
        }
        if (do_sample && net_sample_head_)
            return penalty_sample(logits_mem);
        return greedy_search(logits_mem);
    }

    void Qwen3_5Model::forward_embed(const ArrayInt& tokens) {
        std::fill(visited_tokens.begin(), visited_tokens.end(), 0);
        std::copy(tokens.begin(), tokens.end(), visited_tokens.data());
        std::vector<bm_tensor_t> in_tensors;
        std::vector<bm_tensor_t> out_tensors;
        init_tensors(net_embed_, in_tensors, out_tensors);
        bm_memcpy_s2d_partial(bm_handle_, in_tensors[0].device_mem, (void*)visited_tokens.data(),
                              MAX_INPUT_LENGTH * sizeof(int));
        net_launch(net_embed_, in_tensors, out_tensors);
        empty(bm_handle_, dev_buffer_);
        d2d(dev_buffer_, out_tensors[0].device_mem, 0, tokens.size() * HIDDEN_SIZE * sizeof(uint16_t));
        token_length = static_cast<int>(tokens.size());
    }

    // ========== ViT: similar to Qwen3VLModel but dynamic mode only, no deepstack ==========
    void Qwen3_5Model::forward_vit(const float* pixel_values, const ArrayInt& position_ids,
                                   const ArrayInt& pos_idx, const ArrayFloat& pos_weight,
                                   const ArrayInt& grid_thw, int vit_offset) {
        int t = grid_thw[0], h = grid_thw[1], w = grid_thw[2];
        int hw         = t * h * w;
        int num_pixels = hw * VIT_DIMS;
        empty_net(bm_handle_, net_vit_);
        std::vector<bm_tensor_t> in_tensors;
        std::vector<bm_tensor_t> out_tensors;
        init_tensors(net_vit_, in_tensors, out_tensors);
        bm_memcpy_s2d_partial(bm_handle_, in_tensors[0].device_mem, (void*)pixel_values,
                              num_pixels * sizeof(float));
        bm_memcpy_s2d_partial(bm_handle_, in_tensors[1].device_mem, (void*)position_ids.data(),
                              position_ids.size() * sizeof(int));
        bm_memcpy_s2d_partial(bm_handle_, in_tensors[2].device_mem, (void*)pos_idx.data(),
                              pos_idx.size() * sizeof(int));
        bm_memcpy_s2d_partial(bm_handle_, in_tensors[3].device_mem, (void*)pos_weight.data(),
                              pos_weight.size() * sizeof(float));
        // Qwen3.5: dynamic ViT only
        in_tensors[0].shape.dims[0] = hw;
        in_tensors[1].shape.dims[0] = hw;
        in_tensors[2].shape.dims[0] = hw;
        in_tensors[3].shape.dims[0] = hw;
        net_launch(net_vit_, in_tensors, out_tensors);
        int dst_offset = vit_offset * HIDDEN_SIZE * sizeof(uint16_t);
        int vit_size   = hw / 4 * HIDDEN_SIZE * sizeof(uint16_t);
        bm_memcpy_d2d_byte(bm_handle_, dev_buffer_, dst_offset, out_tensors[0].device_mem, 0, vit_size);
        // No deepstack buffer to process
    }

    // ========== forward_first: core difference -- branch by is_FA ==========
    int Qwen3_5Model::forward_first(const ArrayInt& position_ids) {
        const int* p_ids = position_ids.data();
        std::vector<int> position_ids_pad;
        std::vector<uint16_t> attention_mask(token_length * token_length, mask_value);
        for (int i = 0; i < token_length; i++)
            for (int j = 0; j <= i; j++)
                attention_mask[i * token_length + j] = 0;
        position_ids_pad.assign(3 * token_length, 0);
        std::copy(p_ids, p_ids + token_length * 3, position_ids_pad.begin());

        auto out_mem = dev_buffer_;
        empty_net(bm_handle_, net_blocks_[0]);
        std::vector<bm_tensor_t> in_tensors;
        std::vector<bm_tensor_t> out_tensors;
        for (int idx = 0; idx < NUM_LAYERS; idx++) {
            init_tensors(net_blocks_[idx], in_tensors, out_tensors);
            d2d(in_tensors[0].device_mem, out_mem, 0, token_length * HIDDEN_SIZE * sizeof(uint16_t));
            if (is_FA(idx)) {
                // Full Attention layer: requires position_ids and attention_mask
                bm_memcpy_s2d_partial(bm_handle_, in_tensors[1].device_mem, (void*)position_ids_pad.data(),
                                      token_length * 3 * sizeof(int));
                bm_memcpy_s2d_partial(bm_handle_, in_tensors[2].device_mem, (void*)attention_mask.data(),
                                      token_length * token_length * sizeof(uint16_t));
                in_tensors[0].shape.dims[1] = token_length;
                in_tensors[1].shape.dims[1] = token_length;
                in_tensors[2].shape.dims[2] = token_length;
                in_tensors[2].shape.dims[3] = token_length;
            } else {
                // Linear Attention layer: only needs input + clear recurrent state
                in_tensors[0].shape.dims[1] = token_length;
                empty(bm_handle_, in_tensors[1].device_mem);
            }
            net_launch(net_blocks_[idx], in_tensors, out_tensors);
            out_mem = net_blocks_[idx]->stages[0].output_mems[0];
            // KV cache writeback: branch by layer type
            if (is_FA(idx)) {
                bm_memcpy_d2d_byte(bm_handle_, past_key_[idx], 0, net_blocks_[idx]->stages[0].output_mems[1],
                                   0, KV_BYTES * token_length);
                bm_memcpy_d2d_byte(bm_handle_, past_value_[idx], 0,
                                   net_blocks_[idx]->stages[0].output_mems[2], 0, KV_BYTES * token_length);
            } else {
                // Linear Attention: conv state + recurrent state
                d2d(past_key_[idx], net_blocks_[idx]->stages[0].output_mems[1]);
                d2d(past_value_[idx], net_blocks_[idx]->stages[0].input_mems[1]);
            }
        }
        // forward lm_head
        int bytes = HIDDEN_SIZE * sizeof(uint16_t);
        init_tensors(net_lm_, in_tensors, out_tensors);
        in_tensors[0].device_mem =
            bm_mem_from_device(out_mem.u.device.device_addr + (token_length - 1) * bytes, bytes);
        out_tensors[0].device_mem = dev_buffer_;
        net_launch(net_lm_, in_tensors, out_tensors);
        int token                    = generate(dev_buffer_);
        visited_tokens[token_length] = token;
        token_length++;
        history_length = token_length;
        return token;
    }

    // ========== forward_next: core difference -- FA layers use decode, non-FA layers launch directly
    // ==========
    int Qwen3_5Model::forward_next(const ArrayInt& position_ids) {
        std::vector<uint16_t> attention_mask(SEQLEN + 1, 0);
        for (int i = history_length - 1; i < SEQLEN; i++)
            attention_mask[i] = mask_value;
        const int* p_ids = position_ids.data();
        std::vector<bm_tensor_t> in_tensors;
        std::vector<bm_tensor_t> out_tensors;
        init_tensors(net_embed_cache_, in_tensors, out_tensors);
        int token = visited_tokens[token_length - 1];
        bm_memcpy_s2d(bm_handle_, in_tensors[0].device_mem, (void*)&token);
        net_launch(net_embed_cache_, in_tensors, out_tensors);
        auto out_mem = out_tensors[0].device_mem;
        // KV_BYTES for FA layer
        int fa_bytes = bm_mem_get_device_size(net_blocks_cache_[FA_INTERVAL - 1]->stages[0].output_mems[1]);
        int token_offset = (history_length - 1) * fa_bytes;
        for (int idx = 0; idx < NUM_LAYERS; idx++) {
            if (is_FA(idx)) {
                // Full Attention layer: use net_launch_decode (with KV cache)
                net_launch_decode(idx, token_offset, out_mem, p_ids, attention_mask);
            } else {
                // Linear Attention layer: direct launch (state in input_mems)
                init_tensors(net_blocks_cache_[idx], in_tensors, out_tensors);
                in_tensors[0].device_mem = out_mem;
                net_launch(net_blocks_cache_[idx], in_tensors, out_tensors);
            }
            out_mem = net_blocks_cache_[idx]->stages[0].output_mems[0];
        }
        // forward lm_head
        init_tensors(net_lm_, in_tensors, out_tensors);
        in_tensors[0].device_mem  = out_mem;
        out_tensors[0].device_mem = dev_buffer_;
        net_launch(net_lm_, in_tensors, out_tensors);
        token                        = generate(dev_buffer_);
        visited_tokens[token_length] = token;
        token_length++;
        history_length++;
        return token;
    }

}  // namespace qwen3_5
}  // namespace cosmo::nn
