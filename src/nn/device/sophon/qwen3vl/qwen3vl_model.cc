#include "nn/device/sophon/qwen3vl/qwen3vl_model.h"

#include <assert.h>

#include <cstring>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <stdexcept>

#include "bmlib_runtime.h"
#include "bmruntime_interface.h"

namespace cosmo::nn {
namespace qwen3vl {
    namespace {

        constexpr std::streamoff kMaxJsonFileBytes = 10 * 1024 * 1024;

        bool LooksLikeJsonContent(const std::string& value) {
            const auto first = value.find_first_not_of(" \t\r\n");
            return first != std::string::npos && (value[first] == '{' || value[first] == '[');
        }

        nlohmann::json ParseJsonFile(const std::string& path) {
            std::ifstream in(path, std::ios::binary | std::ios::ate);
            if (!in.good())
                return nlohmann::json::object();
            const auto size = in.tellg();
            if (size < 0 || size > kMaxJsonFileBytes)
                return nlohmann::json::object();
            in.seekg(0, std::ios::beg);
            auto json = nlohmann::json::parse(in, nullptr, false);
            return json.is_discarded() || !json.is_object() ? nlohmann::json::object() : json;
        }

        nlohmann::json ParseJsonContentOrFile(const std::string& value) {
            if (LooksLikeJsonContent(value)) {
                auto json = nlohmann::json::parse(value, nullptr, false);
                if (!json.is_discarded())
                    return json.is_object() ? json : nlohmann::json::object();
            }
            return ParseJsonFile(value);
        }

        void ApplyGenerationConfig(const nlohmann::json& json, GenerationConfig& config) {
            if (json.contains("eos_token_id") && json["eos_token_id"].is_array()) {
                for (const auto& v : json["eos_token_id"]) {
                    if (v.is_number_integer())
                        config.eos_token_id.push_back(v.get<int>());
                }
            }
            if (json.contains("repetition_penalty") && json["repetition_penalty"].is_number())
                config.repetition_penalty = json["repetition_penalty"].get<float>();
            if (json.contains("temperature") && json["temperature"].is_number())
                config.temperature = json["temperature"].get<float>();
            if (json.contains("top_k") && json["top_k"].is_number_integer())
                config.top_k = json["top_k"].get<int>();
            if (json.contains("top_p") && json["top_p"].is_number())
                config.top_p = json["top_p"].get<float>();
            if (json.contains("stop_strings") && json["stop_strings"].is_array()) {
                for (const auto& v : json["stop_strings"]) {
                    if (v.is_string())
                        config.stop_strings.push_back(v.get<std::string>());
                }
            }
        }

    }  // namespace

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

    GenerationConfig GenerationConfig::from_json(const std::string& path) {
        GenerationConfig config;
        ApplyGenerationConfig(ParseJsonFile(path), config);
        return config;
    }

    GenerationConfig GenerationConfig::from_model_json(const std::string& path) {
        GenerationConfig config;
        nlohmann::json root         = ParseJsonContentOrFile(path);
        const nlohmann::json* g_ptr = nullptr;
        if (root.contains("config") && root["config"].is_object() && root["config"].contains("generation") &&
            root["config"]["generation"].is_object()) {
            g_ptr = &root["config"]["generation"];
        } else if (root.contains("generation") && root["generation"].is_object()) {
            g_ptr = &root["generation"];
        }
        if (!g_ptr)
            return config;
        ApplyGenerationConfig(*g_ptr, config);
        return config;
    }

    Qwen3VLModel::Qwen3VLModel() : sgen_(std::random_device{}()) {}

    Qwen3VLModel::~Qwen3VLModel() {
        deinit();
    }

    void Qwen3VLModel::init_tensors(const bm_net_info_t* net, std::vector<bm_tensor_t>& in_tensors,
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

    static bool ends_with(const std::string& str, const std::string& suffix) {
        if (str.size() < suffix.size())
            return false;
        return std::equal(suffix.rbegin(), suffix.rend(), str.rbegin());
    }

    bool Qwen3VLModel::check_stop(const std::string& text) {
        for (const auto& stop_str : stop_strings) {
            if (ends_with(text, stop_str))
                return true;
        }
        return false;
    }

    void Qwen3VLModel::net_launch(const bm_net_info_t* net, const std::vector<bm_tensor_t>& in_tensors,
                                  std::vector<bm_tensor_t>& out_tensors) {
        (void)bmrt_launch_tensor_ex(p_bmrt_, net->name, in_tensors.data(), net->input_num, out_tensors.data(),
                                    net->output_num, true, false);
    }

    void Qwen3VLModel::net_launch_decode(int idx, int kv_offset, bm_device_mem_t& input_mem,
                                         const int* pos_id, std::vector<uint16_t>& attention_mask) {
        const bm_net_info_t* net = net_blocks_cache_[idx];
        std::vector<bm_tensor_t> in_tensors;
        std::vector<bm_tensor_t> out_tensors;
        init_tensors(net, in_tensors, out_tensors);
        in_tensors[0].device_mem = input_mem;
        if (idx == 0) {
            bm_memcpy_s2d(bm_handle_, in_tensors[1].device_mem, (void*)pos_id);
            bm_memcpy_s2d(bm_handle_, in_tensors[2].device_mem, (void*)attention_mask.data());
        } else {
            in_tensors[1].device_mem = net_blocks_cache_[0]->stages[0].input_mems[1];
            in_tensors[2].device_mem = net_blocks_cache_[0]->stages[0].input_mems[2];
        }
        out_tensors[1].device_mem =
            bm_mem_from_device(past_key_[idx].u.device.device_addr + kv_offset, KV_BYTES);
        out_tensors[2].device_mem =
            bm_mem_from_device(past_value_[idx].u.device.device_addr + kv_offset, KV_BYTES);
        net_launch(net, in_tensors, out_tensors);
    }

    void Qwen3VLModel::d2d(bm_device_mem_t& dst, bm_device_mem_t& src, int offset, int size) {
        if (!size)
            size = bm_mem_get_device_size(src);
        bm_memcpy_d2d_byte(bm_handle_, dst, offset, src, 0, size);
    }

    void Qwen3VLModel::clear_history() {
        if (!support_history)
            return;
        for (int i = 0; i < NUM_LAYERS; i++) {
            empty(bm_handle_, past_key_[i]);
            empty(bm_handle_, past_value_[i]);
        }
        history_length = 0;
    }

    void Qwen3VLModel::init_by_names() {
        auto is_exist = [](const char* name, const char** names, int num) {
            for (int i = 0; i < num; i++)
                if (strcmp(name, names[i]) == 0)
                    return true;
            return false;
        };
        net_embed_             = bmrt_get_network_info(p_bmrt_, "embedding");
        net_embed_cache_       = bmrt_get_network_info(p_bmrt_, "embedding_cache");
        net_vit_               = bmrt_get_network_info(p_bmrt_, "vit");
        net_lm_                = bmrt_get_network_info(p_bmrt_, "lm_head");
        net_add_               = bmrt_get_network_info(p_bmrt_, "add");
        const char** net_names = nullptr;
        int num_nets           = bmrt_get_network_number(p_bmrt_);
        bmrt_get_network_names(p_bmrt_, &net_names);
        net_greedy_head_ = nullptr;
        int num_blocks   = num_nets - 4;
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
        hybrid_fa_cache = NUM_LAYERS >= FA_INTERVAL &&
                          (net_blocks_[0]->input_num != net_blocks_[FA_INTERVAL - 1]->input_num ||
                           net_blocks_cache_[0]->input_num != net_blocks_cache_[FA_INTERVAL - 1]->input_num);
        int kv_layer      = first_kv_layer();
        support_history   = net_blocks_[kv_layer]->input_num == 5;
        is_dynamic        = net_blocks_[0]->is_dynamic;
        vit_dynamic       = net_vit_->is_dynamic;
        history_length    = 0;
        lmhead_with_topk  = net_lm_->stages[0].output_shapes[0].dims[1] == 1;
        MAX_INPUT_LENGTH  = net_embed_->stages[0].input_shapes[0].dims[1];
        HIDDEN_SIZE       = net_lm_->stages[0].input_shapes[0].dims[1];
        SEQLEN            = net_blocks_cache_[kv_layer]->stages[0].input_shapes[3].dims[1];
        MAX_PATCHES       = net_vit_->stages[0].input_shapes[0].dims[0];
        MAX_PIXELS        = MAX_PATCHES * 16 * 16;
        VIT_DIMS          = net_vit_->stages[0].input_shapes[0].dims[1];
        KV_BYTES          = bm_mem_get_device_size(net_blocks_cache_[kv_layer]->stages[0].output_mems[1]);
        PREFILL_KV_LENGTH = 0;
        if (support_history)
            PREFILL_KV_LENGTH = net_blocks_[kv_layer]->stages[0].input_shapes[3].dims[1];
        std::cerr << "[Qwen3VLModel] blocks num_layers=" << NUM_LAYERS
                  << " hybrid_fa_cache=" << (hybrid_fa_cache ? 1 : 0) << " kv_layer=" << kv_layer
                  << " support_history=" << (support_history ? 1 : 0) << " seqlen=" << SEQLEN
                  << " kv_bytes=" << KV_BYTES << std::endl;
    }

    void Qwen3VLModel::init(int dev_id, const std::string& model_path, bool do_sample_,
                            const std::string& model_config_json_path) {
        bm_status_t status = bm_dev_request(&bm_handle_, dev_id);
        assert(BM_SUCCESS == status);
        p_bmrt_ = bmrt_create(bm_handle_);
        assert(p_bmrt_ != nullptr);
        bmrt_set_flags(p_bmrt_, BM_RUNTIME_SHARE_MEM);
        /* Qwen3VL model file: uses .nn extension, content is raw bmodel without extra header, loaded
         * directly as bmodel to save memory and storage
         */
        bool ret = bmrt_load_bmodel(p_bmrt_, model_path.c_str());
        assert(ret);
        bm_thread_sync(bm_handle_);
        init_by_names();
        visited_tokens.resize(SEQLEN);
        past_key_.resize(NUM_LAYERS);
        past_value_.resize(NUM_LAYERS);
        for (int i = 0; i < NUM_LAYERS; i++) {
            if (is_fa_layer(i)) {
                past_key_[i]   = net_blocks_cache_[i]->stages[0].input_mems[3];
                past_value_[i] = net_blocks_cache_[i]->stages[0].input_mems[4];
            } else {
                past_key_[i]   = net_blocks_cache_[i]->stages[0].input_mems[1];
                past_value_[i] = net_blocks_cache_[i]->stages[0].input_mems[2];
            }
            empty(bm_handle_, past_key_[i]);
            empty(bm_handle_, past_value_[i]);
        }
        size_t buffer_size = bm_mem_get_device_size(net_embed_->stages[0].output_mems[0]);
        status             = bm_malloc_device_byte(bm_handle_, &dev_buffer_, buffer_size);
        assert(BM_SUCCESS == status);
        num_deepstack = net_vit_->output_num - 1;
        deepstack_buffers_.clear();
        for (int i = 0; i < num_deepstack; i++) {
            bm_device_mem_t mem;
            status = bm_malloc_device_byte(bm_handle_, &mem, buffer_size);
            assert(BM_SUCCESS == status);
            deepstack_buffers_.push_back(mem);
        }
        vit_run   = false;
        do_sample = do_sample_;
        if (do_sample && net_sample_head_) {
            GenerationConfig gen_config;
            if (!model_config_json_path.empty())
                gen_config = GenerationConfig::from_model_json(model_config_json_path);
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

    void Qwen3VLModel::deinit() {
        if (!p_bmrt_)
            return;
        for (int i = 0; i < num_deepstack; i++)
            bm_free_device(bm_handle_, deepstack_buffers_[i]);
        bm_free_device(bm_handle_, dev_buffer_);
        bmrt_destroy(p_bmrt_);
        bm_dev_free(bm_handle_);
        p_bmrt_    = nullptr;
        bm_handle_ = nullptr;
    }

    int Qwen3VLModel::greedy_search(bm_device_mem_t& logits_mem) {
        std::vector<bm_tensor_t> in_tensors;
        std::vector<bm_tensor_t> out_tensors;
        init_tensors(net_greedy_head_, in_tensors, out_tensors);
        in_tensors[0].device_mem = logits_mem;
        net_launch(net_greedy_head_, in_tensors, out_tensors);
        int token = 0;
        bm_memcpy_d2s(bm_handle_, (void*)&token, out_tensors[0].device_mem);
        return token;
    }

    int Qwen3VLModel::penalty_sample(bm_device_mem_t& logits_mem) {
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

    int Qwen3VLModel::generate(bm_device_mem_t& logits_mem) {
        if (lmhead_with_topk) {
            int token = 0;
            bm_memcpy_d2s_partial(bm_handle_, (void*)&token, logits_mem, sizeof(int));
            return token;
        }
        if (do_sample && net_sample_head_)
            return penalty_sample(logits_mem);
        return greedy_search(logits_mem);
    }

    void Qwen3VLModel::forward_embed(const ArrayInt& tokens) {
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
        for (auto& mem : deepstack_buffers_)
            empty(bm_handle_, mem);
    }

    void Qwen3VLModel::forward_vit(const float* pixel_values, const ArrayInt& position_ids,
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
        if (vit_dynamic) {
            in_tensors[0].shape.dims[0] = hw;
            in_tensors[1].shape.dims[0] = hw;
            in_tensors[2].shape.dims[0] = hw;
            in_tensors[3].shape.dims[0] = hw;
        } else {
            std::vector<float> attention_mask(MAX_PATCHES * MAX_PATCHES, -10000.0f);
            for (int i = 0; i < hw; i++) {
                auto row_begin = attention_mask.begin() + i * MAX_PATCHES;
                std::fill(row_begin, row_begin + hw, 0.0f);
            }
            bm_memcpy_s2d(bm_handle_, in_tensors[4].device_mem, (void*)attention_mask.data());
        }
        net_launch(net_vit_, in_tensors, out_tensors);
        int dst_offset = vit_offset * HIDDEN_SIZE * sizeof(uint16_t);
        int vit_size   = hw / 4 * HIDDEN_SIZE * sizeof(uint16_t);
        bm_memcpy_d2d_byte(bm_handle_, dev_buffer_, dst_offset, out_tensors[0].device_mem, 0, vit_size);
        for (int i = 0; i < num_deepstack; i++) {
            bm_memcpy_d2d_byte(bm_handle_, deepstack_buffers_[i], dst_offset, out_tensors[i + 1].device_mem,
                               0, vit_size);
        }
        vit_run = true;
    }

    int Qwen3VLModel::forward_first(const ArrayInt& position_ids) {
        if (support_history)
            return forward_first_with_kv(position_ids);
        const int* p_ids = position_ids.data();
        std::vector<int> position_ids_pad;
        std::vector<uint16_t> attention_mask;
        if (is_dynamic) {
            attention_mask.assign(token_length * token_length, mask_value);
            for (int i = 0; i < token_length; i++)
                for (int j = 0; j <= i; j++)
                    attention_mask[i * token_length + j] = 0;
            position_ids_pad.assign(3 * token_length, 0);
            std::copy(p_ids, p_ids + token_length * 3, position_ids_pad.begin());
        } else {
            int length = MAX_INPUT_LENGTH;
            attention_mask.assign(length * length, mask_value);
            for (int i = 0; i < token_length; i++)
                for (int j = 0; j <= i; j++)
                    attention_mask[i * length + j] = 0;
            position_ids_pad.assign(3 * length, 0);
            int ori_length = static_cast<int>(position_ids.size()) / 3;
            for (int i = 0; i < 3; i++) {
                int ori_offset = i * ori_length;
                int dst_offset = i * length;
                std::copy(p_ids + ori_offset, p_ids + ori_offset + ori_length,
                          position_ids_pad.begin() + dst_offset);
            }
        }
        bm_device_mem_t out_mem = dev_buffer_;
        empty_net(bm_handle_, net_blocks_[0]);
        std::vector<bm_tensor_t> in_tensors;
        std::vector<bm_tensor_t> out_tensors;
        for (int idx = 0; idx < NUM_LAYERS; idx++) {
            init_tensors(net_blocks_[idx], in_tensors, out_tensors);
            if (is_dynamic) {
                d2d(in_tensors[0].device_mem, out_mem);
                if (is_fa_layer(idx)) {
                    bm_memcpy_s2d_partial(bm_handle_, in_tensors[1].device_mem,
                                          (void*)position_ids_pad.data(), token_length * 3 * sizeof(int));
                    bm_memcpy_s2d_partial(bm_handle_, in_tensors[2].device_mem, (void*)attention_mask.data(),
                                          token_length * token_length * sizeof(uint16_t));
                }
                in_tensors[0].shape.dims[1] = token_length;
                in_tensors[1].shape.dims[1] = token_length;
                in_tensors[2].shape.dims[2] = token_length;
                in_tensors[2].shape.dims[3] = token_length;
            } else {
                in_tensors[0].device_mem = out_mem;
                if (is_fa_layer(idx)) {
                    bm_memcpy_s2d(bm_handle_, in_tensors[1].device_mem, (void*)position_ids_pad.data());
                    bm_memcpy_s2d(bm_handle_, in_tensors[2].device_mem, (void*)attention_mask.data());
                }
            }
            net_launch(net_blocks_[idx], in_tensors, out_tensors);
            out_mem = net_blocks_[idx]->stages[0].output_mems[0];
            if (vit_run && idx < num_deepstack) {
                init_tensors(net_add_, in_tensors, out_tensors);
                in_tensors[0].device_mem = out_mem;
                in_tensors[1].device_mem = deepstack_buffers_[idx];
                net_launch(net_add_, in_tensors, out_tensors);
                out_mem = net_add_->stages[0].output_mems[0];
            }
            if (is_fa_layer(idx)) {
                bm_memcpy_d2d_byte(bm_handle_, past_key_[idx], 0, net_blocks_[idx]->stages[0].output_mems[1],
                                   0, KV_BYTES * token_length);
                bm_memcpy_d2d_byte(bm_handle_, past_value_[idx], 0,
                                   net_blocks_[idx]->stages[0].output_mems[2], 0, KV_BYTES * token_length);
            }
        }
        vit_run   = false;
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

    int Qwen3VLModel::forward_first_with_kv(const ArrayInt& position_ids) {
        int max_kv_length = MAX_INPUT_LENGTH + PREFILL_KV_LENGTH;
        std::vector<uint16_t> attention_mask(MAX_INPUT_LENGTH * max_kv_length, mask_value);
        int old_length = history_length;
        history_length += token_length;
        for (int i = 0; i < token_length; i++) {
            for (int j = 0; j < old_length; j++)
                attention_mask[i * max_kv_length + j] = 0;
            for (int j = 0; j <= i; j++)
                attention_mask[i * max_kv_length + j + PREFILL_KV_LENGTH] = 0;
        }
        const int* p_ids = position_ids.data();
        std::vector<int> position_ids_pad(3 * MAX_INPUT_LENGTH, 0);
        int ori_length = static_cast<int>(position_ids.size()) / 3;
        for (int i = 0; i < 3; i++) {
            int ori_offset = i * ori_length;
            int dst_offset = i * MAX_INPUT_LENGTH;
            std::copy(p_ids + ori_offset, p_ids + ori_offset + ori_length,
                      position_ids_pad.begin() + dst_offset);
        }
        bm_device_mem_t out_mem = dev_buffer_;
        empty_net(bm_handle_, net_blocks_[0]);
        std::vector<bm_tensor_t> in_tensors;
        std::vector<bm_tensor_t> out_tensors;
        for (int idx = 0; idx < NUM_LAYERS; idx++) {
            init_tensors(net_blocks_[idx], in_tensors, out_tensors);
            in_tensors[0].device_mem = out_mem;
            if (old_length > 0) {
                d2d(in_tensors[3].device_mem, past_key_[idx], 0, KV_BYTES * old_length);
                d2d(in_tensors[4].device_mem, past_value_[idx], 0, KV_BYTES * old_length);
            } else if (idx == 0) {
                empty(bm_handle_, in_tensors[3].device_mem);
                empty(bm_handle_, in_tensors[4].device_mem);
            }
            bm_memcpy_s2d(bm_handle_, in_tensors[1].device_mem, (void*)position_ids_pad.data());
            bm_memcpy_s2d(bm_handle_, in_tensors[2].device_mem, (void*)attention_mask.data());
            net_launch(net_blocks_[idx], in_tensors, out_tensors);
            out_mem = net_blocks_[idx]->stages[0].output_mems[0];
            if (vit_run && idx < num_deepstack) {
                init_tensors(net_add_, in_tensors, out_tensors);
                in_tensors[0].device_mem = out_mem;
                in_tensors[1].device_mem = deepstack_buffers_[idx];
                net_launch(net_add_, in_tensors, out_tensors);
                out_mem = net_add_->stages[0].output_mems[0];
            }
            bm_memcpy_d2d_byte(bm_handle_, past_key_[idx], old_length * KV_BYTES,
                               net_blocks_[idx]->stages[0].output_mems[1], 0, KV_BYTES * token_length);
            bm_memcpy_d2d_byte(bm_handle_, past_value_[idx], old_length * KV_BYTES,
                               net_blocks_[idx]->stages[0].output_mems[2], 0, KV_BYTES * token_length);
        }
        vit_run   = false;
        int bytes = HIDDEN_SIZE * sizeof(uint16_t);
        init_tensors(net_lm_, in_tensors, out_tensors);
        in_tensors[0].device_mem =
            bm_mem_from_device(out_mem.u.device.device_addr + (token_length - 1) * bytes, bytes);
        out_tensors[0].device_mem = dev_buffer_;
        net_launch(net_lm_, in_tensors, out_tensors);
        int token                    = generate(dev_buffer_);
        visited_tokens[token_length] = token;
        token_length++;
        history_length++;
        return token;
    }

    int Qwen3VLModel::forward_next(const ArrayInt& position_ids) {
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
        bm_device_mem_t out_mem = out_tensors[0].device_mem;
        int bytes = bm_mem_get_device_size(net_blocks_cache_[first_kv_layer()]->stages[0].output_mems[1]);
        int token_offset = (history_length - 1) * bytes;
        for (size_t idx = 0; idx < net_blocks_cache_.size(); idx++) {
            if (is_fa_layer(static_cast<int>(idx))) {
                net_launch_decode(static_cast<int>(idx), token_offset, out_mem, p_ids, attention_mask);
            } else {
                init_tensors(net_blocks_cache_[idx], in_tensors, out_tensors);
                in_tensors[0].device_mem = out_mem;
                net_launch(net_blocks_cache_[idx], in_tensors, out_tensors);
            }
            out_mem = net_blocks_cache_[idx]->stages[0].output_mems[0];
        }
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

}  // namespace qwen3vl
}  // namespace cosmo::nn
