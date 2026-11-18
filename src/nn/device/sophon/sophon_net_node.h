#pragma once

#include <memory>
#include <mutex>
#include <vector>

#include "bmcv_api.h"
#include "bmcv_api_ext.h"
#include "bmlib_runtime.h"
#include "bmruntime_interface.h"
#include "nn/node/net_node.h"

namespace cosmo::nn {

struct BmrtDeleter {
    void operator()(void* runtime) const noexcept;
};

using OwnedBmrt = std::unique_ptr<void, BmrtDeleter>;

typedef struct _netInfos {
    std::vector<std::vector<int>> inPuts;
    std::vector<std::vector<int>> outPuts;
    int netInputH = 0;
    int netInputW = 0;
} netInfos;

class SophonNetNode : public NetNode {
public:
    SophonNetNode();

    virtual ~SophonNetNode();

    virtual DeviceType GetTopBlobDeviceType() override;

    virtual Status InferTopShapes() override;

    virtual Status LoadWeight(const char* data, size_t size) override;

    /// Consume an externally-created bmrt handle whose model data is already loaded.
    /// The handle is destroyed on every failure path or retained by the node on success.
    Status AttachOwnedBmrt(OwnedBmrt bmrt_handle);

    virtual size_t GetBottomCount() override;

    virtual size_t GetTopCount() override;

    virtual Status Forward(std::vector<std::shared_ptr<Blob>>& bottom_blobs,
                           std::vector<std::shared_ptr<Blob>>& top_blobs) override;

private:
    void ModelDescInfo(const bm_net_info_t* modelDesc);

    DataType FromTensorDataType(bm_data_type_t data_type);

    void showInfo();

    /// Common setup after bmrt is created and bmodel data is loaded.
    /// Extracts network info, allocates input/output tensors, allocates device memory.
    Status SetupNetworkAfterBmrt();

    /// Allocate device memory for all output tensors. Rolls back on failure.
    Status AllocateOutputDeviceMemory();
    Status PrepareInputSize();
    Status ProbeInputSize();
    Status CheckInputSizeOutputs();

    /// Copy a single output tensor to a top blob (CPU or device).
    Status CopyOutputToBlob(const std::shared_ptr<Blob>& top_blob, int tensor_idx);

private:
    bm_handle_t pbmhandle = nullptr;

    void* m_bmrt = nullptr;
    std::vector<bm_tensor_t> input_tensors;
    std::vector<bm_tensor_t> output_tensors;
    std::vector<std::string> m_network_names;
    const bm_net_info_t* m_netinfo = nullptr;
    int m_output_num               = 0;  // Cached output count to avoid dangling m_netinfo in destructor
    std::vector<uint32_t> tensor_size_vec;
    std::vector<size_t> tensor_capacity_vec_;
    std::vector<bm_shape_t> selected_output_shapes_;
    int selected_stage_{0};

    static std::mutex sophon_net_mutex;
    uint32_t batch_size = 1;
    netInfos netInfo_;
    size_t input_tensor_size = 0;

    std::vector<const char*> bottom_tensor_names;
    std::vector<const char*> top_tensor_names;
};

}  // namespace cosmo::nn
