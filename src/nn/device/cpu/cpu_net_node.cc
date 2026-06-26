#ifdef COSMO_NN_USE_CPU_BACKEND

#include "nn/device/cpu/cpu_net_node.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <map>

#include "nn/node/node_type_utils.h"
#include "nn/utils/data_type_utils.h"
#include "nn/utils/dims_vector_utils.h"
#include "util/Log.h"

namespace cosmo::nn {
namespace {
    size_t CountElements(const DimsVector& dims) {
        if (dims.empty())
            return 0;

        size_t count = 1;
        for (auto dim : dims) {
            if (dim <= 0)
                return 0;
            count *= static_cast<size_t>(dim);
        }
        return count;
    }

    float Float16ToFloat32(uint16_t value) {
        const uint32_t sign = (static_cast<uint32_t>(value & 0x8000)) << 16;
        uint32_t exponent   = (value >> 10) & 0x1f;
        uint32_t mantissa   = value & 0x03ff;
        uint32_t bits       = 0;

        if (exponent == 0) {
            if (mantissa == 0) {
                bits = sign;
            } else {
                exponent = 1;
                while ((mantissa & 0x0400) == 0) {
                    mantissa <<= 1;
                    exponent--;
                }
                mantissa &= 0x03ff;
                bits = sign | ((exponent + 112) << 23) | (mantissa << 13);
            }
        } else if (exponent == 0x1f) {
            bits = sign | 0x7f800000 | (mantissa << 13);
        } else {
            bits = sign | ((exponent + 112) << 23) | (mantissa << 13);
        }

        float result;
        std::memcpy(&result, &bits, sizeof(result));
        return result;
    }

    float BFloat16ToFloat32(uint16_t value) {
        const uint32_t bits = static_cast<uint32_t>(value) << 16;
        float result;
        std::memcpy(&result, &bits, sizeof(result));
        return result;
    }
}  // namespace

CpuNetNode::CpuNetNode() : NetNode() {
    name = NodeTypeUtils::NodeTypeToStr(NodeType::NODE_NET).append("_0");

    session_options_.SetIntraOpNumThreads(2);
    session_options_.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
}

CpuNetNode::~CpuNetNode() = default;

DeviceType CpuNetNode::GetTopBlobDeviceType() {
    return DeviceType::DEVICE_NAIVE;
}

size_t CpuNetNode::GetBottomCount() {
    return input_names_.empty() ? 1 : input_names_.size();
}

size_t CpuNetNode::GetTopCount() {
    return output_names_.empty() ? 1 : output_names_.size();
}

Status CpuNetNode::LoadWeight(const char* data, size_t size) {
    if (!data || size == 0)
        return Status(COSMO_NN_ERR_NET, "Empty model data");

    try {
        session_ = std::make_unique<Ort::Session>(env_, data, size, session_options_);
    } catch (const Ort::Exception& e) {
        return Status(COSMO_NN_ERR_NET, std::string("ONNX Runtime session creation failed: ") + e.what());
    }

    // Extract input info
    size_t num_inputs = session_->GetInputCount();
    input_names_.resize(num_inputs);
    input_shapes_.resize(num_inputs);
    input_types_.resize(num_inputs);

    for (size_t i = 0; i < num_inputs; i++) {
        auto name_ptr   = session_->GetInputNameAllocated(i, allocator_);
        input_names_[i] = name_ptr.get();

        auto type_info   = session_->GetInputTypeInfo(i);
        auto tensor_info = type_info.GetTensorTypeAndShapeInfo();
        input_shapes_[i] = tensor_info.GetShape();
        input_types_[i]  = tensor_info.GetElementType();

        // Replace dynamic dimensions (-1) with 1
        for (auto& dim : input_shapes_[i]) {
            if (dim < 0)
                dim = 1;
        }
    }

    // Extract output info
    size_t num_outputs = session_->GetOutputCount();
    output_names_.resize(num_outputs);
    output_shapes_.resize(num_outputs);
    output_types_.resize(num_outputs);

    for (size_t i = 0; i < num_outputs; i++) {
        auto name_ptr    = session_->GetOutputNameAllocated(i, allocator_);
        output_names_[i] = name_ptr.get();

        auto type_info    = session_->GetOutputTypeInfo(i);
        auto tensor_info  = type_info.GetTensorTypeAndShapeInfo();
        output_shapes_[i] = tensor_info.GetShape();
        output_types_[i]  = tensor_info.GetElementType();

        // Replace dynamic dimensions (-1) with 1
        for (auto& dim : output_shapes_[i]) {
            if (dim < 0)
                dim = 1;
        }
    }

    return COSMO_NN_OK;
}

Status CpuNetNode::InferTopShapes() {
    // Run a shape probe: one dummy inference with correctly-sized zero inputs
    // to resolve dynamic output dimensions (e.g., [-1, 84, -1] → [1, 84, 8400]).
    // Without this, dynamic dims default to 1, causing output blobs to be
    // allocated too small and heap corruption during the real Forward memcpy.
    if (session_ && shared_resource && shared_resource->net_input_h > 0 && shared_resource->net_input_w > 0) {
        try {
            auto mem = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);

            std::vector<Ort::Value> probe_in;
            std::vector<const char*> in_ptrs, out_ptrs;
            std::vector<std::vector<float>> bufs;  // keep data alive during Run

            for (size_t i = 0; i < input_names_.size(); i++) {
                in_ptrs.push_back(input_names_[i].c_str());

                // Build correct input shape: use actual spatial dims from pipeline
                auto shape = input_shapes_[i];
                if (shape.size() == 4) {
                    shape[0] = 1;
                    shape[2] = shared_resource->net_input_h;
                    shape[3] = shared_resource->net_input_w;
                }

                size_t elem = 1;
                for (auto d : shape)
                    elem *= static_cast<size_t>(d > 0 ? d : 1);

                bufs.emplace_back(elem, 0.0f);
                probe_in.emplace_back(Ort::Value::CreateTensor<float>(mem, bufs.back().data(), elem,
                                                                      shape.data(), shape.size()));
            }

            for (auto& n : output_names_)
                out_ptrs.push_back(n.c_str());

            auto probe_out = session_->Run(Ort::RunOptions{nullptr}, in_ptrs.data(), probe_in.data(),
                                           probe_in.size(), out_ptrs.data(), out_ptrs.size());

            // Update stored output shapes with actual resolved values
            for (size_t i = 0; i < probe_out.size() && i < output_shapes_.size(); i++)
                output_shapes_[i] = probe_out[i].GetTensorTypeAndShapeInfo().GetShape();
        } catch (...) {
            // Probe failed; fall through with original shapes (best-effort)
        }
    }

    // Build top_blob_shapes from (now resolved) output_shapes_
    top_blob_shapes.clear();
    top_blob_data_types.clear();

    for (size_t i = 0; i < output_shapes_.size(); i++) {
        DimsVector dims;
        for (auto d : output_shapes_[i])
            dims.push_back(static_cast<int>(d));
        top_blob_shapes.push_back(dims);

        DataType dt = DATA_TYPE_FLOAT;
        if (output_types_[i] == ONNX_TENSOR_ELEMENT_DATA_TYPE_INT32)
            dt = DATA_TYPE_INT32;
        else if (output_types_[i] == ONNX_TENSOR_ELEMENT_DATA_TYPE_INT8)
            dt = DATA_TYPE_INT8;
        else if (output_types_[i] == ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT8)
            dt = DATA_TYPE_UINT8;
        top_blob_data_types.push_back(dt);
    }
    return COSMO_NN_OK;
}

Status CpuNetNode::Forward(std::vector<std::shared_ptr<Blob>>& bottom_blobs,
                           std::vector<std::shared_ptr<Blob>>& top_blobs) {
    if (!session_)
        return Status(COSMO_NN_ERR_NET, "ONNX session not loaded");

    timer.Start();

    auto memory_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);

    // Prepare input tensors
    std::vector<Ort::Value> input_tensors;
    std::vector<const char*> input_name_ptrs;
    std::map<std::string, std::shared_ptr<Blob>> graph_input_blobs;
    for (size_t i = 0; i < network_input_names.size() && i < bottom_blobs.size(); i++) {
        graph_input_blobs[network_input_names[i]] = bottom_blobs[i];
    }

    for (size_t i = 0; i < input_names_.size(); i++) {
        input_name_ptrs.push_back(input_names_[i].c_str());

        auto iter  = graph_input_blobs.find(input_names_[i]);
        auto blob  = (iter != graph_input_blobs.end()) ? iter->second : bottom_blobs.at(i);
        auto desc  = blob->GetBlobDesc();
        void* data = blob->GetHandle().base;

        // Use actual blob dimensions for the tensor shape.
        // input_shapes_ may have had dynamic dims (-1) replaced with 1 during LoadWeight,
        // but the real spatial dims come from the preprocess pipeline (e.g., 640x640).
        std::vector<int64_t> shape;
        for (auto d : desc.dims)
            shape.push_back(static_cast<int64_t>(d));

        size_t element_count = 1;
        for (auto d : shape)
            element_count *= static_cast<size_t>(d);

        if (input_types_[i] == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) {
            input_tensors.emplace_back(Ort::Value::CreateTensor<float>(
                memory_info, static_cast<float*>(data), element_count, shape.data(), shape.size()));
            // Diagnostic: print input data statistics
            auto* fdata = static_cast<float*>(data);
            float fmin = fdata[0], fmax = fdata[0], fsum = 0;
            for (size_t k = 0; k < std::min(element_count, (size_t)100000); k++) {
                if (fdata[k] < fmin)
                    fmin = fdata[k];
                if (fdata[k] > fmax)
                    fmax = fdata[k];
                fsum += fdata[k];
            }
            size_t sample = std::min(element_count, (size_t)100000);
            LOG_DEBUG(
                "[CpuNetNode] Input[{}:{}] shape=[{},{},{},{}] min={} max={} mean={} first5=[{},{},{},{},{}]",
                i, input_names_[i], shape.size() > 0 ? shape[0] : 0, shape.size() > 1 ? shape[1] : 0,
                shape.size() > 2 ? shape[2] : 0, shape.size() > 3 ? shape[3] : 0, fmin, fmax, fsum / sample,
                element_count > 0 ? fdata[0] : 0, element_count > 1 ? fdata[1] : 0,
                element_count > 2 ? fdata[2] : 0, element_count > 3 ? fdata[3] : 0,
                element_count > 4 ? fdata[4] : 0);
        } else if (input_types_[i] == ONNX_TENSOR_ELEMENT_DATA_TYPE_INT32) {
            input_tensors.emplace_back(Ort::Value::CreateTensor<int32_t>(
                memory_info, static_cast<int32_t*>(data), element_count, shape.data(), shape.size()));
        } else if (input_types_[i] == ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT8) {
            input_tensors.emplace_back(Ort::Value::CreateTensor<uint8_t>(
                memory_info, static_cast<uint8_t*>(data), element_count, shape.data(), shape.size()));
        } else {
            return Status(COSMO_NN_ERR_NET, "Unsupported input data type");
        }
    }

    // Prepare output names
    std::vector<const char*> output_name_ptrs;
    for (auto& name : output_names_)
        output_name_ptrs.push_back(name.c_str());

    // Run inference
    std::vector<Ort::Value> output_tensors;
    try {
        output_tensors =
            session_->Run(Ort::RunOptions{nullptr}, input_name_ptrs.data(), input_tensors.data(),
                          input_tensors.size(), output_name_ptrs.data(), output_name_ptrs.size());
    } catch (const Ort::Exception& e) {
        return Status(COSMO_NN_ERR_NET, std::string("ONNX Runtime inference failed: ") + e.what());
    }

    std::map<std::string, std::shared_ptr<Blob>> graph_output_blobs;
    for (size_t i = 0; i < network_output_names.size() && i < top_blobs.size(); i++) {
        graph_output_blobs[network_output_names[i]] = top_blobs[i];
    }

    // Copy output data to graph blobs by output name. ONNX Runtime may expose
    // outputs in a different order than config.json.
    for (size_t i = 0; i < output_tensors.size(); i++) {
        auto& tensor  = output_tensors[i];
        auto iter     = graph_output_blobs.find(output_names_[i]);
        auto top_blob = (iter != graph_output_blobs.end()) ? iter->second
                                                           : (i < top_blobs.size() ? top_blobs[i] : nullptr);
        if (!top_blob)
            continue;

        auto tensor_info = tensor.GetTensorTypeAndShapeInfo();
        auto shape       = tensor_info.GetShape();
        size_t count     = tensor_info.GetElementCount();

        const auto data_type = tensor_info.GetElementType();

        // Update top blob shape with actual output shape. Dynamic ONNX outputs
        // may be larger than the best-effort shape inferred during Init.
        auto top_desc          = top_blob->GetBlobDesc();
        const size_t old_count = CountElements(top_desc.dims);
        DimsVector dims;
        for (auto d : shape)
            dims.push_back(static_cast<int>(d));
        top_desc.dims = dims;
        if (data_type == ONNX_TENSOR_ELEMENT_DATA_TYPE_INT32)
            top_desc.data_type = DATA_TYPE_INT32;
        else if (data_type == ONNX_TENSOR_ELEMENT_DATA_TYPE_INT8)
            top_desc.data_type = DATA_TYPE_INT8;
        else if (data_type == ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT8)
            top_desc.data_type = DATA_TYPE_UINT8;
        else
            top_desc.data_type = DATA_TYPE_FLOAT;
        top_blob->SetBlobDesc(top_desc);

        size_t nbytes = count * DataTypeUtils::GetBytesSize(top_desc.data_type);
        auto handle   = top_blob->GetHandle();
        if (count > old_count || !handle.base) {
            if (handle.base)
                free(handle.base);
            handle.base = malloc(nbytes);
            if (!handle.base)
                return Status(COSMO_NN_ERR_NET, "Failed to allocate CPU output blob");
            handle.phy = 0;
            top_blob->SetHandle(handle);
        }

        // Copy data
        void* dst = top_blob->GetHandle().base;
        if (data_type == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) {
            std::memcpy(dst, tensor.GetTensorData<float>(), nbytes);
        } else if (data_type == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT16) {
            auto* src = tensor.GetTensorData<uint16_t>();
            auto* out = static_cast<float*>(dst);
            for (size_t j = 0; j < count; j++)
                out[j] = Float16ToFloat32(src[j]);
        } else if (data_type == ONNX_TENSOR_ELEMENT_DATA_TYPE_BFLOAT16) {
            auto* src = tensor.GetTensorData<uint16_t>();
            auto* out = static_cast<float*>(dst);
            for (size_t j = 0; j < count; j++)
                out[j] = BFloat16ToFloat32(src[j]);
        } else if (data_type == ONNX_TENSOR_ELEMENT_DATA_TYPE_INT32) {
            std::memcpy(dst, tensor.GetTensorData<int32_t>(), nbytes);
        } else {
            std::memcpy(dst, tensor.GetTensorData<void>(), nbytes);
        }
    }

    timer.Stop();
    return COSMO_NN_OK;
}

}  // namespace cosmo::nn

#endif  // COSMO_NN_USE_CPU_BACKEND
