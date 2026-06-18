#pragma once

#ifdef COSMO_NN_USE_CPU_BACKEND

#include <memory>
#include <string>
#include <vector>

#include "nn/node/net_node.h"
#include "onnxruntime_cxx_api.h"

namespace cosmo::nn {

/**
 * @brief CPU inference node using ONNX Runtime.
 *
 * Loads an ONNX model from memory and runs inference on CPU.
 * Input/output blobs are in host memory (DEVICE_NAIVE).
 */
class CpuNetNode : public NetNode {
public:
    CpuNetNode();
    ~CpuNetNode() override;

    CpuNetNode(const CpuNetNode&)            = delete;
    CpuNetNode& operator=(const CpuNetNode&) = delete;

    DeviceType GetTopBlobDeviceType() override;
    Status InferTopShapes() override;
    Status LoadWeight(const char* data, size_t size) override;
    size_t GetBottomCount() override;
    size_t GetTopCount() override;

    Status Forward(std::vector<std::shared_ptr<Blob>>& bottom_blobs,
                   std::vector<std::shared_ptr<Blob>>& top_blobs) override;

private:
    Ort::Env env_{ORT_LOGGING_LEVEL_WARNING, "CpuNetNode"};
    std::unique_ptr<Ort::Session> session_;
    Ort::SessionOptions session_options_;
    Ort::AllocatorWithDefaultOptions allocator_;

    std::vector<std::string> input_names_;
    std::vector<std::string> output_names_;
    std::vector<std::vector<int64_t>> input_shapes_;
    std::vector<std::vector<int64_t>> output_shapes_;
    std::vector<ONNXTensorElementDataType> input_types_;
    std::vector<ONNXTensorElementDataType> output_types_;
};

}  // namespace cosmo::nn

#endif  // COSMO_NN_USE_CPU_BACKEND
