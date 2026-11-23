#ifdef COSMO_NN_USE_AXERA_BACKEND
#include "nn/device/axera/axera_net_node.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <limits>
#include <new>

#include "ax_engine_api.h"
#include "ax_sys_api.h"
#include "nn/core/inference_pipeline_metrics.h"
#include "nn/node/node_type_utils.h"

namespace cosmo::nn {
namespace {
    struct EngineState {
        std::mutex mutex;
        size_t references{0};
    };

    EngineState& SharedEngine() {
        static auto* state = new EngineState;
        return *state;
    }

    Status AxeraError(const char* operation, int error) {
        return Status(COSMO_NN_ERR_NET,
                      std::string(operation) + " failed with AX error " + std::to_string(error));
    }

    size_t ElementCount(const std::vector<int>& shape) {
        if (shape.empty())
            return 0;
        size_t count = 1;
        for (const int dim : shape) {
            if (dim <= 0 || count > std::numeric_limits<size_t>::max() / static_cast<size_t>(dim))
                return 0;
            count *= static_cast<size_t>(dim);
        }
        return count;
    }

    // AX metadata strides, when present, are in elements. This initial host-copy
    // implementation accepts only dense tensors and rejects padded layouts.
    bool DenseMetadata(const AX_ENGINE_IOMETA_T& meta, size_t element_size) {
        if (!meta.pShape || meta.nShapeSize == 0 || meta.nShapeSize > 8 || element_size == 0)
            return false;
        size_t count = 1;
        for (size_t i = meta.nShapeSize; i > 0; --i) {
            const auto dim = meta.pShape[i - 1];
            if (dim <= 0 ||
                (meta.pStride &&
                 (meta.pStride[i - 1] <= 0 || static_cast<size_t>(meta.pStride[i - 1]) != count)) ||
                count > std::numeric_limits<size_t>::max() / static_cast<size_t>(dim))
                return false;
            count *= static_cast<size_t>(dim);
        }
        return count <= std::numeric_limits<size_t>::max() / element_size &&
               count * element_size == meta.nSize;
    }
}  // namespace

AxeraEngineLease::~AxeraEngineLease() {
    Reset();
}

int AxeraEngineLease::Acquire() {
    if (held_)
        return 0;
    int result = sys_.Acquire();
    if (result != 0)
        return result;
    auto& state = SharedEngine();
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        if (state.references == 0) {
            AX_ENGINE_NPU_ATTR_T attributes{};
            attributes.eHardMode = AX_ENGINE_VIRTUAL_NPU_DISABLE;
            result               = static_cast<int>(AX_ENGINE_Init(&attributes));
        }
        if (result == 0) {
            ++state.references;
            held_ = true;
        }
    }
    if (result != 0)
        sys_.Reset();
    return result;
}

void AxeraEngineLease::Reset() noexcept {
    if (!held_)
        return;
    auto& state = SharedEngine();
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        held_ = false;
        if (--state.references == 0)
            AX_ENGINE_Deinit();
    }
    sys_.Reset();
}

AxeraNetNode::AxeraNetNode() {
    name = NodeTypeUtils::NodeTypeToStr(NodeType::NODE_NET).append("_0");
}

AxeraNetNode::~AxeraNetNode() {
    std::lock_guard<std::mutex> lock(mutex_);
    ResetModel();
}

DeviceType AxeraNetNode::GetTopBlobDeviceType() {
    return DEVICE_NAIVE;
}

void AxeraNetNode::ResetModel() noexcept {
    for (const auto& buffer : input_buffers_)
        if (buffer.data)
            AX_SYS_MemFree(buffer.phy, buffer.data);
    for (const auto& buffer : output_buffers_)
        if (buffer.data)
            AX_SYS_MemFree(buffer.phy, buffer.data);
    input_buffers_.clear();
    output_buffers_.clear();
    if (handle_)
        AX_ENGINE_DestroyHandle(static_cast<AX_ENGINE_HANDLE>(handle_));
    handle_ = nullptr;
    inputs_.clear();
    outputs_.clear();
    model_data_.clear();
    engine_.Reset();
}

Status AxeraNetNode::LoadWeight(const char* data, size_t size) {
    std::lock_guard<std::mutex> lock(mutex_);
    ResetModel();
    if (!data || size == 0 || size > std::numeric_limits<AX_U32>::max())
        return Status(COSMO_NN_ERR_LOAD_MODEL, "AX model is empty or exceeds the SDK size limit");
    try {
        model_data_.assign(reinterpret_cast<const unsigned char*>(data),
                           reinterpret_cast<const unsigned char*>(data) + size);
    } catch (const std::bad_alloc&) {
        return Status(COSMO_NN_ERR_OUT_OF_MEMORY, "not enough memory to retain the AX model");
    }
    int result = engine_.Acquire();
    if (result != 0) {
        ResetModel();
        return AxeraError("AX engine initialization", result);
    }
    AX_ENGINE_HANDLE handle = nullptr;
    result = static_cast<int>(AX_ENGINE_CreateHandle(&handle, model_data_.data(), static_cast<AX_U32>(size)));
    handle_ = handle;
    if (result != 0 || !handle) {
        ResetModel();
        return AxeraError("AX_ENGINE_CreateHandle", result);
    }
    result = static_cast<int>(AX_ENGINE_CreateContext(handle));
    if (result != 0) {
        ResetModel();
        return AxeraError("AX_ENGINE_CreateContext", result);
    }
    auto status = QueryMetadata();
    if (!status) {
        ResetModel();
        return status;
    }

    auto allocate = [](std::vector<IoBuffer>& buffers, const std::vector<TensorMeta>& metadata) -> int {
        buffers.resize(metadata.size());
        for (size_t i = 0; i < metadata.size(); ++i) {
            AX_U64 phy    = 0;
            AX_VOID* virt = nullptr;
            const int rc  = static_cast<int>(
                AX_SYS_MemAlloc(&phy, &virt, metadata[i].size, 128, reinterpret_cast<const AX_S8*>("cosmo")));
            if (rc != 0)
                return rc;
            buffers[i] = {phy, virt, metadata[i].size};
            if (!virt)
                return -1;
        }
        return 0;
    };
    try {
        result = allocate(input_buffers_, inputs_);
        if (result == 0)
            result = allocate(output_buffers_, outputs_);
    } catch (const std::bad_alloc&) {
        ResetModel();
        return Status(COSMO_NN_ERR_OUT_OF_MEMORY, "AX I/O allocation bookkeeping failed");
    }
    if (result != 0) {
        ResetModel();
        return AxeraError("AX_SYS_MemAlloc", result);
    }
    return Status{COSMO_NN_OK};
}

Status AxeraNetNode::QueryMetadata() {
    AX_ENGINE_IO_INFO_T* info = nullptr;
    const int result = static_cast<int>(AX_ENGINE_GetIOInfo(static_cast<AX_ENGINE_HANDLE>(handle_), &info));
    if (result != 0 || !info)
        return AxeraError("AX_ENGINE_GetIOInfo", result);
    if (info->nInputSize != 1 || !info->pInputs || info->nOutputSize != 1 || !info->pOutputs ||
        network_input_names.size() != 1 || network_output_names.size() != 1)
        return Status(COSMO_NN_ERR_UNSUPPORT_NET,
                      "AX model/config requires exactly one input and one output");

    const auto& input      = info->pInputs[0];
    const bool float_input = input.eDataType == AX_ENGINE_DT_FLOAT32;
    const bool byte_input  = input.eDataType == AX_ENGINE_DT_UINT8;
    if ((!float_input && !byte_input) || input.nShapeSize != 4 ||
        (input.eLayout != AX_ENGINE_TENSOR_LAYOUT_NCHW && input.eLayout != AX_ENGINE_TENSOR_LAYOUT_NHWC) ||
        (byte_input &&
         (input.eLayout != AX_ENGINE_TENSOR_LAYOUT_NHWC || input_contract_ != kAxeraRgbUint8InputContract)) ||
        !DenseMetadata(input, float_input ? sizeof(float) : sizeof(uint8_t)))
        return Status(COSMO_NN_ERR_UNSUPPORT_NET,
                      "AX input type, layout, stride or declared normalization is unsupported");
    const auto& s = input.pShape;
    if (s[0] != 1 || (input.eLayout == AX_ENGINE_TENSOR_LAYOUT_NCHW ? s[1] : s[3]) != 3)
        return Status(COSMO_NN_ERR_UNSUPPORT_NET, "AX model input must be batch-1 RGB with 3 channels");
    if (input.pExtraMeta && input.pExtraMeta->eColorSpace != AX_ENGINE_CS_RGB &&
        input.pExtraMeta->eColorSpace != AX_ENGINE_CS_FEATUREMAP)
        return Status(COSMO_NN_ERR_UNSUPPORT_NET, "AX model input color space is not RGB");

    try {
        inputs_.push_back({std::vector<int>(s, s + input.nShapeSize), static_cast<int>(input.eDataType),
                           static_cast<int>(input.eLayout), input.nSize});
        for (AX_U32 i = 0; i < info->nOutputSize; ++i) {
            const auto& output = info->pOutputs[i];
            if (output.eDataType != AX_ENGINE_DT_FLOAT32 || !DenseMetadata(output, sizeof(float)))
                return Status(COSMO_NN_ERR_UNSUPPORT_NET,
                              "AX output must be dense float32; quantized outputs are unsupported");
            outputs_.push_back({std::vector<int>(output.pShape, output.pShape + output.nShapeSize),
                                static_cast<int>(output.eDataType), static_cast<int>(output.eLayout),
                                output.nSize});
        }
    } catch (const std::bad_alloc&) {
        return Status(COSMO_NN_ERR_OUT_OF_MEMORY, "AX tensor metadata allocation failed");
    }
    return Status{COSMO_NN_OK};
}

Status AxeraNetNode::InferTopShapes() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!handle_)
        return Status(COSMO_NN_ERR_GRAPH_NOT_INIT, "AX model is not initialized");
    top_blob_shapes.clear();
    top_blob_data_types.clear();
    for (const auto& meta : outputs_) {
        top_blob_shapes.push_back(meta.shape);
        top_blob_data_types.push_back(DATA_TYPE_FLOAT);
    }
    return Status{COSMO_NN_OK};
}

Status AxeraNetNode::CopyInput(Blob& blob) {
    const auto& desc  = blob.GetBlobDesc();
    const auto handle = blob.GetHandle();
    if (!handle.base || desc.data_type != DATA_TYPE_FLOAT || desc.data_format != DATA_FORMAT_NCHW ||
        desc.dims.size() != 4 || desc.dims[0] != 1 || desc.dims[1] != 3 || desc.dims[2] <= 0 ||
        desc.dims[3] <= 0)
        return Status(COSMO_NN_ERR_INVALID_INPUT, "AX graph input must be batch-1 NCHW float RGB");
    const auto& meta = inputs_[0];
    const int h      = desc.dims[2];
    const int w      = desc.dims[3];
    const bool nhwc  = meta.layout == AX_ENGINE_TENSOR_LAYOUT_NHWC;
    if ((nhwc && (meta.shape[1] != h || meta.shape[2] != w)) ||
        (!nhwc && (meta.shape[2] != h || meta.shape[3] != w)) || ElementCount(desc.dims) == 0)
        return Status(COSMO_NN_ERR_INVALID_INPUT, "AX graph input shape does not match model metadata");
    const auto* source = static_cast<const float*>(handle.base);
    const size_t plane = static_cast<size_t>(h) * static_cast<size_t>(w);
    if (meta.dtype == AX_ENGINE_DT_FLOAT32 && !nhwc) {
        std::memcpy(input_buffers_[0].data, source, meta.size);
        return Status{COSMO_NN_OK};
    }
    auto* floats = static_cast<float*>(input_buffers_[0].data);
    auto* bytes  = static_cast<uint8_t*>(input_buffers_[0].data);
    for (size_t pixel = 0; pixel < plane; ++pixel) {
        for (size_t channel = 0; channel < 3; ++channel) {
            const float value = source[channel * plane + pixel];
            const size_t dst  = pixel * 3 + channel;
            if (meta.dtype == AX_ENGINE_DT_UINT8) {
                if (!std::isfinite(value) || value < -0.00001f || value > 1.00001f)
                    return Status(COSMO_NN_ERR_INVALID_INPUT,
                                  "AX uint8 contract requires normalized [0,1] input");
                bytes[dst] = static_cast<uint8_t>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f));
            } else {
                floats[dst] = value;
            }
        }
    }
    return Status{COSMO_NN_OK};
}

Status AxeraNetNode::Forward(std::vector<std::shared_ptr<Blob>>& bottom_blobs,
                             std::vector<std::shared_ptr<Blob>>& top_blobs) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!handle_)
        return Status(COSMO_NN_ERR_GRAPH_NOT_INIT, "AX model is not initialized");
    if (bottom_blobs.size() != 1 || !bottom_blobs[0] || top_blobs.size() != outputs_.size())
        return Status(COSMO_NN_ERR_INVALID_INPUT, "AX input/output blob count mismatch");
    auto status = CopyInput(*bottom_blobs[0]);
    if (!status)
        return status;
    for (size_t i = 0; i < outputs_.size(); ++i) {
        if (!top_blobs[i] || !top_blobs[i]->GetHandle().base ||
            top_blobs[i]->GetBlobDesc().data_type != DATA_TYPE_FLOAT ||
            top_blobs[i]->GetBlobDesc().dims != outputs_[i].shape)
            return Status(COSMO_NN_ERR_INVALID_INPUT, "AX output blob does not match model metadata");
    }
    std::vector<AX_ENGINE_IO_BUFFER_T> in(input_buffers_.size());
    std::vector<AX_ENGINE_IO_BUFFER_T> out(output_buffers_.size());
    for (size_t i = 0; i < in.size(); ++i) {
        in[i].phyAddr  = input_buffers_[i].phy;
        in[i].pVirAddr = input_buffers_[i].data;
        in[i].nSize    = input_buffers_[i].size;
    }
    for (size_t i = 0; i < out.size(); ++i) {
        out[i].phyAddr  = output_buffers_[i].phy;
        out[i].pVirAddr = output_buffers_[i].data;
        out[i].nSize    = output_buffers_[i].size;
    }
    AX_ENGINE_IO_T io{};
    io.pInputs         = in.data();
    io.nInputSize      = static_cast<AX_U32>(in.size());
    io.pOutputs        = out.data();
    io.nOutputSize     = static_cast<AX_U32>(out.size());
    const auto started = std::chrono::steady_clock::now();
    const int result   = static_cast<int>(AX_ENGINE_RunSync(static_cast<AX_ENGINE_HANDLE>(handle_), &io));
    const auto elapsed = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - started)
            .count());
    GetInferencePipelineMetrics().RecordGraphForward(elapsed, 1, result == 0);
    if (result != 0)
        return AxeraError("AX_ENGINE_RunSync", result);
    for (size_t i = 0; i < outputs_.size(); ++i)
        std::memcpy(top_blobs[i]->GetHandle().base, output_buffers_[i].data, outputs_[i].size);
    return Status{COSMO_NN_OK};
}
}  // namespace cosmo::nn
#endif  // COSMO_NN_USE_AXERA_BACKEND
