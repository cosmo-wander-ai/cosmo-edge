#include "nn/device/sophon/qwen3vl/qwen3vl_runner.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

#include <nlohmann/json.hpp>

#include "bmlib_runtime.h"
#include "nn/core/blob.h"
#include "nn/core/common.h"
#include "nn/device/sophon/qwen3_5/qwen3_5_model.h"
#include "nn/device/sophon/qwen3vl/qwen3vl_config.h"
#include "nn/device/sophon/qwen3vl/qwen3vl_image_utils.h"
#include "nn/device/sophon/qwen3vl/qwen3vl_model.h"
#include "nn/device/sophon/qwen3vl/qwen3vl_predict_utils.h"
#include "nn/device/sophon/qwen3vl/qwen3vl_runner_factory.h"
#include "tokenizers_cpp.h"

namespace cosmo::nn {

namespace {

    const int SPATIAL_MERGE_SIZE = 2;
    const int NUM_GRID_PER_SIDE  = 48;
    const int TOKENS_PER_SECOND  = 2;
    constexpr std::streamoff kMaxJsonFileBytes = 10 * 1024 * 1024;

    bool LooksLikeJsonContent(const std::string& value) {
        const auto first = value.find_first_not_of(" \t\r\n");
        return first != std::string::npos && (value[first] == '{' || value[first] == '[');
    }

    std::string EscapeForLog(const std::string& s, size_t max_len = 512) {
        std::string out;
        out.reserve(std::min(s.size(), max_len));
        for (size_t i = 0; i < s.size() && out.size() < max_len; ++i) {
            char c = s[i];
            if (c == '\n') {
                out += "\\n";
            } else if (c == '\r') {
                out += "\\r";
            } else if (c == '\t') {
                out += "\\t";
            } else {
                out += c;
            }
        }
        if (s.size() > max_len)
            out += "...";
        return out;
    }

    std::string TokenPreview(const std::vector<int>& tokens, size_t max_count = 32) {
        std::ostringstream oss;
        oss << "[";
        size_t n = std::min(tokens.size(), max_count);
        for (size_t i = 0; i < n; ++i) {
            if (i > 0)
                oss << ",";
            oss << tokens[i];
        }
        if (tokens.size() > max_count)
            oss << ",...";
        oss << "]";
        return oss.str();
    }

    std::string LoadBytesFromFile(const std::string& path) {
        std::ifstream fs(path, std::ios::in | std::ios::binary);
        if (!fs.good())
            return "";
        std::string data;
        fs.seekg(0, std::ios::end);
        data.resize(static_cast<size_t>(fs.tellg()));
        fs.seekg(0, std::ios::beg);
        if (!data.empty())
            fs.read(&data[0], static_cast<std::streamsize>(data.size()));
        return data;
    }

    // Copy BGR from device image blob to host, then call process_image_from_mat
    bool GetPixelValuesFromImageBlob(Blob* image_blob, bm_handle_t bm_handle, qwen3vl::Config& config,
                                     std::vector<float>& pixel_values) {
        const BlobDesc& desc     = image_blob->GetBlobDesc();
        const BlobHandle& handle = image_blob->GetHandle();
        const DimsVector& d      = desc.dims;
        if (d.size() < 4 || d[0] != 1 || d[3] != 3)
            return false;
        int height        = d[1];
        int width         = d[2];
        size_t size_bytes = static_cast<size_t>(height) * width * 3;
        if (desc.device_type == DEVICE_SOPHON_TPU && handle.base) {
            bm_device_mem_t* dev_mem = static_cast<bm_device_mem_t*>(handle.base);
            std::vector<unsigned char> host_buf(size_bytes);
            bm_status_t ret = bm_memcpy_d2s(bm_handle, host_buf.data(), *dev_mem);
            if (ret != BM_SUCCESS)
                return false;
            return qwen3vl::process_image_from_mat(host_buf.data(), width, height, width * 3, config,
                                                   pixel_values);
        }
        if (desc.device_type == DEVICE_NAIVE && handle.base) {
            const unsigned char* bgr = static_cast<const unsigned char*>(handle.base);
            return qwen3vl::process_image_from_mat(bgr, width, height, width * 3, config, pixel_values);
        }
        return false;
    }

    nlohmann::json ParseModelConfigRoot(const std::string& model_config_json_path) {
        if (LooksLikeJsonContent(model_config_json_path)) {
            auto root = nlohmann::json::parse(model_config_json_path, nullptr, false);
            if (!root.is_discarded())
                return root.is_object() ? root : nlohmann::json::object();
        }

        std::ifstream in(model_config_json_path, std::ios::binary | std::ios::ate);
        if (in.good()) {
            const auto size = in.tellg();
            if (size < 0 || size > kMaxJsonFileBytes)
                return nlohmann::json::object();
            in.seekg(0, std::ios::beg);
            auto root = nlohmann::json::parse(in, nullptr, false);
            return root.is_discarded() || !root.is_object() ? nlohmann::json::object() : root;
        }
        return nlohmann::json::object();
    }

    bool ReadDoSampleFromModelConfig(const std::string& model_config_json_path) {
        nlohmann::json root     = ParseModelConfigRoot(model_config_json_path);
        const nlohmann::json* g = nullptr;
        if (root.contains("config") && root["config"].is_object() && root["config"].contains("generation") &&
            root["config"]["generation"].is_object()) {
            g = &root["config"]["generation"];
        } else if (root.contains("generation") && root["generation"].is_object()) {
            g = &root["generation"];
        }
        if (!g)
            return false;
        if (!g->contains("do_sample") || !(*g)["do_sample"].is_boolean())
            return false;
        return (*g)["do_sample"].get<bool>();
    }

    // Read optional max_pixels config from config.json model_params
    // Returns 0 if not configured (use default)
    int ReadMaxPixelsFromModelConfig(const std::string& model_config_json_path) {
        nlohmann::json root      = ParseModelConfigRoot(model_config_json_path);
        const nlohmann::json* mp = nullptr;
        if (root.contains("config") && root["config"].is_object() && root["config"].contains("model_params") &&
            root["config"]["model_params"].is_object()) {
            mp = &root["config"]["model_params"];
        } else if (root.contains("model_params") && root["model_params"].is_object()) {
            mp = &root["model_params"];
        }
        if (!mp || !mp->contains("max_pixels") || !(*mp)["max_pixels"].is_number_integer())
            return 0;
        return (*mp)["max_pixels"].get<int>();
    }

    std::string GetPromptFromBlob(Blob* prompt_blob) {
        const BlobDesc& desc     = prompt_blob->GetBlobDesc();
        const BlobHandle& handle = prompt_blob->GetHandle();
        if (!handle.base || desc.dims.size() < 2)
            return "";
        int len       = desc.dims[1];
        const char* p = static_cast<const char*>(handle.base);
        return std::string(p, len);
    }

    /** Consistent with demo: filter generated text -- trim whitespace, remove trailing special tokens,
     * deduplicate repeated paragraphs */
    std::string FilterOutput(const std::string& raw) {
        size_t first = raw.find_first_not_of(" \t\n\r");
        if (first == std::string::npos)
            return "";
        size_t last                = raw.find_last_not_of(" \t\n\r");
        std::string s              = raw.substr(first, last - first + 1);
        const std::string im_end   = "<|im_end|>";
        const std::string im_start = "<|im_start|>";
        bool changed               = true;
        while (changed && !s.empty()) {
            changed = false;
            if (s.size() >= im_end.size() &&
                s.compare(s.size() - im_end.size(), im_end.size(), im_end) == 0) {
                s.resize(s.size() - im_end.size());
                changed = true;
            }
            if (s.size() >= im_start.size() &&
                s.compare(s.size() - im_start.size(), im_start.size(), im_start) == 0) {
                s.resize(s.size() - im_start.size());
                changed = true;
            }
            if (changed) {
                last = s.find_last_not_of(" \t\n\r");
                if (last == std::string::npos)
                    s.clear();
                else
                    s.resize(last + 1);
            }
        }
        /* Remove repeated paragraphs: split by double newline, keep only paragraphs different from previous
         */
        std::string out;
        std::string prev_para;
        for (size_t i = 0; i <= s.size();) {
            size_t j = s.find("\n\n", i);
            if (j == std::string::npos)
                j = s.size();
            std::string para = s.substr(i, j - i);
            size_t p_first   = para.find_first_not_of(" \t\n\r");
            if (p_first != std::string::npos) {
                size_t p_last = para.find_last_not_of(" \t\n\r");
                para          = para.substr(p_first, p_last - p_first + 1);
            } else {
                para.clear();
            }
            if (!para.empty() && para != prev_para) {
                if (!out.empty())
                    out += "\n\n";
                out += para;
                prev_para = para;
            }
            i = (j == s.size()) ? s.size() + 1 : j + 2;
        }
        /* Remove intra-sentence repetition: split by period/semicolon, keep only sentences different from
         * previous, avoid "A. A. A." pattern */
        s = out;
        out.clear();
        prev_para.clear();
        const char sep[] = "。；.";
        for (size_t i = 0; i < s.size();) {
            size_t j = s.find_first_of(sep, i);
            if (j == std::string::npos)
                j = s.size();
            else
                j++;
            std::string seg = s.substr(i, j - i);
            size_t sf       = seg.find_first_not_of(" \t\n\r");
            if (sf != std::string::npos) {
                size_t sl = seg.find_last_not_of(" \t\n\r");
                seg       = seg.substr(sf, sl - sf + 1);
            } else {
                seg.clear();
            }
            if (!seg.empty() && seg != prev_para) {
                out += seg;
                prev_para = seg;
            }
            i = j;
        }
        return out;
    }

    // Detect if the same phrase repeats consecutively at the end of text (e.g. "XXX. XXX."), used for early
    // stopping
    static bool EndsWithRepeatedPhrase(const std::string& text, size_t min_phrase_len = 12,
                                       int repeat_count = 2) {
        if (text.size() < static_cast<size_t>(repeat_count) * min_phrase_len)
            return false;
        size_t len = min_phrase_len;
        while (len <= text.size() / static_cast<size_t>(repeat_count)) {
            std::string phrase = text.substr(text.size() - len, len);
            bool all_same      = true;
            for (int k = 1; k < repeat_count && all_same; k++) {
                if (text.size() < (k + 1) * len || text.substr(text.size() - (k + 1) * len, len) != phrase)
                    all_same = false;
            }
            if (all_same)
                return true;
            len++;
            if (len > 80u)
                break;
        }
        return false;
    }

}  // namespace

struct Qwen3VLRunner::Impl {
    std::string model_path;
    std::string model_type;  // "qwen3vl" or "qwen3_5"
    int device_id  = 0;
    bool do_sample = false;
    bool inited    = false;

    // Only one of the two model types is instantiated
    std::unique_ptr<qwen3vl::Qwen3VLModel> model_vl;
    std::unique_ptr<qwen3_5::Qwen3_5Model> model_35;
    std::unique_ptr<tokenizers::Tokenizer> tok;
    qwen3vl::Config config;
    std::unique_ptr<qwen3vl::Maker> maker;
    int ID_IM_END       = 0;
    int ID_VISION_START = 0;
    int IMAGE_PAD_TOKEN = 0;

    bool is_qwen35() const {
        return model_type == "qwen3_5";
    }
};

Qwen3VLRunner::~Qwen3VLRunner() = default;

// Opaque interface for default_component.cc (avoids instantiating unique_ptr<Impl> destructor in that TU)
namespace {

    Qwen3VLRunner* Create() {
        return new Qwen3VLRunner();
    }
    void Destroy(Qwen3VLRunner* r) {
        delete r;
    }

}  // namespace

Qwen3VLRunner* Qwen3VLRunner_Create() {
    return Create();
}
void Qwen3VLRunner_Destroy(Qwen3VLRunner* r) {
    Destroy(r);
}
Status Qwen3VLRunner_Init(Qwen3VLRunner* r, const std::string& model_path, const std::string& tokenizer_path,
                          int device_id, const std::string& model_config_json_path,
                          const std::string& model_type) {
    return r->Init(model_path, tokenizer_path, device_id, model_config_json_path, model_type);
}
Status Qwen3VLRunner_Run(Qwen3VLRunner* r, const std::vector<std::vector<std::shared_ptr<Blob>>>& inputs) {
    return r->Run(inputs);
}
const std::vector<std::vector<std::string>>& Qwen3VLRunner_GetTextOutputs(const Qwen3VLRunner* r) {
    return r->GetTextOutputs();
}
int Qwen3VLRunner_GetMaxBatchSize(const Qwen3VLRunner* r) {
    return r->GetMaxBatchSize();
}

Status Qwen3VLRunner::Init(const std::string& model_path, const std::string& tokenizer_path, int device_id,
                           const std::string& model_config_json_path, const std::string& model_type) {
    impl_.reset(new Impl());
    impl_->model_path = model_path;
    impl_->model_type = model_type;
    impl_->device_id  = device_id;
    impl_->do_sample  = false;
    if (!model_config_json_path.empty())
        impl_->do_sample = ReadDoSampleFromModelConfig(model_config_json_path);

    std::string blob = LoadBytesFromFile(tokenizer_path);
    if (blob.empty())
        return Status(COSMO_NN_ERR_OPEN_FILE, "Failed to load tokenizer: " + tokenizer_path);
    impl_->tok = tokenizers::Tokenizer::FromBlobJSON(blob);
    if (!impl_->tok)
        return Status(COSMO_NN_ERR_PARAM, "Tokenizer::FromBlobJSON failed");
    impl_->ID_IM_END       = impl_->tok->TokenToId("<|im_end|>");
    impl_->ID_VISION_START = impl_->tok->TokenToId("<|vision_start|>");
    impl_->IMAGE_PAD_TOKEN = impl_->tok->TokenToId("<|image_pad|>");
    std::cerr << "[Qwen3VLRunner] init model_type=" << impl_->model_type << " device_id=" << impl_->device_id
              << " do_sample=" << (impl_->do_sample ? 1 : 0) << " config_json="
              << (model_config_json_path.empty()
                      ? "(empty)"
                      : (LooksLikeJsonContent(model_config_json_path) ? "(inline_json)"
                                                                      : model_config_json_path))
              << " tokenizer=" << tokenizer_path << " id_im_end=" << impl_->ID_IM_END
              << " id_vision_start=" << impl_->ID_VISION_START << " image_pad=" << impl_->IMAGE_PAD_TOKEN
              << std::endl;

    // Instantiate different underlying models based on model_type
    if (impl_->is_qwen35()) {
        impl_->model_35.reset(new qwen3_5::Qwen3_5Model());
        impl_->model_35->init(device_id, model_path, impl_->do_sample, model_config_json_path);
        impl_->config.SEQLEN             = impl_->model_35->SEQLEN;
        impl_->config.MAX_INPUT_LENGTH   = impl_->model_35->MAX_INPUT_LENGTH;
        impl_->config.MAX_PREFILL_LENGTH = impl_->model_35->MAX_INPUT_LENGTH;
        impl_->config.MAX_PIXELS         = impl_->model_35->MAX_PIXELS;
        impl_->config.MAX_PATCHES        = impl_->model_35->MAX_PATCHES;
    } else {
        impl_->model_vl.reset(new qwen3vl::Qwen3VLModel());
        impl_->model_vl->init(device_id, model_path, impl_->do_sample, model_config_json_path);
        impl_->config.SEQLEN             = impl_->model_vl->SEQLEN;
        impl_->config.MAX_INPUT_LENGTH   = impl_->model_vl->MAX_INPUT_LENGTH;
        impl_->config.MAX_PREFILL_LENGTH = impl_->model_vl->MAX_INPUT_LENGTH;
        impl_->config.MAX_PIXELS         = impl_->model_vl->MAX_PIXELS;
        impl_->config.MAX_PATCHES        = impl_->model_vl->MAX_PATCHES;
    }
    impl_->config.temporal_patch_size = 2;
    impl_->config.spatial_merge_size  = SPATIAL_MERGE_SIZE;
    impl_->config.patch_size          = 16;
    impl_->config.MIN_PIXELS          = 64 * 32 * 32;

    // Read optional max_pixels override from config.json
    // Qwen3_5 is a small model, high-res images produce too many image tokens overwhelming text instructions,
    // causing the model to fail following the prompt. Resolved by limiting max_pixels to reduce image
    // resolution. Default 301056 (~672x448) is a demo-verified resolution that works correctly.
    int cfg_max_pixels = ReadMaxPixelsFromModelConfig(model_config_json_path);
    if (cfg_max_pixels > 0) {
        // Config file explicitly specifies max_pixels, use config value (capped by bmodel limit)
        impl_->config.MAX_PIXELS = std::min(cfg_max_pixels, impl_->config.MAX_PIXELS);
        std::cerr << "[Qwen3VLRunner] max_pixels overridden by config: " << impl_->config.MAX_PIXELS
                  << std::endl;
    } else if (impl_->is_qwen35()) {
        // Qwen3_5 uses default limit when not configured
        const int QWEN35_DEFAULT_MAX_PIXELS = 301056;  // ~672x448, verified by demo
        if (impl_->config.MAX_PIXELS > QWEN35_DEFAULT_MAX_PIXELS) {
            std::cerr << "[Qwen3VLRunner] qwen3_5 auto-limit max_pixels: " << impl_->config.MAX_PIXELS
                      << " -> " << QWEN35_DEFAULT_MAX_PIXELS
                      << " (override via model_params.max_pixels in config.json)" << std::endl;
            impl_->config.MAX_PIXELS = QWEN35_DEFAULT_MAX_PIXELS;
        }
    }
    impl_->maker.reset(new qwen3vl::Maker(impl_->config));
    impl_->inited   = true;
    max_batch_size_ = 1;
    std::cerr << "[Qwen3VLRunner] model limits model_type=" << impl_->model_type
              << " seqlen=" << impl_->config.SEQLEN << " max_input=" << impl_->config.MAX_INPUT_LENGTH
              << " max_pixels=" << impl_->config.MAX_PIXELS << " max_patches=" << impl_->config.MAX_PATCHES
              << std::endl;
    return COSMO_NN_OK;
}

// Templated inference loop, avoids Qwen3VL / Qwen3.5 code duplication
template <typename Model>
Status Qwen3VLRunner::RunImpl(Model& model, Qwen3VLRunner::Impl& impl,
                              const std::vector<std::vector<std::shared_ptr<Blob>>>& inputs,
                              std::vector<std::vector<std::string>>& text_outputs) {
    const auto& images  = inputs[0];
    const auto& prompts = inputs[1];

    bm_handle_t bm_handle = static_cast<bm_handle_t>(model.get_bm_handle());
    text_outputs.clear();
    text_outputs.resize(images.size());

    for (size_t i = 0; i < images.size(); i++) {
        Blob* image_blob     = images[i].get();
        Blob* prompt_blob    = prompts[i].get();
        std::string question = GetPromptFromBlob(prompt_blob);
        if (question.empty())
            return Status(COSMO_NN_ERR_INVALID_INPUT, "empty prompt");
        const BlobDesc& image_desc = image_blob->GetBlobDesc();
        std::cerr << "[Qwen3VLRunner] run begin model_type=" << impl.model_type << " item=" << i
                  << " image_dims=" << (image_desc.dims.size() > 0 ? image_desc.dims[0] : -1) << "x"
                  << (image_desc.dims.size() > 1 ? image_desc.dims[1] : -1) << "x"
                  << (image_desc.dims.size() > 2 ? image_desc.dims[2] : -1) << "x"
                  << (image_desc.dims.size() > 3 ? image_desc.dims[3] : -1) << " question=\""
                  << EscapeForLog(question, 256) << "\"" << std::endl;

        impl.config.max_pos = 0;
        impl.config.grid_thw.clear();
        std::vector<float> pixel_values;
        if (!GetPixelValuesFromImageBlob(image_blob, bm_handle, impl.config, pixel_values))
            return Status(COSMO_NN_ERR_INVALID_INPUT, "failed to process image");

        std::vector<std::vector<int>> grid_thws = {impl.config.grid_thw};
        std::string sentence_input    = qwen3vl::BuildImagePrompt(question, grid_thws, impl.is_qwen35());
        std::vector<int32_t> tokens_i = impl.tok->Encode(sentence_input);
        std::vector<int> tokens(tokens_i.begin(), tokens_i.end());
        std::cerr << "[Qwen3VLRunner] prompt built model_type=" << impl.model_type
                  << " append_empty_think=" << (impl.is_qwen35() ? 1 : 0) << " grid_thw=["
                  << impl.config.grid_thw[0] << "," << impl.config.grid_thw[1] << ","
                  << impl.config.grid_thw[2] << "]"
                  << " pixel_values=" << pixel_values.size() << " tokens=" << tokens.size()
                  << " token_preview=" << TokenPreview(tokens) << " prompt=\""
                  << EscapeForLog(sentence_input, 512) << "\"" << std::endl;
        if (static_cast<int>(tokens.size()) > model.MAX_INPUT_LENGTH)
            return Status(COSMO_NN_ERR_INVALID_INPUT, "input tokens exceed max length");

        std::vector<int> vit_offset = qwen3vl::FindTokenOffset(tokens, impl.ID_VISION_START);
        if (vit_offset.empty())
            return Status(COSMO_NN_ERR_INVALID_INPUT, "no vision token in input");
        std::cerr << "[Qwen3VLRunner] vit offsets model_type=" << impl.model_type
                  << " count=" << vit_offset.size() << " first=" << vit_offset[0] << std::endl;

        model.clear_history();
        model.forward_embed(tokens);

        std::vector<std::vector<int>> grid_thw    = {impl.config.grid_thw};
        std::vector<std::vector<int>> pos_ids_vec = qwen3vl::RotPos(grid_thw, SPATIAL_MERGE_SIZE);
        std::vector<int> position_ids;
        for (const auto& v : pos_ids_vec)
            position_ids.insert(position_ids.end(), v.begin(), v.end());
        std::vector<int> pos_ids;
        std::vector<float> pos_weight;
        qwen3vl::FastPosEmbedInterpolate(impl.config.grid_thw, NUM_GRID_PER_SIDE, SPATIAL_MERGE_SIZE, pos_ids,
                                         pos_weight);
        model.forward_vit(pixel_values.data(), position_ids, pos_ids, pos_weight, impl.config.grid_thw,
                          vit_offset[0] + 1);

        std::vector<std::vector<int>> position_ids_3d =
            qwen3vl::GetRopeIndex(tokens, grid_thws, impl.IMAGE_PAD_TOKEN, impl.ID_VISION_START,
                                  SPATIAL_MERGE_SIZE, TOKENS_PER_SECOND);
        std::vector<int> position_ids_1d;
        for (const auto& dim_tensor : position_ids_3d)
            position_ids_1d.insert(position_ids_1d.end(), dim_tensor.begin(), dim_tensor.end());

        int max_posid = 0;
        for (int pos : position_ids_3d[0]) {
            max_posid = std::max(max_posid, pos);
        }
        impl.config.max_pos = max_posid;
        std::cerr << "[Qwen3VLRunner] prefill position model_type=" << impl.model_type
                  << " max_posid=" << max_posid << " position_ids_1d=" << position_ids_1d.size() << std::endl;

        int token = model.forward_first(position_ids_1d);
        std::string text;
        std::vector<int> full_word_tokens;
        std::string prev_decoded;
        using clock             = std::chrono::steady_clock;
        auto t_start            = clock::now();
        std::string stop_reason = "unknown";
        while (token != impl.ID_IM_END && model.history_length < model.SEQLEN) {
            full_word_tokens.push_back(token);
            std::string full_decoded = impl.tok->Decode(full_word_tokens);
            size_t prev_len          = prev_decoded.size();
            if (full_decoded.size() >= prev_len)
                text += full_decoded.substr(prev_len);
            prev_decoded = full_decoded;
            if (model.do_sample && model.check_stop(text)) {
                stop_reason = "stop_string";
                break;
            }
            if (EndsWithRepeatedPhrase(text, 12, 2)) {
                stop_reason = "repeated_phrase";
                break;
            }
            std::vector<int> next_pos = impl.maker->make_next_position_id();
            token                     = model.forward_next(next_pos);
        }
        if (token == impl.ID_IM_END)
            stop_reason = "im_end";
        else if (model.history_length >= model.SEQLEN)
            stop_reason = "seqlen";
        auto t_end         = clock::now();
        double elapsed_s   = std::chrono::duration<double>(t_end - t_start).count();
        int num_tokens     = static_cast<int>(full_word_tokens.size());
        double tok_per_sec = (elapsed_s > 0.0 && num_tokens > 0) ? (num_tokens / elapsed_s) : 0.0;
        std::string tag    = impl.is_qwen35() ? "Qwen3_5" : "Qwen3VL";
        std::cerr << "[" << tag << "] generated " << num_tokens << " tokens in " << std::fixed
                  << std::setprecision(2) << elapsed_s << " s, " << std::setprecision(1) << tok_per_sec
                  << " tok/s"
                  << " stop=" << stop_reason << " raw_text=\"" << EscapeForLog(text, 512) << "\""
                  << std::endl;
        text_outputs[i].resize(1);
        text_outputs[i][0] = FilterOutput(text);
    }
    return COSMO_NN_OK;
}

Status Qwen3VLRunner::Run(const std::vector<std::vector<std::shared_ptr<Blob>>>& inputs) {
    if (!impl_ || !impl_->inited)
        return Status(COSMO_NN_ERR_GRAPH_NOT_INIT, "runner not inited");
    if (inputs.size() < 2)
        return Status(COSMO_NN_ERR_INVALID_INPUT, "needs images and prompts");
    const auto& images  = inputs[0];
    const auto& prompts = inputs[1];
    if (images.size() != prompts.size() || images.empty())
        return Status(COSMO_NN_ERR_INVALID_INPUT, "images and prompts size mismatch");

    if (impl_->is_qwen35())
        return RunImpl(*impl_->model_35, *impl_, inputs, text_outputs_);
    else
        return RunImpl(*impl_->model_vl, *impl_, inputs, text_outputs_);
}

}  // namespace cosmo::nn
