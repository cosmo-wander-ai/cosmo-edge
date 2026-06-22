#pragma once

#include <mutex>
#include <vector>

#include "bmcv_api.h"
#include "bmcv_api_ext.h"
#include "bmlib_runtime.h"
#include "bmruntime_interface.h"
#include "nn/node/net_node.h"

namespace cosmo::nn {

typedef struct _netInfos {
    std::vector<std::vector<int>> inPuts;
    std::vector<std::vector<int>> outPuts;
    int netInputH;
    int netInputW;
} netInfos;

class SophonNetNode : public NetNode {
public:
    SophonNetNode();

    virtual ~SophonNetNode();

    virtual DeviceType GetTopBlobDeviceType() override;

    virtual Status InferTopShapes() override;

    virtual Status LoadWeight(const char* data, size_t size) override;

    /// Attach an externally-created bmrt handle (from model guard .so).
    /// The bmrt must already have bmodel data loaded via bmrt_load_bmodel_data.
    /// Skips bmrt_create + bmrt_load_bmodel_data, but performs all subsequent
    /// setup: network info, input/output tensors, device memory allocation.
    Status AttachBmrt(void* bmrt_handle);

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

    static std::mutex sophon_net_mutex;
    uint32_t batch_size = 1;
    netInfos netInfo_;
    size_t input_tensor_size = 0;

    std::vector<const char*> bottom_tensor_names;
    std::vector<const char*> top_tensor_names;
};

}  // namespace cosmo::nn
