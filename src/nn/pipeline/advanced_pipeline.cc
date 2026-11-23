#include "nn/pipeline/advanced_pipeline.h"

#include <algorithm>
#include <fstream>
#include <nlohmann/json.hpp>

#include "nn/core/shared_resource.h"
#include "nn/pipeline/pipeline_utils.h"
#include "util/Log.h"

#ifdef COSMO_NN_USE_SOPHON_BACKEND
#include "nn/device/sophon/qwen3vl/qwen3vl_runner_factory.h"
#endif

namespace cosmo::nn {

// ========================= SAM2 ===========================================

Status SAM2Pipeline::Init(const PipelineConfig& config, const std::string& model_path, DeviceType device_type,
                          int device_id, IProfiler* profiler, const std::string& tokenizer_path,
                          const std::string& word_table_path) {
    model_info_.algorithmcode = config.algorithm_code;
    model_info_.reduce        = "sequential";
    model_info_.type          = "SAM2";

    if (config.models.size() < 2)
        return Status(COSMO_NN_ERR_PARAM, "SAM2 requires at least 2 models (encoder + decoder)");

    // --- Encoder ---
    {
        const auto& mc   = config.models[0];
        nlohmann::json p = pipeline_utils::ParseJsonObject(mc.params_json);

        ModelInfo model;
        model.name      = mc.name;
        model.filename  = mc.file_name;
        model.file_md5  = mc.file_md5;
        model.max_batch = mc.max_batch;
        max_batch_      = mc.max_batch;

        std::vector<int> enc_size = pipeline_utils::ReadIntArray(p, "input_size", {1024, 1024}, 2);
        std::vector<float> mean =
            pipeline_utils::ReadFloatArray(p, "normalize_mean", {123.675f, 116.28f, 103.53f}, 3);
        std::vector<float> std_dev =
            pipeline_utils::ReadFloatArray(p, "normalize_std", {58.395f, 57.12f, 57.375f}, 3);
        bool is_bgr = pipeline_utils::ReadBool(p, "is_bgr", false);

        std::vector<std::unique_ptr<Op>> preprocess;
        preprocess.push_back(pipeline_utils::MakeResizeOp(enc_size, 0, {0, 0, 0}));
        preprocess.push_back(pipeline_utils::MakeNormalizeOp(mean, 0.0f, is_bgr, std_dev));

        for (const auto& in_def : mc.inputs) {
            InputNodeInfo input;
            input.name      = in_def.name;
            input.shape     = in_def.shape;
            input.data_type = in_def.data_type;
            input.ops.swap(preprocess);
            model.input_node_infos.push_back(std::move(input));
        }
        for (const auto& out_def : mc.outputs) {
            OutputNodeInfo output;
            output.name      = out_def.name;
            output.shape     = out_def.shape;
            output.data_type = out_def.data_type;
            output.op        = nullptr;
            model.output_node_infos.push_back(std::move(output));
        }
        model_info_.models.push_back(std::move(model));
    }

    // --- Decoder ---
    {
        const auto& mc   = config.models[1];
        nlohmann::json p = pipeline_utils::ParseJsonObject(mc.params_json);

        ModelInfo model;
        model.name      = mc.name;
        model.filename  = mc.file_name;
        model.file_md5  = mc.file_md5;
        model.max_batch = mc.max_batch;

        std::string prompt_type = pipeline_utils::ReadString(p, "prompt_type", std::string("point"));
        bool normalize_prompt   = pipeline_utils::ReadBool(p, "normalize_prompt", true);
        int encoder_size        = pipeline_utils::ReadInt(p, "encoder_size", 1024);
        int max_points          = pipeline_utils::ReadInt(p, "max_points", 6);

        for (const auto& in_def : mc.inputs) {
            InputNodeInfo input;
            input.name      = in_def.name;
            input.shape     = in_def.shape;
            input.data_type = in_def.data_type;
            if (in_def.name == "point_coords" || in_def.name.find("prompt") != std::string::npos) {
                input.ops.push_back(pipeline_utils::MakeSAMPromptEncodeOp(prompt_type, normalize_prompt,
                                                                          encoder_size, max_points));
            }
            model.input_node_infos.push_back(std::move(input));
        }

        float sam_threshold          = pipeline_utils::ReadFloat(p, "threshold", 0.0f);
        std::vector<int> output_size = pipeline_utils::ReadIntArray(p, "output_size", {1024, 1024}, 2);

        for (const auto& out_def : mc.outputs) {
            OutputNodeInfo output;
            output.name      = out_def.name;
            output.shape     = out_def.shape;
            output.data_type = out_def.data_type;
            if (out_def.name.find("mask") != std::string::npos ||
                out_def.name.find("Clip") != std::string::npos) {
                output.op = pipeline_utils::MakeSAMDecodeOp(sam_threshold, output_size);
            }
            model.output_node_infos.push_back(std::move(output));
        }
        model_info_.models.push_back(std::move(model));
    }

    if (!config.labels.empty()) {
        pipeline_utils::BuildInstructionsFromLabels(config.labels, "masks", {-1, -1, -1}, model_info_.config);
    }

    InitThresholdsAndLabels();
    InitNetInputSize();
    return InitGraph(model_path, device_type, device_id, profiler, tokenizer_path);
}

Status SAM2Pipeline::Forward(std::initializer_list<std::vector<std::shared_ptr<Blob>>> inputs) {
    RETURN_ON_FAIL(RunGraph(inputs));

    auto iter = inputs.begin();
    image_sizes_.clear();
    for (size_t i = 0; i < iter->size(); i++) {
        auto dims   = iter->at(i)->GetBlobDesc().dims;
        auto layout = iter->at(i)->GetBlobDesc().data_format;
        Size size;
        RETURN_ON_FAIL(NetUtils::GetImageSize(dims, layout, size));
        image_sizes_.push_back(size);
    }

    if (inputs.size() == 2) {
        iter++;
        rects_.clear();
        for (size_t i = 0; i < iter->size(); i++) {
            auto blob        = iter->at(i);
            auto dims        = blob->GetBlobDesc().dims;
            const int n      = dims.at(0);
            const auto* data = static_cast<const int32_t*>(blob->GetHandle().base);
            for (int j = 0; j < n; j++) {
                Rect2i r;
                r.x      = data[j * 4];
                r.y      = data[j * 4 + 1];
                r.width  = data[j * 4 + 2];
                r.height = data[j * 4 + 3];
                rects_.push_back(r);
            }
        }
    }
    return Status{COSMO_NN_OK};
}

Status SAM2Pipeline::ParseSegmentationOutput(std::vector<std::vector<uint8_t>>& outputs) {
    outputs.clear();
    auto output_blobs = GetGraphOutput();
    if (output_blobs.size() != 1)
        return Status{COSMO_NN_OK};

    auto blob       = output_blobs.at(0);
    auto desc       = blob->GetBlobDesc();
    auto dims       = desc.dims;
    auto handle     = blob->GetHandle();
    const int batch = dims.at(0);

    if (dims.size() == 3) {
        int h = dims[1], w = dims[2];
        uint8_t* data = reinterpret_cast<uint8_t*>(handle.base);
        for (int b = 0; b < batch; b++) {
            auto origin = image_sizes_.at(b);
            std::vector<uint8_t> resized(origin.width * origin.height);
            NetUtils::MaskScale(data + b * h * w, w, h, resized.data(), origin.width, origin.height, 1);
            outputs.push_back(resized);
        }
    } else if (dims.size() == 4) {
        float* data  = reinterpret_cast<float*>(handle.base);
        size_t plane = dims[1] * dims[2] * dims[3];
        size_t hw    = dims[2] * dims[3];
        for (int b = 0; b < batch; b++) {
            float* cur = data + b * plane;
            std::vector<uint8_t> mask;
            for (size_t i = 0; i < hw; i++) {
                std::vector<float> tmp;
                for (size_t c = 0; c < static_cast<size_t>(dims[1]); c++)
                    tmp.push_back(cur[c * hw + i]);
                mask.push_back(static_cast<uint8_t>(
                    std::distance(tmp.begin(), std::max_element(tmp.begin(), tmp.end()))));
            }
            auto origin = image_sizes_.at(b);
            std::vector<uint8_t> resized(origin.width * origin.height * dims[1]);
            NetUtils::MaskScale(mask.data(), dims[3], dims[2], resized.data(), origin.width, origin.height,
                                dims[1]);
            outputs.push_back(resized);
        }
    }
    return Status{COSMO_NN_OK};
}

// ========================= GroundingDINO ==================================

Status DinoPipeline::Init(const PipelineConfig& config, const std::string& model_path, DeviceType device_type,
                          int device_id, IProfiler* profiler, const std::string& tokenizer_path,
                          const std::string& word_table_path) {
    model_info_.algorithmcode = config.algorithm_code;
    model_info_.reduce        = config.reduce;
    model_info_.type          = "dino";

    for (const auto& mc : config.models) {
        nlohmann::json p = pipeline_utils::ParseJsonObject(mc.params_json);

        ModelInfo model;
        model.name      = mc.name;
        model.filename  = mc.file_name;
        model.file_md5  = mc.file_md5;
        model.max_batch = mc.max_batch;
        max_batch_      = mc.max_batch;

        int dst_w   = pipeline_utils::ReadInt(p, "input_width", 800);
        int dst_h   = pipeline_utils::ReadInt(p, "input_height", 800);
        bool is_bgr = pipeline_utils::ReadBool(p, "is_bgr", false);
        std::vector<float> mean =
            pipeline_utils::ReadFloatArray(p, "normalize_mean", {0.485f, 0.456f, 0.406f}, 3);
        std::vector<float> std_dev =
            pipeline_utils::ReadFloatArray(p, "normalize_std", {0.229f, 0.224f, 0.225f}, 3);
        float text_threshold = pipeline_utils::ReadFloat(p, "text_threshold", 0.25f);
        float box_threshold  = pipeline_utils::ReadFloat(p, "box_threshold", 0.3f);

        bool first_input = true;
        for (const auto& in_def : mc.inputs) {
            InputNodeInfo input;
            input.name      = in_def.name;
            input.shape     = in_def.shape;
            input.data_type = in_def.data_type;
            if (first_input) {
                input.ops.push_back(pipeline_utils::MakeDinoEncoderOp(dst_w, dst_h, is_bgr, mean, std_dev));
                first_input = false;
            }
            model.input_node_infos.push_back(std::move(input));
        }
        bool first_output = true;
        for (const auto& out_def : mc.outputs) {
            OutputNodeInfo output;
            output.name      = out_def.name;
            output.shape     = out_def.shape;
            output.data_type = out_def.data_type;
            if (first_output) {
                output.op    = pipeline_utils::MakeDinoDecodeOp(text_threshold, box_threshold);
                first_output = false;
            }
            model.output_node_infos.push_back(std::move(output));
        }
        model_info_.models.push_back(std::move(model));
    }

    InitThresholdsAndLabels();
    InitNetInputSize();
    return InitGraph(model_path, device_type, device_id, profiler, tokenizer_path);
}

Status DinoPipeline::Forward(std::initializer_list<std::vector<std::shared_ptr<Blob>>> inputs) {
    RETURN_ON_FAIL(RunGraph(inputs));

    auto iter = inputs.begin();
    image_sizes_.clear();
    for (size_t i = 0; i < iter->size(); i++) {
        auto dims   = iter->at(i)->GetBlobDesc().dims;
        auto layout = iter->at(i)->GetBlobDesc().data_format;
        Size size;
        RETURN_ON_FAIL(NetUtils::GetImageSize(dims, layout, size));
        image_sizes_.push_back(size);
    }
    return Status{COSMO_NN_OK};
}

Status DinoPipeline::ParseDetectionOutput(std::vector<std::vector<ObjectInfoV1>>& outputs) {
    const auto* shared = static_cast<const SharedResource*>(GetSharedResource());
    if (!shared)
        return Status(COSMO_NN_ERR_NET, "SharedResource is null");
    return ParseDinoDetectionOutput(outputs, shared->text_threshold, shared->box_threshold);
}

Status DinoPipeline::ParseDinoDetectionOutput(std::vector<std::vector<ObjectInfoV1>>& outputs,
                                              float text_threshold, float box_threshold) {
    outputs.clear();
    auto shared = reinterpret_cast<SharedResource*>(GetSharedResource());
    if (!shared)
        return Status(COSMO_NN_ERR_NET, "SharedResource is null");
    std::vector<std::shared_ptr<Blob>> output_blobs = GetGraphOutput();
    return NetUtils::ParseDINOOutput(output_blobs, shared->tokenizer_handle, image_sizes_,
                                     shared->prompt_token_ids, text_threshold, box_threshold, outputs);
}

// ========================= Qwen3VL =======================================

Qwen3VLPipeline::~Qwen3VLPipeline() {
#ifdef COSMO_NN_USE_SOPHON_BACKEND
    if (runner_) {
        Qwen3VLRunner_Destroy(reinterpret_cast<Qwen3VLRunner*>(runner_));
        runner_ = nullptr;
    }
#endif
}

Status Qwen3VLPipeline::Init(const PipelineConfig& config, const std::string& model_path,
                             DeviceType device_type, int device_id, IProfiler* profiler,
                             const std::string& tokenizer_path, const std::string& word_table_path) {
#ifdef COSMO_NN_USE_SOPHON_BACKEND
    model_info_.type  = "qwen3vl";
    auto* qwen_runner = Qwen3VLRunner_Create();
    std::string mp    = model_path;
    std::string tp    = tokenizer_path;
    auto status       = Qwen3VLRunner_Init(qwen_runner, mp, tp, device_id, config.extra_config_json);
    if (!bool(status)) {
        Qwen3VLRunner_Destroy(qwen_runner);
        return status;
    }
    runner_ = qwen_runner;
    return Status{COSMO_NN_OK};
#else
    return Status(COSMO_NN_ERR_NET, "Qwen3VL requires COSMO_NN_USE_SOPHON_BACKEND");
#endif
}

Status Qwen3VLPipeline::Forward(std::initializer_list<std::vector<std::shared_ptr<Blob>>> inputs) {
#ifdef COSMO_NN_USE_SOPHON_BACKEND
    std::vector<std::vector<std::shared_ptr<Blob>>> inputs_vec(inputs);
    return Qwen3VLRunner_Run(reinterpret_cast<Qwen3VLRunner*>(runner_), inputs_vec);
#else
    return Status(COSMO_NN_ERR_NET, "Qwen3VL requires COSMO_NN_USE_SOPHON_BACKEND");
#endif
}

int Qwen3VLPipeline::GetMaxBatchSize() const {
#ifdef COSMO_NN_USE_SOPHON_BACKEND
    if (runner_)
        return Qwen3VLRunner_GetMaxBatchSize(reinterpret_cast<Qwen3VLRunner*>(runner_));
#endif
    return 1;
}

Status Qwen3VLPipeline::ParseTextOutput(std::vector<std::vector<std::string>>& outputs) {
#ifdef COSMO_NN_USE_SOPHON_BACKEND
    outputs = Qwen3VLRunner_GetTextOutputs(reinterpret_cast<Qwen3VLRunner*>(runner_));
    return Status{COSMO_NN_OK};
#else
    return Status(COSMO_NN_ERR_NET, "Qwen3VL requires COSMO_NN_USE_SOPHON_BACKEND");
#endif
}

// ========================= Qwen3_5 =======================================

Qwen3_5Pipeline::~Qwen3_5Pipeline() {
#ifdef COSMO_NN_USE_SOPHON_BACKEND
    if (runner_) {
        Qwen3VLRunner_Destroy(reinterpret_cast<Qwen3VLRunner*>(runner_));
        runner_ = nullptr;
    }
#endif
}

Status Qwen3_5Pipeline::Init(const PipelineConfig& config, const std::string& model_path,
                             DeviceType device_type, int device_id, IProfiler* profiler,
                             const std::string& tokenizer_path, const std::string& word_table_path) {
#ifdef COSMO_NN_USE_SOPHON_BACKEND
    model_info_.type  = "qwen3_5";
    auto* qwen_runner = Qwen3VLRunner_Create();
    std::string mp    = model_path;
    std::string tp    = tokenizer_path;
    auto status = Qwen3VLRunner_Init(qwen_runner, mp, tp, device_id, config.extra_config_json, "qwen3_5");
    if (!bool(status)) {
        Qwen3VLRunner_Destroy(qwen_runner);
        return status;
    }
    runner_ = qwen_runner;
    return Status{COSMO_NN_OK};
#else
    return Status(COSMO_NN_ERR_NET, "Qwen3_5 requires COSMO_NN_USE_SOPHON_BACKEND");
#endif
}

Status Qwen3_5Pipeline::Forward(std::initializer_list<std::vector<std::shared_ptr<Blob>>> inputs) {
#ifdef COSMO_NN_USE_SOPHON_BACKEND
    std::vector<std::vector<std::shared_ptr<Blob>>> inputs_vec(inputs);
    return Qwen3VLRunner_Run(reinterpret_cast<Qwen3VLRunner*>(runner_), inputs_vec);
#else
    return Status(COSMO_NN_ERR_NET, "Qwen3_5 requires COSMO_NN_USE_SOPHON_BACKEND");
#endif
}

int Qwen3_5Pipeline::GetMaxBatchSize() const {
#ifdef COSMO_NN_USE_SOPHON_BACKEND
    if (runner_)
        return Qwen3VLRunner_GetMaxBatchSize(reinterpret_cast<Qwen3VLRunner*>(runner_));
#endif
    return 1;
}

Status Qwen3_5Pipeline::ParseTextOutput(std::vector<std::vector<std::string>>& outputs) {
#ifdef COSMO_NN_USE_SOPHON_BACKEND
    outputs = Qwen3VLRunner_GetTextOutputs(reinterpret_cast<Qwen3VLRunner*>(runner_));
    return Status{COSMO_NN_OK};
#else
    return Status(COSMO_NN_ERR_NET, "Qwen3_5 requires COSMO_NN_USE_SOPHON_BACKEND");
#endif
}

// ========================= Segmentation ==================================

Status SegmentationPipeline::Init(const PipelineConfig& config, const std::string& model_path,
                                  DeviceType device_type, int device_id, IProfiler* profiler,
                                  const std::string& tokenizer_path, const std::string& word_table_path) {
    model_info_.algorithmcode = config.algorithm_code;
    model_info_.reduce        = config.reduce;
    model_info_.type          = "segmentation";

    for (const auto& mc : config.models) {
        nlohmann::json p = pipeline_utils::ParseJsonObject(mc.params_json);

        ModelInfo model;
        model.name      = mc.name;
        model.filename  = mc.file_name;
        model.file_md5  = mc.file_md5;
        model.max_batch = mc.max_batch;
        max_batch_      = mc.max_batch;

        std::vector<int> dsize     = pipeline_utils::ReadIntArray(p, "input_size", {512, 512}, 2);
        std::vector<float> mean    = pipeline_utils::ReadFloatArray(p, "normalize_mean", {0.f, 0.f, 0.f}, 3);
        float scale                = pipeline_utils::ReadFloat(p, "normalize_scale", 0.00392157f);
        bool is_bgr                = pipeline_utils::ReadBool(p, "is_bgr", false);
        std::vector<float> std_dev = pipeline_utils::ReadFloatArray(p, "normalize_std", {}, 3);

        std::vector<std::unique_ptr<Op>> preprocess;
        preprocess.push_back(pipeline_utils::MakeResizeOp(dsize, 0, {0, 0, 0}));
        preprocess.push_back(pipeline_utils::MakeNormalizeOp(mean, scale, is_bgr, std_dev));

        for (const auto& in_def : mc.inputs) {
            InputNodeInfo input;
            input.name      = in_def.name;
            input.shape     = in_def.shape;
            input.data_type = in_def.data_type;
            input.ops.swap(preprocess);
            model.input_node_infos.push_back(std::move(input));
        }
        for (const auto& out_def : mc.outputs) {
            OutputNodeInfo output;
            output.name      = out_def.name;
            output.shape     = out_def.shape;
            output.data_type = out_def.data_type;
            output.op        = nullptr;
            model.output_node_infos.push_back(std::move(output));
        }
        model_info_.models.push_back(std::move(model));
    }

    InitThresholdsAndLabels();
    InitNetInputSize();
    return InitGraph(model_path, device_type, device_id, profiler, tokenizer_path);
}

Status SegmentationPipeline::Forward(std::initializer_list<std::vector<std::shared_ptr<Blob>>> inputs) {
    RETURN_ON_FAIL(RunGraph(inputs));

    auto iter = inputs.begin();
    image_sizes_.clear();
    for (size_t i = 0; i < iter->size(); i++) {
        auto dims   = iter->at(i)->GetBlobDesc().dims;
        auto layout = iter->at(i)->GetBlobDesc().data_format;
        Size size;
        RETURN_ON_FAIL(NetUtils::GetImageSize(dims, layout, size));
        image_sizes_.push_back(size);
    }

    if (inputs.size() == 2) {
        iter++;
        rects_.clear();
        for (size_t i = 0; i < iter->size(); i++) {
            auto blob        = iter->at(i);
            auto dims        = blob->GetBlobDesc().dims;
            const int n      = dims.at(0);
            const auto* data = static_cast<const int32_t*>(blob->GetHandle().base);
            for (int j = 0; j < n; j++) {
                Rect2i r;
                r.x      = data[j * 4];
                r.y      = data[j * 4 + 1];
                r.width  = data[j * 4 + 2];
                r.height = data[j * 4 + 3];
                rects_.push_back(r);
            }
        }
    }
    return Status{COSMO_NN_OK};
}

Status SegmentationPipeline::ParseSegmentationOutput(std::vector<std::vector<uint8_t>>& outputs) {
    outputs.clear();
    auto output_blobs = GetGraphOutput();
    if (output_blobs.size() != 1)
        return Status{COSMO_NN_OK};

    auto blob       = output_blobs.at(0);
    auto desc       = blob->GetBlobDesc();
    auto dims       = desc.dims;
    auto handle     = blob->GetHandle();
    const int batch = dims.at(0);

    if (dims.size() == 3) {
        int h = dims[1], w = dims[2];
        uint8_t* data = reinterpret_cast<uint8_t*>(handle.base);
        for (int b = 0; b < batch; b++) {
            auto origin = image_sizes_.at(b);
            std::vector<uint8_t> resized(origin.width * origin.height);
            NetUtils::MaskScale(data + b * h * w, w, h, resized.data(), origin.width, origin.height, 1);
            outputs.push_back(resized);
        }
    } else if (dims.size() == 4) {
        float* data  = reinterpret_cast<float*>(handle.base);
        size_t plane = dims[1] * dims[2] * dims[3];
        size_t hw    = dims[2] * dims[3];
        for (int b = 0; b < batch; b++) {
            float* cur = data + b * plane;
            std::vector<uint8_t> mask;
            for (size_t i = 0; i < hw; i++) {
                std::vector<float> tmp;
                for (size_t c = 0; c < static_cast<size_t>(dims[1]); c++)
                    tmp.push_back(cur[c * hw + i]);
                mask.push_back(static_cast<uint8_t>(
                    std::distance(tmp.begin(), std::max_element(tmp.begin(), tmp.end()))));
            }
            auto origin = image_sizes_.at(b);
            std::vector<uint8_t> resized(origin.width * origin.height * dims[1]);
            NetUtils::MaskScale(mask.data(), dims[3], dims[2], resized.data(), origin.width, origin.height,
                                dims[1]);
            outputs.push_back(resized);
        }
    }
    return Status{COSMO_NN_OK};
}

// ========================= OCR ============================================

Status OcrPipeline::Init(const PipelineConfig& config, const std::string& model_path, DeviceType device_type,
                         int device_id, IProfiler* profiler, const std::string& tokenizer_path,
                         const std::string& word_table_path) {
    if (config.models.size() != 1)
        return Status(COSMO_NN_ERR_PARAM, "OCR requires exactly one model");

    model_info_.algorithmcode = config.algorithm_code;
    model_info_.reduce        = config.reduce;
    model_info_.type          = "ocr";

    for (const auto& mc : config.models) {
        nlohmann::json p = pipeline_utils::ParseJsonObject(mc.params_json);

        ModelInfo model;
        model.name      = mc.name;
        model.filename  = mc.file_name;
        model.file_md5  = mc.file_md5;
        model.max_batch = mc.max_batch;
        max_batch_      = mc.max_batch;

        std::vector<int> dsize = pipeline_utils::ReadIntArray(p, "input_size", {32, 320}, 2);
        std::vector<float> mean =
            pipeline_utils::ReadFloatArray(p, "normalize_mean", {127.5f, 127.5f, 127.5f}, 3);
        float scale = pipeline_utils::ReadFloat(p, "normalize_scale", 0.00784314f);
        bool is_bgr = pipeline_utils::ReadBool(p, "is_bgr", true);

        std::vector<std::unique_ptr<Op>> preprocess;
        preprocess.push_back(pipeline_utils::MakeResizeOp(dsize, 0, {0, 0, 0}));
        preprocess.push_back(pipeline_utils::MakeNormalizeOp(mean, scale, is_bgr));

        for (const auto& in_def : mc.inputs) {
            InputNodeInfo input;
            input.name      = in_def.name;
            input.shape     = in_def.shape;
            input.data_type = in_def.data_type;
            input.ops.swap(preprocess);
            model.input_node_infos.push_back(std::move(input));
        }
        for (const auto& out_def : mc.outputs) {
            OutputNodeInfo output;
            output.name      = out_def.name;
            output.shape     = out_def.shape;
            output.data_type = out_def.data_type;
            output.op        = nullptr;
            model.output_node_infos.push_back(std::move(output));
        }
        model_info_.models.push_back(std::move(model));
    }

    const nlohmann::json params = pipeline_utils::ParseJsonObject(config.models[0].params_json);
    ctc_config_.blank_index     = pipeline_utils::ReadInt(params, "ctc_blank_index", -1);
    ctc_config_.class_count     = pipeline_utils::ReadInt(params, "ctc_class_count", 0);
    if (ctc_config_.blank_index < 0 || ctc_config_.class_count <= 0 || word_table_path.empty())
        return Status(COSMO_NN_ERR_PARAM, "OCR CTC configuration is incomplete");

    std::ifstream ifs(word_table_path);
    if (!ifs.is_open())
        return Status(COSMO_NN_ERR_PARAM, "OCR character table cannot be opened");
    ocr_words_.clear();
    std::string line;
    while (std::getline(ifs, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        ocr_words_.push_back(line);
    }
    if (ocr_words_.empty())
        return Status(COSMO_NN_ERR_PARAM, "OCR character table is empty");

    const auto add_ctc_tokens = [&params](const char* key, bool prepend,
                                          std::vector<std::string>& words) -> Status {
        if (!params.contains(key))
            return Status{COSMO_NN_OK};
        if (!params[key].is_array())
            return Status(COSMO_NN_ERR_PARAM, "OCR CTC tokens must be an array");

        std::vector<std::string> tokens;
        for (const auto& token : params[key]) {
            if (!token.is_string())
                return Status(COSMO_NN_ERR_PARAM, "OCR CTC token must be a string");
            tokens.push_back(token.get<std::string>());
        }
        if (prepend)
            words.insert(words.begin(), tokens.begin(), tokens.end());
        else
            words.insert(words.end(), tokens.begin(), tokens.end());
        return Status{COSMO_NN_OK};
    };
    auto status = add_ctc_tokens("ctc_prepend_tokens", true, ocr_words_);
    if (!status)
        return status;
    status = add_ctc_tokens("ctc_append_tokens", false, ocr_words_);
    if (!status)
        return status;
    if (ctc_config_.blank_index >= ctc_config_.class_count ||
        ocr_words_.size() != static_cast<size_t>(ctc_config_.class_count)) {
        return Status(COSMO_NN_ERR_PARAM, "OCR character table does not match CTC classes");
    }

    InitThresholdsAndLabels();
    InitNetInputSize();
    return InitGraph(model_path, device_type, device_id, profiler, tokenizer_path);
}

Status OcrPipeline::Forward(std::initializer_list<std::vector<std::shared_ptr<Blob>>> inputs) {
    return RunGraph(inputs);
}

Status DecodeOcrCtc(const float* logits, int batch, int time, int classes,
                    const std::vector<std::string>& words, const OcrCtcConfig& config,
                    std::vector<std::vector<char>>& results) {
    results.clear();
    if (!logits || batch <= 0 || time <= 0 || classes <= 0)
        return Status(COSMO_NN_ERR_NET, "OCR output tensor dimensions must be positive");
    if (config.class_count != classes || config.blank_index < 0 || config.blank_index >= classes ||
        words.size() != static_cast<size_t>(classes)) {
        return Status(COSMO_NN_ERR_NET, "OCR character table does not match output classes");
    }

    results.resize(batch);
    for (int i = 0; i < batch; i++) {
        const float* batch_data = logits + static_cast<size_t>(i) * time * classes;
        std::vector<int> indexes;
        for (int j = 0; j < time; j++) {
            auto row   = batch_data + j * classes;
            int maxIdx = static_cast<int>(std::distance(row, std::max_element(row, row + classes)));
            indexes.push_back(maxIdx);
        }
        std::vector<int> filtered;
        int prev = -1;
        for (int idx : indexes) {
            if (idx == config.blank_index) {
                prev = config.blank_index;
                continue;
            }
            if (idx == prev)
                continue;
            prev = idx;
            filtered.push_back(idx);
        }
        std::string str;
        for (int idx : filtered) {
            str.append(words[idx]);
        }
        results[i] = std::vector<char>(str.begin(), str.end());
    }
    return Status{COSMO_NN_OK};
}

Status OcrPipeline::ParseOcrOutput(std::vector<std::vector<char>>& results) {
    results.clear();
    auto output_blobs = GetGraphOutput();
    if (output_blobs.size() != 1)
        return Status(COSMO_NN_ERR_NET, "OCR model output size should be 1");

    auto blob   = output_blobs.at(0);
    auto desc   = blob->GetBlobDesc();
    auto handle = blob->GetHandle();
    if (desc.data_type != DataType::DATA_TYPE_FLOAT || desc.dims.size() != 3 || !handle.base)
        return Status(COSMO_NN_ERR_NET, "OCR output must be a non-null float32 [batch,time,class] tensor");

    const int batch   = desc.dims.at(0);
    const int time    = desc.dims.at(1);
    const int classes = desc.dims.at(2);
    return DecodeOcrCtc(reinterpret_cast<const float*>(handle.base), batch, time, classes, ocr_words_,
                        ctc_config_, results);
}

// ===================== Auto-Registration ==================================

REGISTER_MODEL_PIPELINE("SAM2", SAM2Pipeline);
REGISTER_MODEL_PIPELINE("dino", DinoPipeline);
REGISTER_MODEL_PIPELINE("qwen3vl", Qwen3VLPipeline);
REGISTER_MODEL_PIPELINE("qwen3_5", Qwen3_5Pipeline);
REGISTER_MODEL_PIPELINE("segmentation", SegmentationPipeline);
REGISTER_MODEL_PIPELINE("ocr", OcrPipeline);

}  // namespace cosmo::nn
