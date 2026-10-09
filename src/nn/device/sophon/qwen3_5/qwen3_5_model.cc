#include "nn/device/sophon/qwen3_5/qwen3_5_model.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>

#include "bmlib_runtime.h"
#include "bmruntime_interface.h"
#include "nn/device/sophon/qwen_runtime_safety.h"

namespace cosmo::nn {
namespace qwen3_5 {

    namespace safety = qwen_runtime_safety;

    static void empty(bm_handle_t& bm_handle, bm_device_mem_t& mem) {
        safety::ClearDevice(bm_handle, mem, "clear device memory");
    }

    static void empty_net(bm_handle_t& bm_handle, const bm_net_info_t* net, int stage = 0) {
        if (net == nullptr || stage < 0 || stage >= net->stage_num || net->stages == nullptr) {
            throw safety::RuntimeError("clear network memory", "invalid network metadata");
        }
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
        if (net == nullptr || stage < 0 || stage >= net->stage_num || net->stages == nullptr ||
            net->input_num < 0 || net->output_num < 0) {
            throw safety::RuntimeError("initialize tensors", "invalid network metadata");
        }
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
        if (p_bmrt_ == nullptr || net == nullptr || net->name == nullptr ||
            in_tensors.size() != static_cast<size_t>(net->input_num) ||
            out_tensors.size() != static_cast<size_t>(net->output_num)) {
            throw safety::RuntimeError("launch network", "invalid runtime or tensor metadata");
        }
        const auto started = std::chrono::steady_clock::now();
        if (trace_networks_) {
            std::cerr << "[Qwen3_5][network] begin " << net->name << " inputs=";
            for (const auto& tensor : in_tensors) {
                std::cerr << "[";
                for (int dim = 0; dim < tensor.shape.num_dims; ++dim)
                    std::cerr << (dim ? "," : "") << tensor.shape.dims[dim];
                std::cerr << "]";
            }
            std::cerr << std::endl;
        }
        if (!bmrt_launch_tensor_ex(p_bmrt_, net->name, in_tensors.data(), net->input_num, out_tensors.data(),
                                   net->output_num, true, false)) {
            throw safety::RuntimeError("launch network", net->name);
        }
        safety::CheckStatus(bm_thread_sync(bm_handle_), "synchronize network");
        if (trace_networks_) {
            std::cerr << "[Qwen3_5][network] complete " << net->name << " elapsed_ms="
                      << std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started)
                             .count()
                      << std::endl;
        }
    }

    void Qwen3_5Model::net_launch_decode(int idx, int kv_offset, bm_device_mem_t& input_mem,
                                         const int* pos_id, std::vector<uint16_t>& attention_mask) {
        const bm_net_info_t* net = net_blocks_cache_[idx];
        std::vector<bm_tensor_t> in_tensors;
        std::vector<bm_tensor_t> out_tensors;
        init_tensors(net, in_tensors, out_tensors);
        if (in_tensors.size() < 3 || out_tensors.size() < 3 || kv_offset < 0 || KV_BYTES <= 0 ||
            pos_id == nullptr) {
            throw safety::RuntimeError("launch decode network", "invalid tensor layout");
        }
        in_tensors[0].device_mem    = input_mem;
        const size_t position_bytes = bm_mem_get_device_size(in_tensors[1].device_mem);
        const size_t mask_bytes     = bm_mem_get_device_size(in_tensors[2].device_mem);
        if (position_bytes > 3 * sizeof(int) || mask_bytes > attention_mask.size() * sizeof(uint16_t)) {
            throw safety::RuntimeError("launch decode network", "host input is too small");
        }
        safety::CopyHostToDevice(bm_handle_, in_tensors[1].device_mem, pos_id, position_bytes,
                                 "copy decode position");
        safety::CopyHostToDevice(bm_handle_, in_tensors[2].device_mem, attention_mask.data(), mask_bytes,
                                 "copy decode mask");
        out_tensors[1].device_mem = safety::DeviceView(past_key_[idx], static_cast<size_t>(kv_offset),
                                                       static_cast<size_t>(KV_BYTES), "map key cache output");
        out_tensors[2].device_mem =
            safety::DeviceView(past_value_[idx], static_cast<size_t>(kv_offset),
                               static_cast<size_t>(KV_BYTES), "map value cache output");
        net_launch(net, in_tensors, out_tensors);
    }

    void Qwen3_5Model::d2d(bm_device_mem_t& dst, bm_device_mem_t& src, int offset, int size) {
        if (offset < 0 || size < 0) {
            throw safety::RuntimeError("copy device memory", "negative range");
        }
        const size_t copy_size = size == 0 ? bm_mem_get_device_size(src) : static_cast<size_t>(size);
        safety::CopyDeviceToDevice(bm_handle_, dst, static_cast<size_t>(offset), src, 0, copy_size,
                                   "copy device memory");
    }

    void Qwen3_5Model::clear_history(bool force) {
        if (!support_history && !force)
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
            if (name == nullptr || names == nullptr || num <= 0) {
                return false;
            }
            for (int i = 0; i < num; i++)
                if (names[i] != nullptr && strcmp(name, names[i]) == 0)
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
        const auto free_names = [](const char** names) { std::free(names); };
        std::unique_ptr<const char*, decltype(free_names)> names_guard(net_names, free_names);
        if (num_nets <= 0 || net_names == nullptr) {
            throw safety::RuntimeError("inspect model", "network list is empty");
        }
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
        if (net_embed_ == nullptr || net_embed_cache_ == nullptr || net_vit_ == nullptr ||
            net_lm_ == nullptr || NUM_LAYERS < FA_INTERVAL ||
            net_blocks_.size() != static_cast<size_t>(NUM_LAYERS) ||
            net_blocks_cache_.size() != static_cast<size_t>(NUM_LAYERS)) {
            throw safety::RuntimeError("inspect model", "required networks are missing");
        }
        const auto valid_network = [](const bm_net_info_t* net, int min_inputs, int min_outputs) {
            return net != nullptr && net->stage_num > 0 && net->stages != nullptr &&
                   net->input_num >= min_inputs && net->output_num >= min_outputs &&
                   net->stages[0].input_shapes != nullptr && net->stages[0].output_shapes != nullptr &&
                   net->stages[0].input_mems != nullptr && net->stages[0].output_mems != nullptr &&
                   net->input_dtypes != nullptr && net->output_dtypes != nullptr;
        };
        if (!valid_network(net_embed_, 1, 1) || !valid_network(net_embed_cache_, 1, 1) ||
            !valid_network(net_vit_, 4, 1) || !valid_network(net_lm_, 1, 1) ||
            net_embed_cache_->output_dtypes == nullptr) {
            throw safety::RuntimeError("inspect model", "invalid base network metadata");
        }
        for (int i = 0; i < NUM_LAYERS; ++i) {
            const bool full_attention = is_FA(i);
            if (!valid_network(net_blocks_[i], 2, full_attention ? 3 : 2) ||
                !valid_network(net_blocks_cache_[i], full_attention ? 5 : 3, full_attention ? 3 : 1)) {
                throw safety::RuntimeError("inspect model", "invalid block network metadata");
            }
        }
        if (net_embed_cache_->output_dtypes[0] == BM_FLOAT16)
            mask_value = 0xF0E2;
        else if (net_embed_cache_->output_dtypes[0] == BM_BFLOAT16)
            mask_value = 0xC61C;
        else
            throw std::runtime_error("Invalid attention dtype");
        // Qwen3.5: determine support_history and read SEQLEN from FA layer
        int fa_idx      = FA_INTERVAL - 1;
        support_history = net_blocks_[fa_idx]->input_num == 5;
        history_length  = 0;
        if (net_lm_->stages[0].output_shapes[0].num_dims < 2 ||
            net_embed_->stages[0].input_shapes[0].num_dims < 2 ||
            net_lm_->stages[0].input_shapes[0].num_dims < 2 ||
            net_blocks_cache_[fa_idx]->stages[0].input_shapes[3].num_dims < 2 ||
            net_vit_->stages[0].input_shapes[0].num_dims < 2) {
            throw safety::RuntimeError("inspect model", "required tensor dimensions are missing");
        }
        lmhead_with_topk = net_lm_->stages[0].output_shapes[0].dims[1] == 1;
        MAX_INPUT_LENGTH = net_embed_->stages[0].input_shapes[0].dims[1];
        HIDDEN_SIZE      = net_lm_->stages[0].input_shapes[0].dims[1];
        SEQLEN           = net_blocks_cache_[fa_idx]->stages[0].input_shapes[3].dims[1];
        MAX_PATCHES      = net_vit_->stages[0].input_shapes[0].dims[0];
        const size_t max_pixels =
            safety::CheckedMultiply(static_cast<size_t>(MAX_PATCHES), 16U * 16U, "calculate maximum pixels");
        if (max_pixels > static_cast<size_t>(std::numeric_limits<int>::max())) {
            throw safety::RuntimeError("inspect model", "maximum pixel count is too large");
        }
        MAX_PIXELS        = static_cast<int>(max_pixels);
        VIT_DIMS          = net_vit_->stages[0].input_shapes[0].dims[1];
        KV_BYTES          = bm_mem_get_device_size(net_blocks_cache_[fa_idx]->stages[0].output_mems[1]);
        PREFILL_KV_LENGTH = 0;
        if (support_history)
            PREFILL_KV_LENGTH = net_blocks_[fa_idx]->stages[0].input_shapes[3].dims[1];
        if (SEQLEN <= 1 || MAX_INPUT_LENGTH <= 0 || MAX_INPUT_LENGTH > SEQLEN || HIDDEN_SIZE <= 0 ||
            MAX_PATCHES <= 0 || VIT_DIMS <= 0 || KV_BYTES <= 0 || PREFILL_KV_LENGTH < 0) {
            throw safety::RuntimeError("inspect model", "invalid model limits");
        }
    }

    void Qwen3_5Model::init(int dev_id, const std::string& model_path, bool do_sample_,
                            const std::string& model_config_json_path) {
        if (bm_handle_ != nullptr || p_bmrt_ != nullptr || model_path.empty()) {
            throw safety::RuntimeError("initialize model", "already initialized or empty model path");
        }

        const RawBmodelLoadPlan load_plan = safety::AuthorizeRawBmodel(model_path);

        try {
            safety::CheckStatus(bm_dev_request(&bm_handle_, dev_id), "request device");
            p_bmrt_ = safety::LoadSingleAuthorizedRawBmodel(load_plan, bm_handle_);
            safety::CheckStatus(bm_thread_sync(bm_handle_), "synchronize model load");
            init_by_names();
            const size_t token_capacity = static_cast<size_t>(std::max(SEQLEN, MAX_INPUT_LENGTH)) + 1U;
            visited_tokens.assign(token_capacity, 0);
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
            const size_t buffer_size = bm_mem_get_device_size(net_embed_->stages[0].output_mems[0]);
            if (buffer_size == 0) {
                throw safety::RuntimeError("allocate model buffer", "zero-sized embedding output");
            }
            safety::CheckStatus(
                bm_malloc_device_byte(bm_handle_, &dev_buffer_,
                                      safety::CheckedTransferSize(buffer_size, "allocate model buffer")),
                "allocate model buffer");
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
                if (net_sample_head_->stage_num <= 0 || net_sample_head_->stages == nullptr ||
                    net_sample_head_->input_num < 6 || net_sample_head_->output_num < 2 || top_k <= 0 ||
                    static_cast<size_t>(top_k) >
                        bm_mem_get_device_size(net_sample_head_->stages[0].output_mems[0]) / sizeof(float) ||
                    static_cast<size_t>(top_k) >
                        bm_mem_get_device_size(net_sample_head_->stages[0].output_mems[1]) / sizeof(int)) {
                    throw safety::RuntimeError("configure sampling", "invalid sampling network or top_k");
                }
                safety::CopyHostToDevice(bm_handle_, net_sample_head_->stages[0].input_mems[2], &penalty,
                                         sizeof(penalty), "copy repetition penalty");
                safety::CopyHostToDevice(bm_handle_, net_sample_head_->stages[0].input_mems[3], &temperature,
                                         sizeof(temperature), "copy temperature");
                safety::CopyHostToDevice(bm_handle_, net_sample_head_->stages[0].input_mems[4], &top_k,
                                         sizeof(top_k), "copy top_k");
                safety::CopyHostToDevice(bm_handle_, net_sample_head_->stages[0].input_mems[5], &top_p,
                                         sizeof(top_p), "copy top_p");
            }
        } catch (...) {
            deinit();
            throw;
        }
    }

    void Qwen3_5Model::deinit() {
        safety::FreeDeviceNoThrow(bm_handle_, dev_buffer_);
        dev_buffer_ = {};
        safety::DestroyRuntimeNoThrow(&p_bmrt_);
        safety::FreeHandleNoThrow(&bm_handle_);
        net_blocks_.clear();
        net_blocks_cache_.clear();
        past_key_.clear();
        past_value_.clear();
        visited_tokens.clear();
        net_embed_       = nullptr;
        net_embed_cache_ = nullptr;
        net_lm_          = nullptr;
        net_vit_         = nullptr;
        net_greedy_head_ = nullptr;
        net_sample_head_ = nullptr;
        token_length     = 0;
        history_length   = 0;
    }

    int Qwen3_5Model::greedy_search(bm_device_mem_t& logits_mem) {
        if (net_greedy_head_ == nullptr) {
            throw safety::RuntimeError("greedy search", "greedy head is missing");
        }
        std::vector<bm_tensor_t> in_tensors;
        std::vector<bm_tensor_t> out_tensors;
        init_tensors(net_greedy_head_, in_tensors, out_tensors);
        in_tensors[0].device_mem = logits_mem;
        net_launch(net_greedy_head_, in_tensors, out_tensors);
        int token = 0;
        safety::CopyDeviceToHost(bm_handle_, &token, out_tensors[0].device_mem, sizeof(token),
                                 "copy greedy token");
        return token;
    }

    int Qwen3_5Model::penalty_sample(bm_device_mem_t& logits_mem) {
        std::vector<bm_tensor_t> in_tensors;
        std::vector<bm_tensor_t> out_tensors;
        init_tensors(net_sample_head_, in_tensors, out_tensors);
        in_tensors[0].device_mem = logits_mem;
        if (token_length <= 0 || static_cast<size_t>(token_length) > visited_tokens.size() || top_k <= 0) {
            throw safety::RuntimeError("sample token", "invalid token count or top_k");
        }
        safety::CopyHostToDevice(bm_handle_, in_tensors[1].device_mem, visited_tokens.data(),
                                 static_cast<size_t>(token_length) * sizeof(int), "copy sampling history");
        in_tensors[1].shape.dims[1] = token_length;
        net_launch(net_sample_head_, in_tensors, out_tensors);
        std::vector<float> probs(top_k);
        std::vector<int> tokens(top_k);
        safety::CopyDeviceToHost(bm_handle_, probs.data(), out_tensors[0].device_mem,
                                 static_cast<size_t>(top_k) * sizeof(float), "copy sampling probabilities");
        safety::CopyDeviceToHost(bm_handle_, tokens.data(), out_tensors[1].device_mem,
                                 static_cast<size_t>(top_k) * sizeof(int), "copy sampling tokens");
        std::discrete_distribution<> dist(probs.begin(), probs.end());
        return tokens[dist(sgen_)];
    }

    int Qwen3_5Model::generate(bm_device_mem_t& logits_mem) {
        if (lmhead_with_topk) {
            int token = 0;
            safety::CopyDeviceToHost(bm_handle_, &token, logits_mem, sizeof(token), "copy top-k token");
            return token;
        }
        if (do_sample && net_sample_head_)
            return penalty_sample(logits_mem);
        return greedy_search(logits_mem);
    }

    void Qwen3_5Model::forward_embed(const ArrayInt& tokens) {
        if (p_bmrt_ == nullptr || tokens.empty() || tokens.size() > static_cast<size_t>(MAX_INPUT_LENGTH) ||
            tokens.size() >= static_cast<size_t>(SEQLEN) || tokens.size() > visited_tokens.size()) {
            throw safety::RuntimeError("embed tokens", "invalid token count");
        }
        std::fill(visited_tokens.begin(), visited_tokens.end(), 0);
        std::copy(tokens.begin(), tokens.end(), visited_tokens.data());
        std::vector<bm_tensor_t> in_tensors;
        std::vector<bm_tensor_t> out_tensors;
        init_tensors(net_embed_, in_tensors, out_tensors);
        const size_t input_bytes = safety::CheckedMultiply(static_cast<size_t>(MAX_INPUT_LENGTH), sizeof(int),
                                                           "calculate embedding input size");
        safety::CopyHostToDevice(bm_handle_, in_tensors[0].device_mem, visited_tokens.data(), input_bytes,
                                 "copy embedding input");
        net_launch(net_embed_, in_tensors, out_tensors);
        empty(bm_handle_, dev_buffer_);
        size_t embedding_bytes = safety::CheckedMultiply(tokens.size(), static_cast<size_t>(HIDDEN_SIZE),
                                                         "calculate embedding output size");
        embedding_bytes =
            safety::CheckedMultiply(embedding_bytes, sizeof(uint16_t), "calculate embedding output size");
        safety::CopyDeviceToDevice(bm_handle_, dev_buffer_, 0, out_tensors[0].device_mem, 0, embedding_bytes,
                                   "copy embedding output");
        token_length = static_cast<int>(tokens.size());
    }

    // ========== ViT: similar to Qwen3VLModel but dynamic mode only, no deepstack ==========
    void Qwen3_5Model::forward_vit(const float* pixel_values, const ArrayInt& position_ids,
                                   const ArrayInt& pos_idx, const ArrayFloat& pos_weight,
                                   const ArrayInt& grid_thw, int vit_offset) {
        if (pixel_values == nullptr || grid_thw.size() != 3 || grid_thw[0] <= 0 || grid_thw[1] <= 0 ||
            grid_thw[2] <= 0 || vit_offset < 0) {
            throw safety::RuntimeError("run vision encoder", "invalid image metadata");
        }
        size_t hw = safety::CheckedMultiply(static_cast<size_t>(grid_thw[0]),
                                            static_cast<size_t>(grid_thw[1]), "calculate vision patch count");
        hw = safety::CheckedMultiply(hw, static_cast<size_t>(grid_thw[2]), "calculate vision patch count");
        if (hw > static_cast<size_t>(MAX_PATCHES) ||
            hw > static_cast<size_t>(std::numeric_limits<int>::max()) || hw % 4 != 0 ||
            position_ids.size() != hw * 2U || pos_idx.size() != hw * 4U || pos_weight.size() != hw * 4U) {
            throw safety::RuntimeError("run vision encoder", "vision inputs do not match patch grid");
        }
        const size_t num_pixels =
            safety::CheckedMultiply(hw, static_cast<size_t>(VIT_DIMS), "calculate vision input size");
        empty_net(bm_handle_, net_vit_);
        std::vector<bm_tensor_t> in_tensors;
        std::vector<bm_tensor_t> out_tensors;
        init_tensors(net_vit_, in_tensors, out_tensors);
        safety::CopyHostToDevice(
            bm_handle_, in_tensors[0].device_mem, pixel_values,
            safety::CheckedMultiply(num_pixels, sizeof(float), "calculate vision input bytes"),
            "copy vision pixels");
        safety::CopyHostToDevice(bm_handle_, in_tensors[1].device_mem, position_ids.data(),
                                 position_ids.size() * sizeof(int), "copy vision positions");
        safety::CopyHostToDevice(bm_handle_, in_tensors[2].device_mem, pos_idx.data(),
                                 pos_idx.size() * sizeof(int), "copy vision interpolation indices");
        safety::CopyHostToDevice(bm_handle_, in_tensors[3].device_mem, pos_weight.data(),
                                 pos_weight.size() * sizeof(float), "copy vision interpolation weights");
        // Qwen3.5: dynamic ViT only
        in_tensors[0].shape.dims[0] = static_cast<int>(hw);
        in_tensors[1].shape.dims[0] = static_cast<int>(hw);
        in_tensors[2].shape.dims[0] = static_cast<int>(hw);
        in_tensors[3].shape.dims[0] = static_cast<int>(hw);
        net_launch(net_vit_, in_tensors, out_tensors);
        size_t dst_offset =
            safety::CheckedMultiply(static_cast<size_t>(vit_offset), static_cast<size_t>(HIDDEN_SIZE),
                                    "calculate vision output offset");
        dst_offset = safety::CheckedMultiply(dst_offset, sizeof(uint16_t), "calculate vision output offset");
        size_t vit_size = safety::CheckedMultiply(hw / 4U, static_cast<size_t>(HIDDEN_SIZE),
                                                  "calculate vision output size");
        vit_size        = safety::CheckedMultiply(vit_size, sizeof(uint16_t), "calculate vision output size");
        safety::CopyDeviceToDevice(bm_handle_, dev_buffer_, dst_offset, out_tensors[0].device_mem, 0,
                                   vit_size, "copy vision output");
        // No deepstack buffer to process
    }

    // ========== forward_first: core difference -- branch by is_FA ==========
    int Qwen3_5Model::forward_first(const ArrayInt& position_ids) {
        if (token_length <= 0 || token_length >= SEQLEN ||
            position_ids.size() != static_cast<size_t>(token_length) * 3U ||
            static_cast<size_t>(token_length) >= visited_tokens.size()) {
            throw safety::RuntimeError("prefill model", "invalid token or position count");
        }
        std::vector<int> position_ids_pad;
        const size_t attention_elements =
            safety::CheckedMultiply(static_cast<size_t>(token_length), static_cast<size_t>(token_length),
                                    "calculate prefill attention mask");
        std::vector<uint16_t> attention_mask(attention_elements, mask_value);
        for (int i = 0; i < token_length; i++)
            for (int j = 0; j <= i; j++)
                attention_mask[static_cast<size_t>(i) * static_cast<size_t>(token_length) +
                               static_cast<size_t>(j)] = 0;
        position_ids_pad.assign(static_cast<size_t>(token_length) * 3U, 0);
        std::copy(position_ids.begin(), position_ids.end(), position_ids_pad.begin());

        auto out_mem = dev_buffer_;
        empty_net(bm_handle_, net_blocks_[0]);
        std::vector<bm_tensor_t> in_tensors;
        std::vector<bm_tensor_t> out_tensors;
        size_t layer_bytes =
            safety::CheckedMultiply(static_cast<size_t>(token_length), static_cast<size_t>(HIDDEN_SIZE),
                                    "calculate block input size");
        layer_bytes = safety::CheckedMultiply(layer_bytes, sizeof(uint16_t), "calculate block input size");
        for (int idx = 0; idx < NUM_LAYERS; idx++) {
            init_tensors(net_blocks_[idx], in_tensors, out_tensors);
            safety::CopyDeviceToDevice(bm_handle_, in_tensors[0].device_mem, 0, out_mem, 0, layer_bytes,
                                       "copy block input");
            if (is_FA(idx)) {
                // Full Attention layer: position IDs and an optional explicit causal mask
                safety::CopyHostToDevice(bm_handle_, in_tensors[1].device_mem, position_ids_pad.data(),
                                         position_ids_pad.size() * sizeof(int), "copy prefill positions");
                in_tensors[0].shape.dims[1] = token_length;
                in_tensors[1].shape.dims[1] = token_length;
                // New exports infer the causal mask internally; old exports accept it.
                if (in_tensors.size() >= 3) {
                    safety::CopyHostToDevice(bm_handle_, in_tensors[2].device_mem, attention_mask.data(),
                                             attention_mask.size() * sizeof(uint16_t),
                                             "copy prefill attention mask");
                    in_tensors[2].shape.dims[2] = token_length;
                    in_tensors[2].shape.dims[3] = token_length;
                }
            } else {
                // Linear Attention layer: only needs input + clear recurrent state
                in_tensors[0].shape.dims[1] = token_length;
                empty(bm_handle_, in_tensors[1].device_mem);
            }
            // Use the separately allocated buffer: adjacent dynamic networks may
            // reuse overlapping runtime memory ranges for their input and output.
            out_tensors[0].device_mem = dev_buffer_;
            net_launch(net_blocks_[idx], in_tensors, out_tensors);
            out_mem = out_tensors[0].device_mem;
            // KV cache writeback: branch by layer type
            if (is_FA(idx)) {
                const size_t kv_size =
                    safety::CheckedMultiply(static_cast<size_t>(KV_BYTES), static_cast<size_t>(token_length),
                                            "calculate prefill cache size");
                safety::CopyDeviceToDevice(bm_handle_, past_key_[idx], 0,
                                           net_blocks_[idx]->stages[0].output_mems[1], 0, kv_size,
                                           "copy prefill key cache");
                safety::CopyDeviceToDevice(bm_handle_, past_value_[idx], 0,
                                           net_blocks_[idx]->stages[0].output_mems[2], 0, kv_size,
                                           "copy prefill value cache");
            } else {
                // Linear Attention: conv state + recurrent state
                d2d(past_key_[idx], net_blocks_[idx]->stages[0].output_mems[1]);
                // New graphs return recurrent state explicitly; older two-output graphs
                // update input 1 in place. Preserve both artifact contracts.
                const auto* block = net_blocks_[idx];
                if (block->output_num >= 3) {
                    const auto& expected = block->stages[0].input_shapes[1];
                    const auto& actual   = out_tensors[2].shape;
                    if (actual.num_dims != expected.num_dims ||
                        !std::equal(expected.dims, expected.dims + expected.num_dims, actual.dims) ||
                        out_tensors[2].dtype != block->input_dtypes[1]) {
                        throw safety::RuntimeError("copy recurrent state", "unexpected output 2 layout");
                    }
                    d2d(past_value_[idx], out_tensors[2].device_mem);
                } else {
                    d2d(past_value_[idx], block->stages[0].input_mems[1]);
                }
            }
        }
        // forward lm_head
        const size_t bytes     = safety::CheckedMultiply(static_cast<size_t>(HIDDEN_SIZE), sizeof(uint16_t),
                                                         "calculate language model input size");
        const size_t lm_offset = safety::CheckedMultiply(static_cast<size_t>(token_length - 1), bytes,
                                                         "calculate language model input offset");
        init_tensors(net_lm_, in_tensors, out_tensors);
        in_tensors[0].device_mem  = safety::DeviceView(out_mem, lm_offset, bytes, "map language model input");
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
        if (token_length <= 0 || token_length >= SEQLEN || history_length <= 0 || history_length > SEQLEN ||
            position_ids.size() < 3 || static_cast<size_t>(token_length) >= visited_tokens.size()) {
            throw safety::RuntimeError("decode token", "invalid generation state");
        }
        std::vector<uint16_t> attention_mask(static_cast<size_t>(SEQLEN) + 1U, 0);
        for (int i = history_length - 1; i < SEQLEN; i++)
            attention_mask[i] = mask_value;
        const int* p_ids = position_ids.data();
        std::vector<bm_tensor_t> in_tensors;
        std::vector<bm_tensor_t> out_tensors;
        init_tensors(net_embed_cache_, in_tensors, out_tensors);
        int token = visited_tokens[token_length - 1];
        safety::CopyHostToDevice(bm_handle_, in_tensors[0].device_mem, &token, sizeof(token),
                                 "copy decode token");
        net_launch(net_embed_cache_, in_tensors, out_tensors);
        auto out_mem = out_tensors[0].device_mem;
        // KV_BYTES for FA layer
        const size_t fa_bytes =
            bm_mem_get_device_size(net_blocks_cache_[FA_INTERVAL - 1]->stages[0].output_mems[1]);
        const size_t token_offset = safety::CheckedMultiply(static_cast<size_t>(history_length - 1), fa_bytes,
                                                            "calculate decode cache offset");
        if (fa_bytes == 0 || token_offset > static_cast<size_t>(std::numeric_limits<int>::max())) {
            throw safety::RuntimeError("decode token", "invalid cache geometry");
        }
        for (int idx = 0; idx < NUM_LAYERS; idx++) {
            if (is_FA(idx)) {
                // Full Attention layer: use net_launch_decode (with KV cache)
                net_launch_decode(idx, static_cast<int>(token_offset), out_mem, p_ids, attention_mask);
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
