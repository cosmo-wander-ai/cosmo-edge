// BmodelTool — BmodelTool — Utility for retrieving bmodel info and converting to nn format.

#include "infer/BmodelTool.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>

#include "nn/utils/model_header_info.h"
#include "util/Log.h"

#if defined(__linux__) || defined(__ANDROID__)
#include <sys/stat.h>
#endif

#ifdef COSMO_NN_USE_SOPHON_BACKEND
#include "bmlib_runtime.h"
#include "bmruntime_interface.h"
#endif

#ifdef COSMO_NN_USE_ONNX_BACKEND
#include "onnxruntime_cxx_api.h"
#endif
#ifdef COSMO_NN_USE_RKNN_BACKEND
#include "nn/device/rknn/rknn_yolov8_adapter.h"
#include "rknn_api.h"
#endif

namespace fs = std::filesystem;

namespace cosmo {

int BmodelTool::ConvertDataType(int bmDataType) {
    switch (bmDataType) {
        case 0:
            return 0;  // BM_FLOAT32 -> 0
        case 1:
            return 1;  // BM_FLOAT16 -> 1
        case 3:
            return 3;  // BM_INT8 -> 3
        case 4:
            return 4;  // BM_INT32 -> 4
        case 5:
            return 5;  // BM_UINT8 -> 5
        default:
            return 0;
    }
}

static std::string ShapeToString(const std::vector<int>& shape) {
    std::ostringstream oss;
    oss << "[";
    for (size_t i = 0; i < shape.size(); i++) {
        if (i > 0)
            oss << ",";
        oss << shape[i];
    }
    oss << "]";
    return oss.str();
}

void BmodelTool::LogBmodelInfo(const BmodelInfo& info, const std::string& logPrefix) {
    LOG_INFO("{} filePath={} valid={}", logPrefix, info.file_path, info.valid);
    if (!info.valid) {
        if (!info.error_msg.empty())
            LOG_INFO("{} errorMsg={}", logPrefix, info.error_msg);
        return;
    }
    for (size_t n = 0; n < info.networks.size(); n++) {
        const auto& net = info.networks[n];
        LOG_INFO("{} network[{}] name={} max_batch={}", logPrefix, n, net.name, net.max_batch);
        for (size_t i = 0; i < net.inputs.size(); i++) {
            const auto& node = net.inputs[i];
            LOG_INFO("{}   input[{}] name={} shape={} data_type={}", logPrefix, i, node.name,
                     ShapeToString(node.shape), node.data_type);
        }
        for (size_t i = 0; i < net.outputs.size(); i++) {
            const auto& node = net.outputs[i];
            LOG_INFO("{}   output[{}] name={} shape={} data_type={}", logPrefix, i, node.name,
                     ShapeToString(node.shape), node.data_type);
        }
    }
}

#ifdef COSMO_NN_USE_SOPHON_BACKEND
BmodelInfo BmodelTool::GetBmodelInfo(const std::string& bmodelPath) {
    BmodelInfo info;
    info.file_path = bmodelPath;

    if (!fs::exists(bmodelPath)) {
        info.valid     = false;
        info.error_msg = "File does not exist: " + bmodelPath;
        LOG_WARN("[BmodelTool] {}", info.error_msg);
        return info;
    }

    bm_handle_t handle;
    bm_status_t ret = bm_dev_request(&handle, 0);
    if (ret != BM_SUCCESS) {
        info.valid     = false;
        info.error_msg = "Cannot request device (device may not exist or is in use)";
        LOG_WARN("[BmodelTool] {}", info.error_msg);
        return info;
    }

    void* context = bmrt_create(handle);
    if (!context) {
        bm_dev_free(handle);
        info.valid     = false;
        info.error_msg = "Cannot create BMRT context";
        LOG_WARN("[BmodelTool] {}", info.error_msg);
        return info;
    }

    bool loaded = bmrt_load_bmodel(context, bmodelPath.c_str());
    if (!loaded) {
        bmrt_destroy(context);
        bm_dev_free(handle);
        info.valid     = false;
        info.error_msg = "Cannot load bmodel file: " + bmodelPath;
        LOG_WARN("[BmodelTool] {}", info.error_msg);
        return info;
    }

    int net_count = bmrt_get_network_number(context);
    const char** net_names;
    bmrt_get_network_names(context, &net_names);

    for (int net_idx = 0; net_idx < net_count; net_idx++) {
        const char* net_name          = net_names[net_idx];
        const bm_net_info_t* net_info = bmrt_get_network_info(context, net_name);

        if (!net_info)
            continue;

        BmodelNetworkInfo network_info;
        network_info.name = net_name;

        if (net_info->stage_num > 0 && net_info->input_num > 0) {
            bm_shape_t shape = net_info->stages[0].input_shapes[0];
            if (shape.num_dims > 0) {
                network_info.max_batch = shape.dims[0];
            }
        }

        for (int i = 0; i < net_info->input_num; i++) {
            BmodelNodeInfo node_info;
            if (net_info->input_names && net_info->input_names[i]) {
                node_info.name = net_info->input_names[i];
            } else {
                node_info.name = "input_" + std::to_string(i);
            }
            if (net_info->stage_num > 0) {
                bm_shape_t shape = net_info->stages[0].input_shapes[i];
                for (int dim = 0; dim < shape.num_dims; dim++) {
                    node_info.shape.push_back(shape.dims[dim]);
                }
            }
            node_info.data_type = ConvertDataType(net_info->input_dtypes[i]);
            network_info.inputs.push_back(node_info);
        }

        for (int i = 0; i < net_info->output_num; i++) {
            BmodelNodeInfo node_info;
            if (net_info->output_names && net_info->output_names[i]) {
                node_info.name = net_info->output_names[i];
            } else {
                node_info.name = "output_" + std::to_string(i);
            }
            if (net_info->stage_num > 0) {
                bm_shape_t shape = net_info->stages[0].output_shapes[i];
                for (int dim = 0; dim < shape.num_dims; dim++) {
                    node_info.shape.push_back(shape.dims[dim]);
                }
            }
            node_info.data_type = ConvertDataType(net_info->output_dtypes[i]);
            network_info.outputs.push_back(node_info);
        }

        info.networks.push_back(network_info);
    }

    bmrt_destroy(context);
    bm_dev_free(handle);

    info.valid = true;
    LOG_INFO("[BmodelTool] Successfully got bmodel info: {} networks", info.networks.size());
    LogBmodelInfo(info, "[BmodelTool]");

    return info;
}
#elif defined(COSMO_NN_USE_RKNN_BACKEND)

namespace {
    class RknnContextGuard {
    public:
        ~RknnContextGuard() {
            if (context != 0)
                rknn_destroy(context);
        }
        rknn_context context{0};
    };

    std::vector<int> RknnShape(const rknn_tensor_attr& attr) {
        std::vector<int> shape;
        shape.reserve(attr.n_dims);
        for (uint32_t index = 0; index < attr.n_dims; ++index) {
            if (attr.dims[index] > static_cast<uint32_t>(std::numeric_limits<int>::max()))
                return {};
            shape.push_back(static_cast<int>(attr.dims[index]));
        }
        return shape;
    }

    std::vector<int> RknnInputShapeNchw(const rknn_tensor_attr& attr) {
        auto shape = RknnShape(attr);
        if (shape.size() == 4 && attr.fmt == RKNN_TENSOR_NHWC)
            return {shape[0], shape[3], shape[1], shape[2]};
        return shape;
    }
}  // namespace

BmodelInfo BmodelTool::GetBmodelInfo(const std::string& bmodelPath) {
    BmodelInfo info;
    info.file_path = bmodelPath;
    std::error_code ec;
    const auto file_size = fs::file_size(bmodelPath, ec);
    if (ec || file_size == 0 || file_size > std::numeric_limits<uint32_t>::max()) {
        info.error_msg = ec ? "File does not exist or is inaccessible: " + bmodelPath
                            : "RKNN model file has an invalid size";
        return info;
    }

    std::vector<unsigned char> model(static_cast<size_t>(file_size));
    std::ifstream stream(bmodelPath, std::ios::binary);
    if (!stream.read(reinterpret_cast<char*>(model.data()), static_cast<std::streamsize>(model.size()))) {
        info.error_msg = "Failed to read RKNN model file";
        return info;
    }

    RknnContextGuard guard;
    int result = rknn_init(&guard.context, model.data(), static_cast<uint32_t>(model.size()), 0, nullptr);
    if (result != RKNN_SUCC) {
        info.error_msg = "rknn_init failed with code " + std::to_string(result);
        return info;
    }
    rknn_input_output_num count{};
    result = rknn_query(guard.context, RKNN_QUERY_IN_OUT_NUM, &count, sizeof(count));
    if (result != RKNN_SUCC || count.n_input == 0 || count.n_output == 0 || count.n_output > 64) {
        info.error_msg = "Failed to query RKNN input/output count";
        return info;
    }

    BmodelNetworkInfo network;
    network.name = "rknn_network";
    for (uint32_t index = 0; index < count.n_input; ++index) {
        rknn_tensor_attr attr{};
        attr.index = index;
        result     = rknn_query(guard.context, RKNN_QUERY_INPUT_ATTR, &attr, sizeof(attr));
        if (result != RKNN_SUCC) {
            info.error_msg = "Failed to query RKNN input attributes";
            return info;
        }
        BmodelNodeInfo node;
        node.name      = attr.name[0] ? attr.name : "input_" + std::to_string(index);
        node.shape     = RknnInputShapeNchw(attr);
        node.data_type = 0;  // Cosmo host boundary supplies float NCHW.
        if (node.shape.empty()) {
            info.error_msg = "RKNN input shape cannot be represented";
            return info;
        }
        network.inputs.push_back(std::move(node));
    }
    if (!network.inputs.empty() && !network.inputs[0].shape.empty())
        network.max_batch = network.inputs[0].shape[0];

    std::vector<rknn_tensor_attr> output_attrs(count.n_output);
    std::vector<std::vector<int>> output_shapes;
    for (uint32_t index = 0; index < count.n_output; ++index) {
        output_attrs[index].index = index;
        result                    = rknn_query(guard.context, RKNN_QUERY_OUTPUT_ATTR, &output_attrs[index],
                                               sizeof(output_attrs[index]));
        if (result != RKNN_SUCC) {
            info.error_msg = "Failed to query RKNN output attributes";
            return info;
        }
        auto shape = RknnShape(output_attrs[index]);
        if (shape.empty()) {
            info.error_msg = "RKNN output shape cannot be represented";
            return info;
        }
        output_shapes.push_back(std::move(shape));
    }

    cosmo::nn::RknnOutputAdapterContract output_adapter;
    std::string adapter_error;
    if (!cosmo::nn::ResolveRknnOutputAdapter(output_shapes, output_adapter, adapter_error)) {
        info.error_msg = adapter_error;
        return info;
    }
    if (cosmo::nn::IsRknnYolov8DflAdapter(output_adapter.kind)) {
        BmodelNodeInfo node;
        node.name      = "output0";
        node.shape     = output_adapter.logical_shape;
        node.data_type = 0;
        network.outputs.push_back(std::move(node));
    } else {
        for (uint32_t index = 0; index < count.n_output; ++index) {
            BmodelNodeInfo node;
            node.name =
                output_attrs[index].name[0] ? output_attrs[index].name : "output_" + std::to_string(index);
            node.shape     = std::move(output_shapes[index]);
            node.data_type = 0;  // rknn_outputs_get(want_float=1)
            network.outputs.push_back(std::move(node));
        }
    }

    info.networks.push_back(std::move(network));
    info.valid = true;
    LogBmodelInfo(info, "[BmodelTool][RKNN]");
    return info;
}

#elif defined(COSMO_NN_USE_ONNX_BACKEND)

static int ConvertOnnxDataType(ONNXTensorElementDataType onnxType) {
    switch (onnxType) {
        case ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT:
            return 0;  // FLOAT32
        case ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT16:
            return 1;  // FLOAT16
        case ONNX_TENSOR_ELEMENT_DATA_TYPE_INT8:
            return 3;  // INT8
        case ONNX_TENSOR_ELEMENT_DATA_TYPE_INT32:
            return 4;  // INT32
        case ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT8:
            return 5;  // UINT8
        default:
            return 0;  // default to FLOAT32
    }
}

BmodelInfo BmodelTool::GetBmodelInfo(const std::string& bmodelPath) {
    BmodelInfo info;
    info.file_path = bmodelPath;

    if (!fs::exists(bmodelPath)) {
        info.valid     = false;
        info.error_msg = "File does not exist: " + bmodelPath;
        LOG_WARN("[BmodelTool] {}", info.error_msg);
        return info;
    }

    try {
        Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "BmodelTool");
        Ort::SessionOptions opts;
        opts.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_DISABLE_ALL);

        Ort::Session session(env, bmodelPath.c_str(), opts);
        Ort::AllocatorWithDefaultOptions allocator;

        BmodelNetworkInfo network;
        network.name = "onnx_network";

        // Read inputs
        size_t numInputs = session.GetInputCount();
        for (size_t i = 0; i < numInputs; i++) {
            BmodelNodeInfo nodeInfo;
            auto namePtr  = session.GetInputNameAllocated(i, allocator);
            nodeInfo.name = namePtr.get();

            auto typeInfo   = session.GetInputTypeInfo(i);
            auto tensorInfo = typeInfo.GetTensorTypeAndShapeInfo();
            auto shape      = tensorInfo.GetShape();
            auto elemType   = tensorInfo.GetElementType();

            for (auto dim : shape) {
                nodeInfo.shape.push_back(static_cast<int>(dim));
            }
            nodeInfo.data_type = ConvertOnnxDataType(elemType);
            network.inputs.push_back(nodeInfo);
        }

        // Infer max_batch from first input shape[0] (treat dynamic -1 as 1)
        if (!network.inputs.empty() && !network.inputs[0].shape.empty()) {
            network.max_batch = network.inputs[0].shape[0] > 0 ? network.inputs[0].shape[0] : 1;
        }

        // Read outputs
        size_t numOutputs = session.GetOutputCount();
        for (size_t i = 0; i < numOutputs; i++) {
            BmodelNodeInfo nodeInfo;
            auto namePtr  = session.GetOutputNameAllocated(i, allocator);
            nodeInfo.name = namePtr.get();

            auto typeInfo   = session.GetOutputTypeInfo(i);
            auto tensorInfo = typeInfo.GetTensorTypeAndShapeInfo();
            auto shape      = tensorInfo.GetShape();
            auto elemType   = tensorInfo.GetElementType();

            for (auto dim : shape) {
                nodeInfo.shape.push_back(static_cast<int>(dim));
            }
            nodeInfo.data_type = ConvertOnnxDataType(elemType);
            network.outputs.push_back(nodeInfo);
        }

        // Read class names from ONNX metadata when present (Ultralytics
        // exports store them under the "names" key, e.g. "{0: 'z', 1: 't'}").
        try {
            Ort::ModelMetadata metadata = session.GetModelMetadata();
            auto namesPtr               = metadata.LookupCustomMetadataMapAllocated("names", allocator);
            if (namesPtr) {
                info.class_names_raw = namesPtr.get();
                LOG_INFO("[BmodelTool] ONNX metadata names: {}", info.class_names_raw);
            }
        } catch (const std::exception& e) {
            LOG_WARN("[BmodelTool] Failed to read ONNX metadata names: {}", e.what());
        }

        info.networks.push_back(network);
        info.valid = true;
        LOG_INFO("[BmodelTool] Successfully got ONNX model info: {} inputs, {} outputs",
                 network.inputs.size(), network.outputs.size());
        LogBmodelInfo(info, "[BmodelTool]");

    } catch (const Ort::Exception& e) {
        info.valid     = false;
        info.error_msg = std::string("Failed to read ONNX model: ") + e.what();
        LOG_WARN("[BmodelTool] {}", info.error_msg);
    } catch (const std::exception& e) {
        info.valid     = false;
        info.error_msg = std::string("Failed to read ONNX model: ") + e.what();
        LOG_WARN("[BmodelTool] {}", info.error_msg);
    }

    return info;
}

#else
BmodelInfo BmodelTool::GetBmodelInfo(const std::string& bmodelPath) {
    BmodelInfo info;
    info.file_path = bmodelPath;
    info.valid     = false;
    info.error_msg = "SDK_NOT_AVAILABLE";
    LOG_INFO("{}", "[BmodelTool] No inference backend enabled, returning SDK_NOT_AVAILABLE");
    return info;
}
#endif  // selected inference backend

std::string BmodelTool::ConvertToNn(const std::vector<std::string>& bmodelPaths,
                                    const std::string& outputPath) {
    if (bmodelPaths.empty()) {
        return "No bmodel files provided";
    }
    if (bmodelPaths.size() > cosmo::nn::kPlainNnMaxModelCount) {
        return "Too many bmodel files for nn container";
    }

    std::vector<uint64_t> fileSizes;
    for (const auto& path : bmodelPaths) {
        std::error_code equivalentError;
        if (fs::equivalent(outputPath, path, equivalentError)) {
            return "Output nn file must be different from input bmodel file: " + path;
        }
        std::error_code ec;
        auto size = fs::file_size(path, ec);
        if (ec) {
            return "File does not exist or is inaccessible: " + path;
        }
        if (size == 0) {
            return "Bmodel file is empty: " + path;
        }
        if (size > static_cast<std::uintmax_t>(std::numeric_limits<uint64_t>::max())) {
            return "Bmodel file is too large: " + path;
        }
        fileSizes.push_back(static_cast<uint64_t>(size));
        LOG_INFO("[BmodelTool] bmodel file: {}, size: {} bytes", path, size);
    }

    auto headerResult = cosmo::nn::BuildPlainNnHeader(fileSizes);
    if (!headerResult) {
        return "Failed to build nn header: " + headerResult.error;
    }

    std::ofstream outputFile(outputPath, std::ios::binary);
    if (!outputFile) {
        return "Cannot create output file: " + outputPath;
    }

    outputFile.write(headerResult.data.data(), static_cast<std::streamsize>(headerResult.data.size()));

    // Chunked copy: do not allocate vector<char>(fileSize) at once, otherwise GB-scale bmodel will OOM
    // or only write out the header (model.nn only a few hundred bytes)
    constexpr std::streamsize kCopyChunk = 4 * 1024 * 1024;
    std::vector<char> copyBuf(static_cast<size_t>(kCopyChunk));

    for (size_t idx = 0; idx < bmodelPaths.size(); ++idx) {
        const std::string& bmodelPath = bmodelPaths[idx];

        std::ifstream inputFile(bmodelPath, std::ios::binary);
        if (inputFile.fail()) {
            outputFile.close();
            fs::remove(outputPath);
            return "Cannot open bmodel file: " + bmodelPath;
        }

        uint64_t remaining = fileSizes[idx];
        while (remaining > 0) {
            const uint64_t copyBytes = std::min<uint64_t>(remaining, static_cast<uint64_t>(kCopyChunk));
            const std::streamsize n  = static_cast<std::streamsize>(copyBytes);
            if (!inputFile.read(copyBuf.data(), n)) {
                outputFile.close();
                fs::remove(outputPath);
                return "Failed to read bmodel file: " + bmodelPath;
            }
            outputFile.write(copyBuf.data(), n);
            if (outputFile.fail()) {
                inputFile.close();
                outputFile.close();
                fs::remove(outputPath);
                return "Failed to write nn file: " + outputPath;
            }
            remaining -= copyBytes;
        }
        inputFile.close();
    }

    if (outputFile.fail()) {
        outputFile.close();
        fs::remove(outputPath);
        return "Failed to write nn file";
    }
    outputFile.close();

    if (!fs::exists(outputPath)) {
        return "nn file does not exist after creation";
    }

    auto outputSize       = fs::file_size(outputPath);
    uint64_t expectedSize = cosmo::nn::kPlainNnHeaderSize;
    for (uint64_t fileSize : fileSizes) {
        if (fileSize > std::numeric_limits<uint64_t>::max() - expectedSize) {
            fs::remove(outputPath);
            return "nn file size overflow";
        }
        expectedSize += fileSize;
    }
    if (outputSize != static_cast<std::uintmax_t>(expectedSize)) {
        fs::remove(outputPath);
        return "nn file size is incorrect";
    }

    LOG_INFO("[BmodelTool] Successfully converted to nn: {}, size: {} bytes", outputPath, outputSize);
    return "";
}

void BmodelTool::CleanupTempFiles(const std::vector<std::string>& filePaths) {
    for (const auto& path : filePaths) {
        try {
            if (fs::exists(path)) {
                fs::remove(path);
                LOG_INFO("[BmodelTool] Cleaned up temp file: {}", path);
            }
        } catch (const std::exception& e) {
            LOG_WARN("[BmodelTool] Failed to cleanup temp file {}: {}", path, e.what());
        }
    }
}

}  // namespace cosmo
