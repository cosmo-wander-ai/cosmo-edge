#pragma once

#include <initializer_list>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "nn/core/blob.h"
#include "nn/core/blob_store.h"
#include "nn/core/shared_resource.h"
#include "nn/core/status.h"
#include "nn/node/node.h"
#include "nn/utils/model_info_utils.h"
#include "nn/utils/profiler.h"

namespace cosmo::nn {

class Graph {
public:
    // empty
    Graph() = default;

    Graph(Graph const& other)             = delete;
    Graph& operator=(Graph const& other)  = delete;
    Graph(Graph const&& other)            = delete;
    Graph& operator=(Graph const&& other) = delete;

    virtual ~Graph();

    const std::string& GetName() const;
    void SetName(const std::string& name);

    Status Init(CombinedModelInfo& info, const std::string& model_path, DeviceType,
                const std::string& tokenizer_path = std::string(), int device_id = 0, bool use_skip = false);

    void SetProfiler(IProfiler*);

    IProfiler* GetProfiler() const;

    SharedResource* GetSharedResource();

    int GetMaxBatchSize() const;

    Status Forward(std::initializer_list<std::vector<std::shared_ptr<Blob>>> params);

    /** Same as Forward(initializer_list), but supports more than typical braced-list limits when
     *  combined models need repeated (image, aux) groups per sub-network.
     *  Non-const: Node::Forward requires mutable blob vectors. */
    Status Forward(std::vector<std::vector<std::shared_ptr<Blob>>>& params);

    std::vector<std::shared_ptr<Blob>> Output();

private:
    Status MakeUp(CombinedModelInfo&, bool);

    Status LoadWeight(const std::string&);

    Status InferTopDesc();

    Status AddCombinedModelNode(CombinedModelInfo&);

    void AddInputNodeIfNecessary(Node*);

    Node* GetNodeByName(const std::string& name);

    void AddNodeTopBlobs(Node*);
    void AddNodeTopBlobs(Node*, int);

    std::vector<std::vector<std::string>> GetModelInputNames(CombinedModelInfo&);
    std::vector<std::vector<std::string>> GetModelOutputNames(CombinedModelInfo&);

    std::string Dump();

    // Phase 2: helper to create IdentityNode for pass-through decoder inputs
    Node* CreateIdentityNodeForInput(const InputNodeInfo& input_info,
                                     std::vector<std::string>& preprocess_output_blobs);

    // Phase 2: helper to match input against previous model outputs (name then shape fallback)
    bool MatchPreviousModelOutput(const std::string& input_name, const DimsVector& input_shape,
                                  const std::vector<std::string>& prev_output_blobs,
                                  const std::vector<std::string>& prev_output_names,
                                  const std::vector<DimsVector>& prev_output_shapes,
                                  std::set<size_t>& used_prev_outputs, std::string& matched_blob);

    // Phase 3: build preprocess node chain for one model input
    Status BuildPreprocessChain(ModelInfo& model, size_t input_idx, bool use_skip,
                                const std::vector<std::string>& prev_output_blobs,
                                const std::vector<std::string>& prev_output_names,
                                const std::vector<DimsVector>& prev_output_shapes,
                                std::set<size_t>& used_prev_outputs,
                                std::vector<std::string>& preprocess_output_blobs, size_t& skip_count);

    // Phase 3: wire NetNode inputs/outputs with copy nodes
    Status WireNetNode(const ModelInfo& model, const std::vector<std::string>& preprocess_output_blobs,
                       const std::vector<std::string>& prev_output_names,
                       const std::vector<DimsVector>& prev_output_shapes, std::set<size_t>& used_prev_outputs,
                       const std::vector<std::string>& actual_input_names,
                       const std::vector<std::string>& actual_output_names,
                       std::vector<std::string>& current_model_output_blobs);

    // Phase 3: build postprocess node chain for one model
    Status BuildPostprocessChain(const ModelInfo& model,
                                 std::vector<std::string>& current_model_output_blobs);

private:
    int device_id           = 0;
    DeviceType device_type_ = DEVICE_SOPHON_TPU;

    std::string name{};
    int max_batch_size = 1;

    IProfiler* profiler = nullptr;

    std::vector<std::unique_ptr<Node>> nodes{};

    std::unique_ptr<BlobStore> blob_store;

    std::vector<std::string> output_blob_names{};
    std::vector<ModelInfo> model_infos_{};

    std::unique_ptr<SharedResource> shared_resource;
};

}  // namespace cosmo::nn
