#include "nn/device/sophon/qwen3vl/qwen3vl_model.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <nlohmann/json.hpp>
#include <stdexcept>

#include "bmlib_runtime.h"
#include "bmruntime_interface.h"
#include "nn/device/sophon/qwen_runtime_safety.h"

namespace cosmo::nn {
namespace qwen3vl {

    namespace safety = qwen_runtime_safety;

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
        if (p_bmrt_ == nullptr || net == nullptr || net->name == nullptr ||
            in_tensors.size() != static_cast<size_t>(net->input_num) ||
            out_tensors.size() != static_cast<size_t>(net->output_num)) {
            throw safety::RuntimeError("launch network", "invalid runtime or tensor metadata");
        }
        if (!bmrt_launch_tensor_ex(p_bmrt_, net->name, in_tensors.data(), net->input_num, out_tensors.data(),
                                   net->output_num, true, false)) {
            throw safety::RuntimeError("launch network", net->name);
        }
        safety::CheckStatus(bm_thread_sync(bm_handle_), "synchronize network");
    }

    void Qwen3VLModel::net_launch_decode(int idx, int kv_offset, bm_device_mem_t& input_mem,
                                         const int* pos_id, std::vector<uint16_t>& attention_mask) {
        const bm_net_info_t* net = net_blocks_cache_[idx];
        std::vector<bm_tensor_t> in_tensors;
        std::vector<bm_tensor_t> out_tensors;
        init_tensors(net, in_tensors, out_tensors);
        if (in_tensors.size() < 3 || out_tensors.size() < 3 || kv_offset < 0 || KV_BYTES <= 0 ||
            pos_id == nullptr) {
            throw safety::RuntimeError("launch decode network", "invalid tensor layout");
        }
        in_tensors[0].device_mem     = input_mem;
        const int shared_input_layer = first_kv_layer();
        if (idx == shared_input_layer) {
            const size_t position_bytes = bm_mem_get_device_size(in_tensors[1].device_mem);
            const size_t mask_bytes     = bm_mem_get_device_size(in_tensors[2].device_mem);
            if (position_bytes > 3 * sizeof(int) || mask_bytes > attention_mask.size() * sizeof(uint16_t)) {
                throw safety::RuntimeError("launch decode network", "host input is too small");
            }
            safety::CopyHostToDevice(bm_handle_, in_tensors[1].device_mem, pos_id, position_bytes,
                                     "copy decode position");
            safety::CopyHostToDevice(bm_handle_, in_tensors[2].device_mem, attention_mask.data(), mask_bytes,
                                     "copy decode mask");
        } else {
            in_tensors[1].device_mem = net_blocks_cache_[shared_input_layer]->stages[0].input_mems[1];
            in_tensors[2].device_mem = net_blocks_cache_[shared_input_layer]->stages[0].input_mems[2];
        }
        out_tensors[1].device_mem = safety::DeviceView(past_key_[idx], static_cast<size_t>(kv_offset),
                                                       static_cast<size_t>(KV_BYTES), "map key cache output");
        out_tensors[2].device_mem =
            safety::DeviceView(past_value_[idx], static_cast<size_t>(kv_offset),
                               static_cast<size_t>(KV_BYTES), "map value cache output");
        net_launch(net, in_tensors, out_tensors);
    }

    void Qwen3VLModel::d2d(bm_device_mem_t& dst, bm_device_mem_t& src, int offset, int size) {
        if (offset < 0 || size < 0) {
            throw safety::RuntimeError("copy device memory", "negative range");
        }
        const size_t copy_size = size == 0 ? bm_mem_get_device_size(src) : static_cast<size_t>(size);
        safety::CopyDeviceToDevice(bm_handle_, dst, static_cast<size_t>(offset), src, 0, copy_size,
                                   "copy device memory");
    }

    void Qwen3VLModel::clear_history(bool force) {
        if (!support_history && !force)
            return;
        for (int i = 0; i < NUM_LAYERS; i++) {
            empty(bm_handle_, past_key_[i]);
            empty(bm_handle_, past_value_[i]);
        }
        history_length = 0;
    }

    void Qwen3VLModel::init_by_names() {
        auto is_exist = [](const char* name, const char** names, int num) {
            if (name == nullptr || names == nullptr || num <= 0) {
                return false;
            }
            for (int i = 0; i < num; i++)
                if (names[i] != nullptr && strcmp(name, names[i]) == 0)
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
        const auto free_names = [](const char** names) { std::free(names); };
        std::unique_ptr<const char*, decltype(free_names)> names_guard(net_names, free_names);
        if (num_nets <= 0 || net_names == nullptr) {
            throw safety::RuntimeError("inspect model", "network list is empty");
        }
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
        if (net_embed_ == nullptr || net_embed_cache_ == nullptr || net_vit_ == nullptr ||
            net_lm_ == nullptr || NUM_LAYERS <= 0 || net_blocks_.size() != static_cast<size_t>(NUM_LAYERS) ||
            net_blocks_cache_.size() != static_cast<size_t>(NUM_LAYERS)) {
            throw safety::RuntimeError("inspect model", "required networks are missing");
        }
        const auto valid_network = [](const bm_net_info_t* net, int min_inputs, int min_outputs) {
            return net != nullptr && net->stage_num > 0 && net->stages != nullptr &&
                   net->input_num >= min_inputs && net->output_num >= min_outputs &&
                   net->stages[0].input_shapes != nullptr && net->stages[0].output_shapes != nullptr &&
                   net->stages[0].input_mems != nullptr && net->stages[0].output_mems != nullptr;
        };
        if (!valid_network(net_embed_, 1, 1) || !valid_network(net_embed_cache_, 1, 1) ||
            !valid_network(net_vit_, 4, 1) || !valid_network(net_lm_, 1, 1) ||
            net_embed_cache_->output_dtypes == nullptr) {
            throw safety::RuntimeError("inspect model", "invalid base network metadata");
        }
        if (net_embed_cache_->output_dtypes[0] == BM_FLOAT16)
            mask_value = 0xF0E2;
        else if (net_embed_cache_->output_dtypes[0] == BM_BFLOAT16)
            mask_value = 0xC61C;
        else
            throw std::runtime_error("Invalid attention dtype");
        hybrid_fa_cache = NUM_LAYERS >= FA_INTERVAL &&
                          (net_blocks_[0]->input_num != net_blocks_[FA_INTERVAL - 1]->input_num ||
                           net_blocks_cache_[0]->input_num != net_blocks_cache_[FA_INTERVAL - 1]->input_num);
        int kv_layer    = first_kv_layer();
        support_history = net_blocks_[kv_layer]->input_num == 5;
        for (int i = 0; i < NUM_LAYERS; ++i) {
            const bool full_attention = is_fa_layer(i);
            const int block_inputs    = support_history ? 5 : 3;
            const int block_outputs   = (support_history || full_attention) ? 3 : 1;
            if (!valid_network(net_blocks_[i], block_inputs, block_outputs) ||
                !valid_network(net_blocks_cache_[i], full_attention ? 5 : 3, full_attention ? 3 : 1)) {
                throw safety::RuntimeError("inspect model", "invalid block network metadata");
            }
        }
        is_dynamic     = net_blocks_[0]->is_dynamic;
        vit_dynamic    = net_vit_->is_dynamic;
        history_length = 0;
        if ((!vit_dynamic && net_vit_->input_num < 5) || net_lm_->stages[0].output_shapes[0].num_dims < 2 ||
            net_embed_->stages[0].input_shapes[0].num_dims < 2 ||
            net_lm_->stages[0].input_shapes[0].num_dims < 2 ||
            net_blocks_cache_[kv_layer]->stages[0].input_shapes[3].num_dims < 2 ||
            net_vit_->stages[0].input_shapes[0].num_dims < 2) {
            throw safety::RuntimeError("inspect model", "required tensor dimensions are missing");
        }
        lmhead_with_topk = net_lm_->stages[0].output_shapes[0].dims[1] == 1;
        MAX_INPUT_LENGTH = net_embed_->stages[0].input_shapes[0].dims[1];
        HIDDEN_SIZE      = net_lm_->stages[0].input_shapes[0].dims[1];
        SEQLEN           = net_blocks_cache_[kv_layer]->stages[0].input_shapes[3].dims[1];
        MAX_PATCHES      = net_vit_->stages[0].input_shapes[0].dims[0];
        const size_t max_pixels =
            safety::CheckedMultiply(static_cast<size_t>(MAX_PATCHES), 16U * 16U, "calculate maximum pixels");
        if (max_pixels > static_cast<size_t>(std::numeric_limits<int>::max())) {
            throw safety::RuntimeError("inspect model", "maximum pixel count is too large");
        }
        MAX_PIXELS        = static_cast<int>(max_pixels);
        VIT_DIMS          = net_vit_->stages[0].input_shapes[0].dims[1];
        KV_BYTES          = bm_mem_get_device_size(net_blocks_cache_[kv_layer]->stages[0].output_mems[1]);
        PREFILL_KV_LENGTH = 0;
        if (support_history)
            PREFILL_KV_LENGTH = net_blocks_[kv_layer]->stages[0].input_shapes[3].dims[1];
        if (SEQLEN <= 1 || MAX_INPUT_LENGTH <= 0 || MAX_INPUT_LENGTH > SEQLEN || HIDDEN_SIZE <= 0 ||
            MAX_PATCHES <= 0 || VIT_DIMS <= 0 || KV_BYTES <= 0 || PREFILL_KV_LENGTH < 0) {
            throw safety::RuntimeError("inspect model", "invalid model limits");
        }
        std::cerr << "[Qwen3VLModel] blocks num_layers=" << NUM_LAYERS
                  << " hybrid_fa_cache=" << (hybrid_fa_cache ? 1 : 0) << " kv_layer=" << kv_layer
                  << " support_history=" << (support_history ? 1 : 0) << " seqlen=" << SEQLEN
                  << " kv_bytes=" << KV_BYTES << std::endl;
    }

    void Qwen3VLModel::init(int dev_id, const std::string& model_path, bool do_sample_,
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
            const size_t buffer_size = bm_mem_get_device_size(net_embed_->stages[0].output_mems[0]);
            if (buffer_size == 0) {
                throw safety::RuntimeError("allocate model buffer", "zero-sized embedding output");
            }
            const auto allocation_size = safety::CheckedTransferSize(buffer_size, "allocate model buffer");
            safety::CheckStatus(bm_malloc_device_byte(bm_handle_, &dev_buffer_, allocation_size),
                                "allocate model buffer");
            num_deepstack = net_vit_->output_num - 1;
            if (num_deepstack > 0 &&
                (net_add_ == nullptr || net_add_->stage_num <= 0 || net_add_->stages == nullptr ||
                 net_add_->input_num < 2 || net_add_->output_num < 1)) {
                throw safety::RuntimeError("inspect model", "deepstack add network is missing");
            }
            deepstack_buffers_.clear();
            deepstack_buffers_.reserve(static_cast<size_t>(num_deepstack));
            for (int i = 0; i < num_deepstack; i++) {
                bm_device_mem_t mem{};
                safety::CheckStatus(bm_malloc_device_byte(bm_handle_, &mem, allocation_size),
                                    "allocate deepstack buffer");
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

    void Qwen3VLModel::deinit() {
        if (bm_handle_ != nullptr) {
            for (auto& memory : deepstack_buffers_) {
                safety::FreeDeviceNoThrow(bm_handle_, memory);
            }
            safety::FreeDeviceNoThrow(bm_handle_, dev_buffer_);
        }
        deepstack_buffers_.clear();
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
        net_add_         = nullptr;
        net_greedy_head_ = nullptr;
        net_sample_head_ = nullptr;
        token_length     = 0;
        history_length   = 0;
        num_deepstack    = 0;
        vit_run          = false;
    }

    int Qwen3VLModel::greedy_search(bm_device_mem_t& logits_mem) {
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

    int Qwen3VLModel::penalty_sample(bm_device_mem_t& logits_mem) {
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

    int Qwen3VLModel::generate(bm_device_mem_t& logits_mem) {
        if (lmhead_with_topk) {
            int token = 0;
            safety::CopyDeviceToHost(bm_handle_, &token, logits_mem, sizeof(token), "copy top-k token");
            return token;
        }
        if (do_sample && net_sample_head_)
            return penalty_sample(logits_mem);
        return greedy_search(logits_mem);
    }

    void Qwen3VLModel::forward_embed(const ArrayInt& tokens) {
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
        for (auto& mem : deepstack_buffers_)
            empty(bm_handle_, mem);
    }

    void Qwen3VLModel::forward_vit(const float* pixel_values, const ArrayInt& position_ids,
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
        if (vit_dynamic) {
            in_tensors[0].shape.dims[0] = static_cast<int>(hw);
            in_tensors[1].shape.dims[0] = static_cast<int>(hw);
            in_tensors[2].shape.dims[0] = static_cast<int>(hw);
            in_tensors[3].shape.dims[0] = static_cast<int>(hw);
        } else {
            const size_t mask_elements =
                safety::CheckedMultiply(static_cast<size_t>(MAX_PATCHES), static_cast<size_t>(MAX_PATCHES),
                                        "calculate vision attention mask");
            std::vector<float> attention_mask(mask_elements, -10000.0f);
            for (size_t i = 0; i < hw; i++) {
                auto row_begin = attention_mask.begin() + i * static_cast<size_t>(MAX_PATCHES);
                std::fill(row_begin, row_begin + static_cast<std::ptrdiff_t>(hw), 0.0f);
            }
            safety::CopyHostToDevice(bm_handle_, in_tensors[4].device_mem, attention_mask.data(),
                                     attention_mask.size() * sizeof(float), "copy vision attention mask");
        }
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
        for (int i = 0; i < num_deepstack; i++) {
            safety::CopyDeviceToDevice(bm_handle_, deepstack_buffers_[i], dst_offset,
                                       out_tensors[i + 1].device_mem, 0, vit_size, "copy deepstack output");
        }
        vit_run = true;
    }

    int Qwen3VLModel::forward_first(const ArrayInt& position_ids) {
        if (token_length <= 0 || token_length >= SEQLEN ||
            position_ids.size() != static_cast<size_t>(token_length) * 3U ||
            static_cast<size_t>(token_length) >= visited_tokens.size()) {
            throw safety::RuntimeError("prefill model", "invalid token or position count");
        }
        if (support_history)
            return forward_first_with_kv(position_ids);
        const int* p_ids = position_ids.data();
        std::vector<int> position_ids_pad;
        std::vector<uint16_t> attention_mask;
        if (is_dynamic) {
            const size_t mask_elements =
                safety::CheckedMultiply(static_cast<size_t>(token_length), static_cast<size_t>(token_length),
                                        "calculate prefill attention mask");
            attention_mask.assign(mask_elements, mask_value);
            for (int i = 0; i < token_length; i++)
                for (int j = 0; j <= i; j++)
                    attention_mask[static_cast<size_t>(i) * static_cast<size_t>(token_length) +
                                   static_cast<size_t>(j)] = 0;
            position_ids_pad.assign(static_cast<size_t>(token_length) * 3U, 0);
            std::copy(position_ids.begin(), position_ids.end(), position_ids_pad.begin());
        } else {
            int length                 = MAX_INPUT_LENGTH;
            const size_t mask_elements = safety::CheckedMultiply(
                static_cast<size_t>(length), static_cast<size_t>(length), "calculate prefill attention mask");
            attention_mask.assign(mask_elements, mask_value);
            for (int i = 0; i < token_length; i++)
                for (int j = 0; j <= i; j++)
                    attention_mask[static_cast<size_t>(i) * static_cast<size_t>(length) +
                                   static_cast<size_t>(j)] = 0;
            position_ids_pad.assign(static_cast<size_t>(length) * 3U, 0);
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
                    safety::CopyHostToDevice(bm_handle_, in_tensors[1].device_mem, position_ids_pad.data(),
                                             position_ids_pad.size() * sizeof(int), "copy prefill positions");
                    safety::CopyHostToDevice(bm_handle_, in_tensors[2].device_mem, attention_mask.data(),
                                             attention_mask.size() * sizeof(uint16_t),
                                             "copy prefill attention mask");
                }
                in_tensors[0].shape.dims[1] = token_length;
                in_tensors[1].shape.dims[1] = token_length;
                in_tensors[2].shape.dims[2] = token_length;
                in_tensors[2].shape.dims[3] = token_length;
            } else {
                in_tensors[0].device_mem = out_mem;
                if (is_fa_layer(idx)) {
                    safety::CopyHostToDevice(bm_handle_, in_tensors[1].device_mem, position_ids_pad.data(),
                                             position_ids_pad.size() * sizeof(int), "copy prefill positions");
                    safety::CopyHostToDevice(bm_handle_, in_tensors[2].device_mem, attention_mask.data(),
                                             attention_mask.size() * sizeof(uint16_t),
                                             "copy prefill attention mask");
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
                const size_t kv_size =
                    safety::CheckedMultiply(static_cast<size_t>(KV_BYTES), static_cast<size_t>(token_length),
                                            "calculate prefill cache size");
                safety::CopyDeviceToDevice(bm_handle_, past_key_[idx], 0,
                                           net_blocks_[idx]->stages[0].output_mems[1], 0, kv_size,
                                           "copy prefill key cache");
                safety::CopyDeviceToDevice(bm_handle_, past_value_[idx], 0,
                                           net_blocks_[idx]->stages[0].output_mems[2], 0, kv_size,
                                           "copy prefill value cache");
            }
        }
        vit_run                = false;
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

    int Qwen3VLModel::forward_first_with_kv(const ArrayInt& position_ids) {
        if (history_length < 0 || history_length > PREFILL_KV_LENGTH || token_length <= 0 ||
            token_length > MAX_INPUT_LENGTH ||
            position_ids.size() != static_cast<size_t>(token_length) * 3U) {
            throw safety::RuntimeError("prefill model with history", "invalid history or input length");
        }
        const size_t max_kv_length =
            static_cast<size_t>(MAX_INPUT_LENGTH) + static_cast<size_t>(PREFILL_KV_LENGTH);
        const size_t mask_elements = safety::CheckedMultiply(
            static_cast<size_t>(MAX_INPUT_LENGTH), max_kv_length, "calculate history attention mask");
        std::vector<uint16_t> attention_mask(mask_elements, mask_value);
        const int old_length     = history_length;
        const size_t new_history = static_cast<size_t>(old_length) + static_cast<size_t>(token_length);
        if (new_history >= static_cast<size_t>(SEQLEN)) {
            throw safety::RuntimeError("prefill model with history", "sequence limit exceeded");
        }
        for (int i = 0; i < token_length; i++) {
            for (int j = 0; j < old_length; j++)
                attention_mask[static_cast<size_t>(i) * max_kv_length + static_cast<size_t>(j)] = 0;
            for (int j = 0; j <= i; j++)
                attention_mask[static_cast<size_t>(i) * max_kv_length + static_cast<size_t>(j) +
                               static_cast<size_t>(PREFILL_KV_LENGTH)] = 0;
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
                const size_t history_bytes =
                    safety::CheckedMultiply(static_cast<size_t>(KV_BYTES), static_cast<size_t>(old_length),
                                            "calculate history cache size");
                safety::CopyDeviceToDevice(bm_handle_, in_tensors[3].device_mem, 0, past_key_[idx], 0,
                                           history_bytes, "copy history key cache");
                safety::CopyDeviceToDevice(bm_handle_, in_tensors[4].device_mem, 0, past_value_[idx], 0,
                                           history_bytes, "copy history value cache");
            } else if (idx == 0) {
                empty(bm_handle_, in_tensors[3].device_mem);
                empty(bm_handle_, in_tensors[4].device_mem);
            }
            safety::CopyHostToDevice(bm_handle_, in_tensors[1].device_mem, position_ids_pad.data(),
                                     position_ids_pad.size() * sizeof(int), "copy prefill positions");
            safety::CopyHostToDevice(bm_handle_, in_tensors[2].device_mem, attention_mask.data(),
                                     attention_mask.size() * sizeof(uint16_t), "copy prefill attention mask");
            net_launch(net_blocks_[idx], in_tensors, out_tensors);
            out_mem = net_blocks_[idx]->stages[0].output_mems[0];
            if (vit_run && idx < num_deepstack) {
                init_tensors(net_add_, in_tensors, out_tensors);
                in_tensors[0].device_mem = out_mem;
                in_tensors[1].device_mem = deepstack_buffers_[idx];
                net_launch(net_add_, in_tensors, out_tensors);
                out_mem = net_add_->stages[0].output_mems[0];
            }
            const size_t cache_offset =
                safety::CheckedMultiply(static_cast<size_t>(old_length), static_cast<size_t>(KV_BYTES),
                                        "calculate prefill cache offset");
            const size_t cache_size =
                safety::CheckedMultiply(static_cast<size_t>(token_length), static_cast<size_t>(KV_BYTES),
                                        "calculate prefill cache size");
            safety::CopyDeviceToDevice(bm_handle_, past_key_[idx], cache_offset,
                                       net_blocks_[idx]->stages[0].output_mems[1], 0, cache_size,
                                       "copy prefill key cache");
            safety::CopyDeviceToDevice(bm_handle_, past_value_[idx], cache_offset,
                                       net_blocks_[idx]->stages[0].output_mems[2], 0, cache_size,
                                       "copy prefill value cache");
        }
        vit_run                = false;
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
        history_length = static_cast<int>(new_history) + 1;
        return token;
    }

    int Qwen3VLModel::forward_next(const ArrayInt& position_ids) {
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
        bm_device_mem_t out_mem = out_tensors[0].device_mem;
        const size_t bytes =
            bm_mem_get_device_size(net_blocks_cache_[first_kv_layer()]->stages[0].output_mems[1]);
        const size_t token_offset = safety::CheckedMultiply(static_cast<size_t>(history_length - 1), bytes,
                                                            "calculate decode cache offset");
        if (bytes == 0 || token_offset > static_cast<size_t>(std::numeric_limits<int>::max())) {
            throw safety::RuntimeError("decode token", "invalid cache geometry");
        }
        for (size_t idx = 0; idx < net_blocks_cache_.size(); idx++) {
            if (is_fa_layer(static_cast<int>(idx))) {
                net_launch_decode(static_cast<int>(idx), static_cast<int>(token_offset), out_mem, p_ids,
                                  attention_mask);
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
