#pragma once

#ifdef COSMO_NN_USE_AXERA_BACKEND
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "nn/core/model_input_contracts.h"
#include "nn/device/axera/axera_sys.h"
#include "nn/node/net_node.h"

namespace cosmo::nn {

class AxeraEngineLease {
public:
    AxeraEngineLease() = default;
    ~AxeraEngineLease();
    AxeraEngineLease(const AxeraEngineLease&)            = delete;
    AxeraEngineLease& operator=(const AxeraEngineLease&) = delete;
    int Acquire();
    void Reset() noexcept;

private:
    AxeraSysLease sys_;
    bool held_{false};
};

class AxeraNetNode final : public NetNode {
public:
    AxeraNetNode();
    ~AxeraNetNode() override;
    AxeraNetNode(const AxeraNetNode&)            = delete;
    AxeraNetNode& operator=(const AxeraNetNode&) = delete;

    using NetNode::Forward;
    DeviceType GetTopBlobDeviceType() override;
    Status InferTopShapes() override;
    Status LoadWeight(const char* data, size_t size) override;
    Status Forward(std::vector<std::shared_ptr<Blob>>& bottom_blobs,
                   std::vector<std::shared_ptr<Blob>>& top_blobs) override;

private:
    struct TensorMeta {
        std::vector<int> shape;
        int dtype{0};
        int layout{0};
        uint32_t size{0};
    };
    struct IoBuffer {
        uint64_t phy{0};
        void* data{nullptr};
        uint32_t size{0};
    };

    void ResetModel() noexcept;
    Status QueryMetadata();
    Status CopyInput(Blob& blob);

    AxeraEngineLease engine_;
    void* handle_{nullptr};
    std::vector<unsigned char> model_data_;
    std::vector<TensorMeta> inputs_;
    std::vector<TensorMeta> outputs_;
    std::vector<IoBuffer> input_buffers_;
    std::vector<IoBuffer> output_buffers_;
    std::mutex mutex_;
};

}  // namespace cosmo::nn
#endif  // COSMO_NN_USE_AXERA_BACKEND
