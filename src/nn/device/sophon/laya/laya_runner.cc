#include "nn/device/sophon/laya/laya_runner.h"

#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <stdexcept>

#include "bmruntime_interface.h"
#include "nn/core/blob.h"
#include "nn/utils/laya_frontend.h"
#include "nn/utils/model_header_info.h"
#include "tokenizers_cpp.h"

namespace cosmo::nn {
namespace {
    void Require(bool condition, const std::string& message) {
        if (!condition)
            throw std::runtime_error("Laya: " + message);
    }
    nlohmann::json JsonFile(const std::filesystem::path& path) {
        std::ifstream stream(path);
        Require(stream.good(), "cannot open " + path.filename().string());
        return nlohmann::json::parse(stream);
    }
    struct Mapping {
        void* data   = nullptr;
        size_t bytes = 0;
        explicit Mapping(const std::string& path) {
            bytes = std::filesystem::file_size(path);
            Require(bytes > 0, "empty model container");
            int fd = open(path.c_str(), O_RDONLY);
            Require(fd >= 0, "cannot open model container");
            data = mmap(nullptr, bytes, PROT_READ, MAP_PRIVATE, fd, 0);
            close(fd);
            if (data == MAP_FAILED) {
                data = nullptr;
                throw std::runtime_error("Laya: model mmap failed");
            }
        }
        ~Mapping() {
            if (data)
                munmap(data, bytes);
        }
    };
    struct Network {
        bm_handle_t handle = nullptr;
        void* runtime      = nullptr;
        std::string name;
        const bm_net_info_t* info = nullptr;
        std::vector<bm_tensor_t> inputs, outputs;
        std::vector<bm_device_mem_t> owned;
        ~Network() {
            if (handle)
                bm_handle_sync(handle);
            for (auto memory : owned)
                bm_free_device(handle, memory);
            if (runtime)
                bmrt_destroy(runtime);
        }
        static size_t Elements(const bm_shape_t& shape) {
            Require(shape.num_dims > 0 && shape.num_dims <= BM_MAX_DIMS_NUM, "invalid tensor rank");
            size_t count = 1;
            for (int i = 0; i < shape.num_dims; ++i) {
                Require(shape.dims[i] > 0 && count <= 16 * 1024 * 1024 / size_t(shape.dims[i]),
                        "invalid tensor size");
                count *= shape.dims[i];
            }
            return count;
        }
        void Load(bm_handle_t device, const char* data, size_t bytes, const std::string& expected) {
            handle  = device;
            runtime = bmrt_create(handle);
            Require(runtime, "bmrt_create failed");
            Require(bytes <= std::numeric_limits<unsigned int>::max() &&
                        bmrt_load_bmodel_data(runtime, data, bytes),
                    "model segment load failed");
            Require(bmrt_get_network_number(runtime) == 1, "one network per segment is required");
            name = expected;
            info = bmrt_get_network_info(runtime, name.c_str());
            Require(info && !info->is_dynamic && info->stage_num == 1, "unexpected network or stage");
            inputs.resize(info->input_num);
            outputs.resize(info->output_num);
            owned.reserve(inputs.size() + outputs.size());
            for (bool input : {true, false}) {
                auto& tensors = input ? inputs : outputs;
                for (size_t i = 0; i < tensors.size(); ++i) {
                    auto dtype = input ? info->input_dtypes[i] : info->output_dtypes[i];
                    auto shape = input ? info->stages[0].input_shapes[i] : info->stages[0].output_shapes[i];
                    Require(dtype == BM_FLOAT32 && shape.dims[0] == 1, "expected FP32 batch-one model I/O");
                    Elements(shape);
                    Require(bmrt_tensor(&tensors[i], runtime, dtype, shape), "tensor allocation failed");
                    owned.push_back(tensors[i].device_mem);
                }
            }
        }
        std::vector<float> Run(const std::map<std::string, const std::vector<float>*>& feeds) {
            Require(feeds.size() == inputs.size() && outputs.size() == 1, "network I/O count mismatch");
            for (size_t i = 0; i < inputs.size(); ++i) {
                auto found = feeds.find(info->input_names[i]);
                Require(found != feeds.end() && found->second &&
                            found->second->size() == Elements(inputs[i].shape),
                        "network input shape mismatch");
                Require(bm_memcpy_s2d_partial(handle, inputs[i].device_mem,
                                              const_cast<float*>(found->second->data()),
                                              found->second->size() * sizeof(float)) == BM_SUCCESS,
                        "input transfer failed");
            }
            Require(bmrt_launch_tensor_ex(runtime, name.c_str(), inputs.data(), int(inputs.size()),
                                          outputs.data(), int(outputs.size()), true, false),
                    "network launch failed");
            Require(bm_thread_sync(handle) == BM_SUCCESS, "network synchronization failed");
            Require(outputs[0].dtype == BM_FLOAT32 &&
                        bmrt_shape_is_same(&outputs[0].shape, &info->stages[0].output_shapes[0]),
                    "runtime output shape changed");
            std::vector<float> result(Elements(outputs[0].shape));
            Require(bm_memcpy_d2s_partial(handle, result.data(), outputs[0].device_mem,
                                          result.size() * sizeof(float)) == BM_SUCCESS,
                    "output transfer failed");
            return result;
        }
    };
}  // namespace

struct LayaRunner::Impl {
    bm_handle_t handle = nullptr;
    std::array<std::unique_ptr<Network>, 3> networks;
    std::unique_ptr<tokenizers::Tokenizer> tokenizer;
    std::unique_ptr<laya::EmbeddingTable> embeddings;
    laya::Heads heads;
    nlohmann::json config;
    std::map<std::string, laya::Question> prepared;
    const laya::Question& Compile(const nlohmann::ordered_json& spec) {
        const auto key = spec.dump();
        auto found     = prepared.find(key);
        if (found != prepared.end())
            return found->second;
        auto q = laya::CompileQuestion(
            spec.at("question"), config, [&](const auto& value) { return tokenizer->Encode(value); },
            spec.value("text_state", ""));
        if (prepared.size() >= 512)
            prepared.clear();
        return prepared.emplace(key, std::move(q)).first->second;
    }
    ~Impl() {
        for (auto& network : networks)
            network.reset();
        if (handle)
            bm_dev_free(handle);
    }
};
LayaRunner::LayaRunner()  = default;
LayaRunner::~LayaRunner() = default;

Status LayaRunner::Init(const std::string& model_path, const std::string& tokenizer_path, int device_id) {
    try {
        auto state           = std::make_unique<Impl>();
        const auto directory = std::filesystem::path(model_path).parent_path();
        state->config        = JsonFile(directory / "laya_config.json");
        state->heads         = laya::ReadHeads((directory / "heads").string());
        Require(state->heads.at("type_emb.weight").shape == std::vector<size_t>{3, laya::kHidden},
                "type embedding shape");
        state->embeddings =
            std::make_unique<laya::EmbeddingTable>((directory / "token_embeddings.f16.npy").string());
        auto tokenizer_json = JsonFile(tokenizer_path);
        state->tokenizer =
            tokenizers::Tokenizer::FromBlobJSON(laya::TokenizerJson(std::move(tokenizer_json)));
        Require(state->tokenizer && state->tokenizer->TokenToId("<bos>") == 2 &&
                    state->tokenizer->TokenToId("<eos>") == 1 && state->tokenizer->TokenToId("<mask>") == 4 &&
                    state->tokenizer->TokenToId("<pad>") == 0,
                "tokenizer special token mismatch");
        Mapping container(model_path);
        Require(container.bytes >= kPlainNnHeaderSize, "truncated model container");
        std::array<char, kPlainNnHeaderSize> header{};
        std::copy_n(static_cast<const char*>(container.data), header.size(), header.data());
        auto parsed = ParsePlainNnHeader(header);
        Require(bool(parsed) && parsed.header.model_count == 3, "expected three-segment CENN model");
        Require(bool(ValidatePlainNnFileSize(parsed.header, container.bytes)), "container size mismatch");
        Require(bm_dev_request(&state->handle, device_id) == BM_SUCCESS, "device request failed");
        const std::array<std::string, 3> names = {"laya_vision_tower", "laya_vision_adapter",
                                                  "laya_decision_core"};
        size_t offset                          = kPlainNnHeaderSize;
        for (size_t i = 0; i < names.size(); ++i) {
            state->networks[i] = std::make_unique<Network>();
            state->networks[i]->Load(state->handle, static_cast<const char*>(container.data) + offset,
                                     parsed.header.model_sizes[i], names[i]);
            offset += parsed.header.model_sizes[i];
        }
        impl_ = std::move(state);
        return COSMO_NN_OK;
    } catch (const std::bad_alloc&) {
        return Status(COSMO_NN_ERR_OUT_OF_MEMORY, "Laya initialization ran out of memory");
    } catch (const std::exception& error) {
        return Status(COSMO_NN_ERR_LOAD_MODEL, error.what());
    }
}

Status LayaRunner::PrepareText(const std::string& input, std::string& output) {
    if (!impl_)
        return Status(COSMO_NN_ERR_GRAPH_NOT_INIT, "Laya is not initialized");
    try {
        auto questions = nlohmann::ordered_json::parse(input).at("questions");
        Require(questions.is_array() && !questions.empty() && questions.size() <= 32,
                "expected 1..32 questions");
        nlohmann::json rows = nlohmann::json::array();
        for (const auto& spec : questions) {
            const auto& q = impl_->Compile(spec);
            rows.push_back({{"qtype", q.type},
                            {"ordered_options", q.labels},
                            {"temperature", q.temperature_info},
                            {"tokens", q.tokens},
                            {"markers", q.markers},
                            {"image_position", q.image_position}});
        }
        output = nlohmann::json{{"backend", "laya_v"}, {"questions", rows}}.dump();
        return COSMO_NN_OK;
    } catch (const std::exception& error) {
        return Status(COSMO_NN_ERR_INVALID_INPUT, error.what());
    }
}

Status LayaRunner::Run(const std::vector<std::vector<std::shared_ptr<Blob>>>& inputs) {
    outputs_.clear();
    if (!impl_)
        return Status(COSMO_NN_ERR_GRAPH_NOT_INIT, "Laya is not initialized");
    try {
        Require(inputs.size() == 2 && !inputs[0].empty() && inputs[0].size() == inputs[1].size(),
                "expected images and aligned prompts");
        for (size_t index = 0; index < inputs[0].size(); ++index) {
            Require(inputs[0][index] && inputs[1][index], "missing image or prompt");
            auto& image      = *inputs[0][index];
            auto& prompt     = *inputs[1][index];
            const auto& desc = image.GetBlobDesc();
            const auto& dims = desc.dims;
            Require(desc.device_type == DEVICE_NAIVE && dims.size() == 4 && dims[0] == 1 && dims[3] == 3 &&
                        image.GetHandle().base,
                    "expected host BGR image");
            const auto& pd = prompt.GetBlobDesc();
            Require(pd.device_type == DEVICE_NAIVE && pd.dims.size() == 2 && pd.dims[1] > 0 &&
                        prompt.GetHandle().base,
                    "invalid prompt blob");
            std::string text(static_cast<const char*>(prompt.GetHandle().base), pd.dims[1]);
            auto request    = nlohmann::ordered_json::parse(text, nullptr, false);
            bool structured = request.is_object() && request.contains("laya_questions");
            nlohmann::ordered_json questions;
            if (structured)
                questions = request.at("laya_questions");
            else
                questions = nlohmann::ordered_json::array({{{"type", "noul"}, {"instructions", text}}});
            Require(questions.is_array() && !questions.empty() && questions.size() <= 16,
                    "expected 1..16 questions");
            std::vector<laya::Question> compiled;
            for (auto& question : questions) {
                if (!question.contains("question"))
                    question = nlohmann::ordered_json{{"question", question}, {"text_state", ""}};
                compiled.push_back(impl_->Compile(question));
            }
            auto pixels = laya::ImagePatches(static_cast<const uint8_t*>(image.GetHandle().base), dims[2],
                                             dims[1], size_t(dims[2]) * 3);
            auto tower  = impl_->networks[0]->Run({{"pixel_values", &pixels}});
            auto visual = impl_->networks[1]->Run({{"tower_features", &tower}});
            nlohmann::json rows = nlohmann::json::array();
            for (size_t i = 0; i < compiled.size(); ++i) {
                const auto started   = std::chrono::steady_clock::now();
                const auto& question = compiled[i];
                auto embedded        = impl_->embeddings->Splice(question, visual);
                std::vector<float> full, sliding;
                laya::AttentionMasks(question.tokens.size(), full, sliding);
                const auto& type_weights = impl_->heads.at("type_emb.weight").data;
                std::vector<float> type(type_weights.begin() + question.type * laya::kHidden,
                                        type_weights.begin() + (question.type + 1) * laya::kHidden);
                auto hidden = impl_->networks[2]->Run({{"inputs_embeds", &embedded},
                                                       {"full_attention_mask", &full},
                                                       {"sliding_attention_mask", &sliding},
                                                       {"type_embedding", &type}});
                auto scores = laya::Score(hidden, question, impl_->heads);
                rows.push_back({{"question_id", questions[i].at("question").value("id", "")},
                                {"question_version", questions[i].at("question").value("version", 1)},
                                {"status", "completed"},
                                {"elapsed_ms", std::chrono::duration<double, std::milli>(
                                                   std::chrono::steady_clock::now() - started)
                                                   .count()},
                                {"ordered_options", question.labels},
                                {"qtype", question.type},
                                {"temperature", question.temperature_info},
                                {"raw_option_logits", scores.logits},
                                {"raw_action_logits", scores.action_logits},
                                {"probabilities", scores.probabilities},
                                {"top1", question.labels.at(scores.top)},
                                {"top2_margin", scores.margin},
                                {"business_qualified", false}});
            }
            if (structured)
                outputs_.push_back(
                    {nlohmann::json{{"backend", "laya_v"}, {"status", "completed"}, {"items", rows}}.dump()});
            else
                outputs_.push_back({rows[0].at("top1") == "true" ? "是" : "否"});
        }
        return COSMO_NN_OK;
    } catch (const std::bad_alloc&) {
        outputs_.clear();
        return Status(COSMO_NN_ERR_OUT_OF_MEMORY, "Laya inference ran out of memory");
    } catch (const std::exception& error) {
        outputs_.clear();
        return Status(COSMO_NN_ERR_NET, error.what());
    }
}
}  // namespace cosmo::nn
