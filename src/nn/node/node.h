#pragma once

#include <memory>

#include "nn/core/blob.h"
#include "nn/core/shared_resource.h"
#include "nn/core/status.h"
#include "nn/node/node_type.h"
#include "nn/utils/op.h"
#include "nn/utils/timer.h"

namespace cosmo::nn {

class Node {
public:
    // empty
    Node();

    virtual ~Node();

    void SetExtraResource(void* resource);

    std::string GetNodeName();
    void SetNodeName(std::string name);
    void UpdateNodeName(int count);

    NodeType GetNodeType();
    void SetNodeType(NodeType type);

    size_t GetMaxBatch();
    void SetMaxBatch(size_t max_batch);

    double GetInferTime();

    void SetCurrentBatch(std::shared_ptr<Blob> top, size_t batch);
    void SetCurrentBatch(std::vector<std::shared_ptr<Blob>>& tops, size_t batch);

    // top/bottom blobs
    void AddTopBlobName(std::string name);
    std::vector<std::string> GetTopBlobNames();
    void SetTopBlobNames(std::vector<std::string> names);

    std::vector<std::string> GetBottomBlobNames();
    void SetBottomBlobNames(std::vector<std::string> names);

    std::vector<DimsVector> GetTopBlobShapes();
    std::vector<DataType> GetTopBlobDataTypes();
    virtual DeviceType GetTopBlobDeviceType();

    virtual size_t GetBottomCount();
    virtual size_t GetTopCount();

    /**
     * infer top shapes
     */
    virtual Status InferTopShapes();
    virtual Status InferTopShapesWithBottoms(std::vector<DimsVector> dims, std::vector<DataType> types);

    /**
     * @note If this node is first calculate node, return false
     * @return true if need bottom shapes infered
     */
    virtual bool NeedBottomShapesInfered();

    virtual void LoadParam(Op* op);

    // inference
    virtual Status Forward(std::vector<std::shared_ptr<Blob>>& bottom_blobs,
                           std::vector<std::shared_ptr<Blob>>& top_blobs);

    /**
     * first calculate node inference
     */
    virtual Status Forward(std::vector<std::shared_ptr<Blob>>& bottom_blob,
                           std::vector<std::shared_ptr<Blob>>& params,
                           std::vector<std::shared_ptr<Blob>>& top_blobs);

    void SetSharedResource(SharedResource* resource);

public:
    // one input and one output
    bool one_blob_only;

    /**
     * first calculate node flag
     * first calculate node may has input in discontinous memory,
     * other nodes have input in continuous memory
     * all nodes have output in continuous memory
     */
    bool first_calculate_node;

protected:
    // node type
    NodeType node_type;

    // node name
    std::string name;

    // top blob shape and data type
    std::vector<DimsVector> top_blob_shapes;
    std::vector<DataType> top_blob_data_types;

    // blob names
    std::vector<std::string> top_blob_names;
    std::vector<std::string> bottom_blob_names;

    int max_batch;

    void* extra_resource            = nullptr;
    SharedResource* shared_resource = nullptr;

    Timer timer;
};

Status CheckNodeForwardParam(Blob& bottom_blob, Blob& top_blob, bool same_device);

Status CheckNodeInputOutput(std::shared_ptr<Blob>& bottom_blob, std::shared_ptr<Blob>& top_blob,
                            bool same_device);
Status CheckNodeInputOutput(std::vector<std::shared_ptr<Blob>>& bottom_blob,
                            std::vector<std::shared_ptr<Blob>>& top_blob, bool same_device);

}  // namespace cosmo::nn
