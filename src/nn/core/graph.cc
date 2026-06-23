#include "nn/core/graph.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <new>
#include <set>

#include "nn/core/blob_store.h"
#include "nn/node/copy_node.h"
#include "nn/node/identity_node.h"
#include "nn/node/input_node.h"
#include "nn/node/net_node.h"
#include "nn/node/node_type_utils.h"
#include "nn/utils/data_type_utils.h"
#include "nn/utils/model_header_info.h"
#include "nn/utils/model_info_utils.h"
#include "nn/utils/net_utils.h"
#include "nn/utils/string_format.h"
#include "util/DurationLogger.h"

#if defined(COSMO_NN_USE_SOPHON_BACKEND) && defined(COSMO_HAS_MODEL_GUARD)
#include "nn/device/sophon/sophon_net_node.h"
#include "nn/guard/cosmo_model_guard.h"
#endif

namespace cosmo::nn {

Graph::~Graph() {
    nodes.clear();
}

Status Graph::Init(CombinedModelInfo& info, const std::string& model_path, DeviceType device_type,
                   const std::string& tokenizer_path, int device_id, bool use_skip) {
    this->device_type_ = device_type;
    this->blob_store   = std::make_unique<BlobStore>(device_type, device_id);

    name = info.algorithmcode;
    try {
        shared_resource = std::make_unique<SharedResource>(device_id);
    } catch (std::exception& e) {
        return Status(COSMO_NN_ERR_GRAPH_NOT_INIT, e.what());
    }

    if (!tokenizer_path.empty())
        shared_resource->tokenizer_path = tokenizer_path;

    RETURN_ON_FAIL(MakeUp(info, use_skip));

    for (auto& node : nodes) {
        if (!NodeTypeUtils::IsInputNode(node.get())) {
            node->SetSharedResource(shared_resource.get());
        }
    }

    RETURN_ON_FAIL(LoadWeight(model_path));
    RETURN_ON_FAIL(InferTopDesc());

    if (profiler)
        profiler->ReportGraphInfo(Dump().c_str());

    return blob_store->AllocaAllBlobs();
}

void Graph::SetProfiler(IProfiler* p) {
    profiler = p;
}

IProfiler* Graph::GetProfiler() const {
    return profiler;
}

SharedResource* Graph::GetSharedResource() {
    return shared_resource.get();
}

void Graph::SetName(const std::string& name_) {
    name = name_;
}

const std::string& Graph::GetName() const {
    return name;
}

int Graph::GetMaxBatchSize() const {
    return max_batch_size;
}

// ---------------------------------------------------------------------------
// Phase 2 helper: match input against previous model outputs
// ---------------------------------------------------------------------------
bool Graph::MatchPreviousModelOutput(const std::string& input_name, const DimsVector& input_shape,
                                     const std::vector<std::string>& prev_output_blobs,
                                     const std::vector<std::string>& prev_output_names,
                                     const std::vector<DimsVector>& prev_output_shapes,
                                     std::set<size_t>& used_prev_outputs, std::string& matched_blob) {
    // Step 1: exact name matching
    for (size_t k = 0; k < prev_output_names.size(); k++) {
        if (prev_output_names.at(k) == input_name) {
            matched_blob = prev_output_blobs.at(k);
            used_prev_outputs.insert(k);
            return true;
        }
    }
    // Step 2: shape-based fallback matching
    // This handles cases where platform-generated JSON uses original
    // model node names (e.g. ONNX names) that differ from the canonical
    // names expected by the decoder model.
    if (!prev_output_shapes.empty()) {
        for (size_t k = 0; k < prev_output_shapes.size(); k++) {
            if (used_prev_outputs.find(k) != used_prev_outputs.end())
                continue;
            if (prev_output_shapes.at(k) == input_shape) {
                matched_blob = prev_output_blobs.at(k);
                used_prev_outputs.insert(k);
                return true;
            }
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// Phase 2 helper: create IdentityNode for pass-through decoder inputs
// ---------------------------------------------------------------------------
Node* Graph::CreateIdentityNodeForInput(const InputNodeInfo& input_info,
                                        std::vector<std::string>& preprocess_output_blobs) {
    NodeType node_type = NODE_IDENTITY;
    int count          = NodeTypeUtils::TypedNodeCount(nodes, node_type);
    auto identity_node = NodeTypeUtils::CreateNode(node_type, count, max_batch_size, device_type_);

    if (!identity_node)
        return nullptr;

    auto shape              = input_info.shape;
    DataType data_type_enum = DataTypeFromInputInfo(input_info.data_type);

    // Set max_batch in shape if shape is not empty
    if (!shape.empty() && max_batch_size > 0) {
        shape[0] = max_batch_size;
    }

    auto* typed = dynamic_cast<IdentityNode*>(identity_node.get());
    if (!typed) {
        return nullptr;
    }
    typed->SetExpectedShape(shape, data_type_enum);

    // Set as first_calculate_node so it can receive input from params
    identity_node->first_calculate_node = true;
    AddInputNodeIfNecessary(identity_node.get());

    // Add top blob for this identity node
    AddNodeTopBlobs(identity_node.get());

    Node* raw_node = identity_node.get();
    nodes.emplace_back(std::move(identity_node));

    // Record the output blob for connecting to NetNode
    auto top_blobs = raw_node->GetTopBlobNames();
    if (!top_blobs.empty()) {
        preprocess_output_blobs.push_back(top_blobs.at(0));
    }

    return raw_node;
}

// ---------------------------------------------------------------------------
// Phase 3: build preprocess chain for one model input
// ---------------------------------------------------------------------------
Status Graph::BuildPreprocessChain(ModelInfo& model, size_t input_idx, bool use_skip,
                                   const std::vector<std::string>& prev_output_blobs,
                                   const std::vector<std::string>& prev_output_names,
                                   const std::vector<DimsVector>& prev_output_shapes,
                                   std::set<size_t>& used_prev_outputs,
                                   std::vector<std::string>& preprocess_output_blobs, size_t& skip_count) {
    auto& input_node_infos = model.input_node_infos;
    auto input_name        = input_node_infos.at(input_idx).name;
    auto& ops              = input_node_infos.at(input_idx).ops;

    // Check if this input should come from previous model's output
    if (!prev_output_blobs.empty() && ops.empty()) {
        std::string matched_blob;
        if (MatchPreviousModelOutput(input_name, input_node_infos.at(input_idx).shape, prev_output_blobs,
                                     prev_output_names, prev_output_shapes, used_prev_outputs,
                                     matched_blob)) {
            preprocess_output_blobs.push_back(matched_blob);
            return COSMO_NN_OK;
        }
    }

    if (ops.empty()) {
        // No preprocess, and not from previous model
        // Create an IdentityNode as a pass-through first_calculate_node
        // This will convert params to blobs for decoder inputs like point_labels, mask_input,
        // has_mask_input
        auto* node = CreateIdentityNodeForInput(input_node_infos.at(input_idx), preprocess_output_blobs);
        if (!node)
            return Status(COSMO_NN_ERR_NODE_CREATE,
                          "Failed to create identity node for input: " + input_name);

        auto top_blobs = node->GetTopBlobNames();
        if (top_blobs.empty()) {
            return Status(COSMO_NN_ERR_NODE_CREATE,
                          "IdentityNode created but has no top blobs for input: " + input_name);
        }
        return COSMO_NN_OK;
    }

    NetUtils::OptimizePreOps(ops, use_skip);

    // After optimization, ops might become empty (e.g., all ops were skipped)
    // In this case, treat it as no preprocess and create an IdentityNode
    if (ops.empty()) {
        auto* node = CreateIdentityNodeForInput(input_node_infos.at(input_idx), preprocess_output_blobs);
        if (!node)
            return Status(COSMO_NN_ERR_NODE_CREATE,
                          "Failed to create identity node for input: " + input_name);

        auto top_blobs = node->GetTopBlobNames();
        if (top_blobs.empty()) {
            return Status(COSMO_NN_ERR_NODE_CREATE,
                          "IdentityNode created but has no top blobs for input: " + input_name);
        }
        return COSMO_NN_OK;
    }

    bool first_calculate_node  = true;
    Node* last_preprocess_node = nullptr;
    for (size_t k = 0; k < ops.size(); k++) {
        Op* op = ops.at(k).get();

        NodeType node_type = NodeTypeUtils::NodeTypeFromStr(op->name);
        int count          = NodeTypeUtils::TypedNodeCount(nodes, node_type);
        auto node          = NodeTypeUtils::CreateNode(node_type, count, max_batch_size, device_type_);
        if (!node)
            return Status(COSMO_NN_ERR_NODE_CREATE, "unknown node type from op name");
        node->LoadParam(op);
        // set top blobs
        AddNodeTopBlobs(node.get());

        if (first_calculate_node) {
            node->first_calculate_node = true;
            first_calculate_node       = false;
            AddInputNodeIfNecessary(node.get());
        } else {
            // set bottom blob
            auto previous_node_top_blob_names = nodes.back()->GetTopBlobNames();
            node->SetBottomBlobNames(previous_node_top_blob_names);
        }

        Node* raw_node = node.get();
        nodes.emplace_back(std::move(node));
        last_preprocess_node = raw_node;
    }

    // Record the output blob of the last preprocess node for this input
    if (!last_preprocess_node) {
        return Status(COSMO_NN_ERR_NODE_CREATE,
                      "No preprocess node created for input with ops: " + input_name);
    }

    auto top_blobs = last_preprocess_node->GetTopBlobNames();
    if (!top_blobs.empty()) {
        // For nodes with multiple outputs (like DinoEncodeNode with 8 outputs),
        // add all outputs to preprocess_output_blobs
        for (size_t k = 0; k < top_blobs.size(); k++) {
            preprocess_output_blobs.push_back(top_blobs.at(k));
        }
        // If this node produced multiple outputs, skip the next (top_blobs.size() - 1) inputs
        // since they are already covered by this node's outputs
        if (top_blobs.size() > 1) {
            skip_count = top_blobs.size() - 1;
        }
    } else {
        return Status(COSMO_NN_ERR_NODE_CREATE,
                      "Preprocess node created but has no top blobs for input: " + input_name);
    }

    return COSMO_NN_OK;
}

// ---------------------------------------------------------------------------
// Phase 3: wire NetNode inputs/outputs with copy nodes
// ---------------------------------------------------------------------------
Status Graph::WireNetNode(const ModelInfo& model, const std::vector<std::string>& preprocess_output_blobs,
                          const std::vector<std::string>& prev_output_names,
                          const std::vector<DimsVector>& prev_output_shapes,
                          std::set<size_t>& used_prev_outputs,
                          const std::vector<std::string>& actual_input_names,
                          const std::vector<std::string>& actual_output_names,
                          std::vector<std::string>& current_model_output_blobs) {
    auto& model_input_node_infos = model.input_node_infos;
    auto& output_node_infos      = model.output_node_infos;
    auto net_output_num          = output_node_infos.size();

    // add net node
    int count     = NodeTypeUtils::TypedNodeCount(nodes, NodeType::NODE_NET);
    auto net_node = NodeTypeUtils::CreateNode(NODE_NET, count, max_batch_size, device_type_);

    // Set NetNode's bottom blobs
    std::vector<std::string> net_bottom_blobs;

    for (size_t j = 0; j < model_input_node_infos.size(); j++) {
        auto input_name = model_input_node_infos.at(j).name;
        if (j < preprocess_output_blobs.size() && !preprocess_output_blobs.at(j).empty()) {
            net_bottom_blobs.push_back(preprocess_output_blobs.at(j));
        } else {
            std::string matched_blob;
            bool from_prev = MatchPreviousModelOutput(input_name, model_input_node_infos.at(j).shape, {},
                                                      prev_output_names, prev_output_shapes,
                                                      used_prev_outputs, matched_blob);

            if (from_prev) {
                net_bottom_blobs.push_back(matched_blob);
            } else {
                int current_input_num  = NodeTypeUtils::TypedNodeCount(nodes, NODE_INPUT);
                int required_input_num = current_input_num + 1;

                if (required_input_num > current_input_num) {
                    auto new_input = NodeTypeUtils::CreateNode(NODE_INPUT, current_input_num, max_batch_size,
                                                               device_type_);
                    nodes.emplace_back(std::move(new_input));
                }

                std::string input_blob_name = "input_" + std::to_string(current_input_num) + "/0";
                net_bottom_blobs.push_back(input_blob_name);
            }
        }
    }

    // For NetNode inputs, if they come from NAIVE device nodes, insert copy nodes to convert to TPU
    std::vector<std::string> final_net_bottom_blobs;
    std::vector<std::unique_ptr<Node>> new_copy_nodes;

    int copy_count = NodeTypeUtils::TypedNodeCount(nodes, NodeType::NODE_COPY);

    for (size_t k = 0; k < net_bottom_blobs.size(); k++) {
        auto blob_name = net_bottom_blobs.at(k);

        // Find the node that produces this blob
        Node* producer_node = nullptr;
        for (auto& node : nodes) {
            auto top_names = node->GetTopBlobNames();
            if (std::find(top_names.begin(), top_names.end(), blob_name) != top_names.end()) {
                producer_node = node.get();
                break;
            }
        }

        if (producer_node && producer_node->GetTopBlobDeviceType() == DEVICE_NAIVE &&
            device_type_ != DEVICE_CPU && device_type_ != DEVICE_NAIVE) {
            // This input is from NAIVE device, insert a H2D copy node (Sophon only)
            auto copy_node = NodeTypeUtils::CreateNode(NODE_COPY, copy_count, max_batch_size, device_type_);
            copy_count++;

            auto* copy_ptr = dynamic_cast<CopyNode*>(copy_node.get());
            if (!copy_ptr) {
                return Status(COSMO_NN_ERR_NODE_CREATE, "Failed to create H2D copy node");
            }
            copy_ptr->SetDirection(0, 1);

            copy_node->SetBottomBlobNames({blob_name});
            AddNodeTopBlobs(copy_node.get());

            final_net_bottom_blobs.push_back(copy_node->GetTopBlobNames().at(0));
            new_copy_nodes.push_back(std::move(copy_node));
        } else {
            // Use original blob (CPU mode: net reads from host directly)
            final_net_bottom_blobs.push_back(blob_name);
        }
    }

    // Reorder final_net_bottom_blobs according to network_actual_input_names
    if (actual_input_names.size() == final_net_bottom_blobs.size()) {
        std::map<std::string, std::string> name_to_blob;
        for (size_t k = 0; k < model_input_node_infos.size() && k < final_net_bottom_blobs.size(); k++) {
            name_to_blob[model_input_node_infos.at(k).name] = final_net_bottom_blobs.at(k);
        }

        std::vector<std::string> reordered_blobs;
        for (size_t k = 0; k < actual_input_names.size(); k++) {
            auto it = name_to_blob.find(actual_input_names.at(k));
            if (it != name_to_blob.end()) {
                reordered_blobs.push_back(it->second);
            } else if (k < final_net_bottom_blobs.size()) {
                reordered_blobs.push_back(final_net_bottom_blobs.at(k));
            }
        }

        if (reordered_blobs.size() == final_net_bottom_blobs.size()) {
            final_net_bottom_blobs = reordered_blobs;
        }
    }

    net_node->SetBottomBlobNames(final_net_bottom_blobs);

    // set network actual input/output names
    auto* net_ptr = dynamic_cast<NetNode*>(net_node.get());
    if (!net_ptr) {
        return Status(COSMO_NN_ERR_NODE_CREATE, "Failed to cast to NetNode");
    }
    net_ptr->SetNetworkInputNames(actual_input_names);
    net_ptr->SetNetworkOutputNames(actual_output_names);

    // Insert copy nodes right before net_node (after all preprocess nodes)
    for (auto& copy_node : new_copy_nodes) {
        nodes.emplace_back(std::move(copy_node));
    }

    Node* net_raw = net_node.get();
    nodes.emplace_back(std::move(net_node));

    // Create copy nodes for net outputs depending on device mode
    bool has_host_post =
        std::any_of(output_node_infos.begin(), output_node_infos.end(), [](const OutputNodeInfo& o) {
            return o.op && (o.op->name == "yolov8_postprocess" || o.op->name == "yolo_postprocess" ||
                            o.op->name == "yolo_e2e_postprocess");
        });

    bool is_cpu_device = (device_type_ == DEVICE_CPU || device_type_ == DEVICE_NAIVE);

    if (is_cpu_device) {
        // CPU mode: net output is already in host memory, no D2H copy needed.
        AddNodeTopBlobs(net_raw, net_output_num);
        for (size_t j = 0; j < net_output_num; j++) {
            auto blob_name = net_raw->GetTopBlobNames().at(j);
            current_model_output_blobs.push_back(blob_name);
            output_blob_names.push_back(blob_name);
        }
    } else if (has_host_post && net_output_num == 1) {
        // Create copy node and its top (CPU) first; net will write directly to it via D2S
        int cnt        = NodeTypeUtils::TypedNodeCount(nodes, NodeType::NODE_COPY);
        auto copy      = NodeTypeUtils::CreateNode(NODE_COPY, cnt, max_batch_size, device_type_);
        auto* copy_cst = dynamic_cast<CopyNode*>(copy.get());
        if (copy_cst)
            copy_cst->SetDirection(0, 0);  // CPU->CPU no-op, net already wrote to copy_0/0
        AddNodeTopBlobs(copy.get());
        auto copy_top = copy->GetTopBlobNames().at(0);
        copy->SetBottomBlobNames({copy_top});  // same blob: copy Forward is memcpy(dst,dst) no-op
        nodes.emplace_back(std::move(copy));
        net_raw->AddTopBlobName(copy_top);  // net writes directly to copy's top (CPU)
        if (net_ptr)
            net_ptr->SetOutputToCpu(true);  // net will D2S to CPU blob
        current_model_output_blobs.push_back(copy_top);
        output_blob_names.push_back(copy_top);
    } else {
        AddNodeTopBlobs(net_raw, net_output_num);
        for (size_t j = 0; j < net_output_num; j++) {
            int cnt        = NodeTypeUtils::TypedNodeCount(nodes, NodeType::NODE_COPY);
            auto copy      = NodeTypeUtils::CreateNode(NODE_COPY, cnt, max_batch_size, device_type_);
            auto* copy_cst = dynamic_cast<CopyNode*>(copy.get());
            if (copy_cst)
                copy_cst->SetDirection(1, 0);
            AddNodeTopBlobs(copy.get());
            copy->SetBottomBlobNames({net_raw->GetTopBlobNames().at(j)});
            auto copy_top_blob_names = copy->GetTopBlobNames();
            nodes.emplace_back(std::move(copy));
            current_model_output_blobs.insert(current_model_output_blobs.end(), copy_top_blob_names.begin(),
                                              copy_top_blob_names.end());
            output_blob_names.insert(output_blob_names.end(), copy_top_blob_names.begin(),
                                     copy_top_blob_names.end());
        }
    }

    return COSMO_NN_OK;
}

// ---------------------------------------------------------------------------
// Phase 3: build postprocess chain for one model
// ---------------------------------------------------------------------------
Status Graph::BuildPostprocessChain(const ModelInfo& model,
                                    std::vector<std::string>& current_model_output_blobs) {
    auto& output_node_infos = model.output_node_infos;
    auto net_output_num     = output_node_infos.size();

    if (std::all_of(output_node_infos.begin(), output_node_infos.end(),
                    [](const OutputNodeInfo& info) { return !info.op; }))
        return COSMO_NN_OK;

    // Postprocess nodes should only use current model's outputs, not all previous models' outputs
    std::vector<std::string> postprocess_input_blobs = current_model_output_blobs;
    for (size_t j = 0; j < net_output_num; j++) {
        auto* op = output_node_infos.at(j).op.get();
        if (!op)
            continue;

        auto node_type = NodeTypeUtils::NodeTypeFromStr(op->name);
        int count      = NodeTypeUtils::TypedNodeCount(nodes, node_type);
        auto node      = NodeTypeUtils::CreateNode(node_type, count, max_batch_size, device_type_);

        if (!node)
            return Status(COSMO_NN_ERR_NODE_CREATE, "unknown node type from op name");

        node->LoadParam(op);

        node->SetBottomBlobNames(postprocess_input_blobs);
        AddNodeTopBlobs(node.get());

        postprocess_input_blobs = node->GetTopBlobNames();
        nodes.emplace_back(std::move(node));
        output_blob_names = postprocess_input_blobs;  // Update global output_blob_names for next model
    }

    return COSMO_NN_OK;
}

Status Graph::MakeUp(CombinedModelInfo& info, bool use_skip) {
    // add one input node to graph first
    auto input = std::make_unique<InputNode>();
    nodes.emplace_back(std::move(input));

    auto network_actual_input_names  = GetModelInputNames(info);
    auto network_actual_output_names = GetModelOutputNames(info);

    size_t model_node_num = info.models.size();

    // Track previous model's output blobs for cross-model connections (e.g., encoder -> decoder)
    std::vector<std::string> prev_model_output_blobs;
    std::vector<std::string> prev_model_output_names;
    std::vector<DimsVector> prev_model_output_shapes;

    for (size_t i = 0; i < model_node_num; i++) {
        auto& model                  = info.models.at(i);
        auto& model_input_node_infos = model.input_node_infos;
        if (max_batch_size != model.max_batch)
            max_batch_size = model.max_batch;

        // Track preprocess node outputs for this model (to connect to NetNode)
        std::vector<std::string> preprocess_output_blobs;

        // Track which previous model outputs have been consumed by shape matching
        std::set<size_t> used_prev_outputs;

        // Build preprocess chain for each input
        for (size_t j = 0; j < model_input_node_infos.size(); j++) {
            size_t skip_count = 0;
            RETURN_ON_FAIL(BuildPreprocessChain(model, j, use_skip, prev_model_output_blobs,
                                                prev_model_output_names, prev_model_output_shapes,
                                                used_prev_outputs, preprocess_output_blobs, skip_count));
            j += skip_count;
        }

        // Wire NetNode with copy nodes
        std::vector<std::string> current_model_output_blobs;
        RETURN_ON_FAIL(WireNetNode(model, preprocess_output_blobs, prev_model_output_names,
                                   prev_model_output_shapes, used_prev_outputs,
                                   network_actual_input_names.at(i), network_actual_output_names.at(i),
                                   current_model_output_blobs));

        // Save this model's outputs for potential use by next model
        auto& output_node_infos = model.output_node_infos;
        auto net_output_num     = output_node_infos.size();

        prev_model_output_blobs.clear();
        prev_model_output_names.clear();
        prev_model_output_shapes.clear();
        for (size_t j = 0; j < net_output_num; j++) {
            if (j >= current_model_output_blobs.size())
                return Status(COSMO_NN_ERR_PARAM, "Current model output blob count mismatch");
            prev_model_output_blobs.push_back(current_model_output_blobs.at(j));
            prev_model_output_names.push_back(output_node_infos.at(j).name);
            prev_model_output_shapes.push_back(output_node_infos.at(j).shape);
        }

        // Build postprocess chain
        RETURN_ON_FAIL(BuildPostprocessChain(model, current_model_output_blobs));
    }

    Status status = AddCombinedModelNode(info);
    model_infos_  = std::move(info.models);
    return status;
}

Status Graph::AddCombinedModelNode(CombinedModelInfo& info) {
    auto model_count = info.models.size();
    if (model_count < 2)
        return COSMO_NN_OK;

    if (info.reduce == "concat") {
        int count   = NodeTypeUtils::TypedNodeCount(nodes, NODE_CONCAT);
        auto concat = NodeTypeUtils::CreateNode(NODE_CONCAT, count, max_batch_size, device_type_);

        AddNodeTopBlobs(concat.get());
        concat->SetBottomBlobNames(output_blob_names);
        output_blob_names = concat->GetTopBlobNames();
        nodes.push_back(std::move(concat));

    } else if (info.reduce == "mean") {
        int count = NodeTypeUtils::TypedNodeCount(nodes, NODE_MEAN);
        auto mean = NodeTypeUtils::CreateNode(NODE_MEAN, count, max_batch_size, device_type_);

        AddNodeTopBlobs(mean.get());
        mean->SetBottomBlobNames(output_blob_names);
        output_blob_names = mean->GetTopBlobNames();
        nodes.push_back(std::move(mean));

    } else if (info.reduce == "sequential" || info.reduce == "none" || info.reduce.empty()) {
        // Multi-model sequential / pipeline: no merge. Keep all model outputs in
        // output_blob_names (encoder outputs + decoder outputs, etc.). Used by
        // SAM2-style configs where models run in sequence and the second model
        // consumes the first's outputs; wiring is handled elsewhere (e.g. NetNode
        // by input names). Do not add any reduce node.
    } else {
        return Status(COSMO_NN_ERR_UNSUPPORT_NET, "invalid reduce type");
    }

    return COSMO_NN_OK;
}

Status Graph::LoadWeight(const std::string& model_path) {
    if (model_path.empty())
        return Status(COSMO_NN_ERR_PARAM, "model path is empty");

    int net_node_num = NodeTypeUtils::TypedNodeCount(nodes, NODE_NET);

    std::ifstream stream(model_path, std::ios::in | std::ios::binary);
#ifdef COSMO_NN_USE_CPU_BACKEND
    if (stream.fail() && !std::filesystem::is_directory(model_path))
        return Status(COSMO_NN_ERR_LOAD_MODEL, "open model file failed");
#else
    if (stream.fail())
        return Status(COSMO_NN_ERR_LOAD_MODEL, "open model file failed");
#endif

#if defined(COSMO_NN_USE_SOPHON_BACKEND) && defined(COSMO_HAS_MODEL_GUARD)
    // Peek first 4 bytes to detect encrypted model.
    uint32_t magic = 0;
    stream.read(reinterpret_cast<char*>(&magic), sizeof(magic));
    stream.seekg(0);

    if (cosmo::guard::IsEncryptedModel(magic)) {
        // File-based guard API: the .so reads segments on demand from disk.
        // No need to load the entire encrypted file into memory.
        stream.close();

        // Validate segment count matches graph structure.
        int seg_count = cosmo::guard::GetEncryptedSegmentCountFromFile(model_path.c_str());
        if (seg_count < 0)
            return Status(COSMO_NN_ERR_LOAD_MODEL, "Failed to read encrypted segment count (error: " +
                                                       std::to_string(seg_count) + ")");
        if (seg_count != net_node_num)
            return Status(COSMO_NN_ERR_LOAD_MODEL, "Encrypted segment count (" + std::to_string(seg_count) +
                                                       ") does not match graph net nodes (" +
                                                       std::to_string(net_node_num) + ")");

        // Decrypt and load each segment into its own independent bmrt,
        // mirroring the plaintext path where each NetNode has its own bmrt.
        for (int i = 0; i < net_node_num; i++) {
            auto net_node = GetNodeByName("net_" + std::to_string(i));
            if (!net_node)
                return Status(COSMO_NN_ERR_LOAD_MODEL, "Can not find net node");

            // The .so handles everything internally per segment:
            //   open file → seek to segment → read ciphertext
            //   → SN validation → key derivation → decrypt → bmrt_create → bmrt_load → wipe
            void* bmrt = nullptr;
            int ret    = 0;
            {
                cosmo::util::DurationLogger logger("DecryptAndLoadSegmentFromFile net_" + std::to_string(i));
                ret = cosmo::guard::DecryptAndLoadSegmentFromFile(
                    model_path.c_str(), shared_resource->m_handle, static_cast<uint32_t>(i), &bmrt);
            }
            if (ret != 0 || !bmrt)
                return Status(COSMO_NN_ERR_LOAD_MODEL,
                              "Encrypted segment " + std::to_string(i) +
                                  " load failed (guard error: " + std::to_string(ret) + ")");

            // AttachBmrt is on SophonNetNode only (ISP: not on NetNode base class).
            // Within this #ifdef block, the node is guaranteed to be SophonNetNode.
            auto* sophon_node = dynamic_cast<SophonNetNode*>(net_node);
            if (!sophon_node) {
                bmrt_destroy(bmrt);
                return Status(COSMO_NN_ERR_LOAD_MODEL, "Expected SophonNetNode for encrypted model");
            }
            RETURN_ON_FAIL(sophon_node->AttachBmrt(bmrt));
        }
        return COSMO_NN_OK;
    }
#endif

#ifdef COSMO_NN_USE_CPU_BACKEND
    stream.close();

    namespace fs = std::filesystem;
    fs::path base_path(model_path);

    if (net_node_num == 1 && fs::is_regular_file(base_path)) {
        std::ifstream model_stream(model_path, std::ios::in | std::ios::binary);
        if (model_stream.fail())
            return Status(COSMO_NN_ERR_LOAD_MODEL, "open model file failed");

        model_stream.seekg(0, std::ios::end);
        long int model_size = static_cast<long int>(model_stream.tellg());
        model_stream.seekg(0, std::ios::beg);

        if (model_size <= 0)
            return Status(COSMO_NN_ERR_LOAD_MODEL, "model file is empty or unreadable");

        std::unique_ptr<char[]> model_data;
        try {
            model_data.reset(new char[static_cast<size_t>(model_size)]);
        } catch (const std::bad_alloc&) {
            return Status(COSMO_NN_ERR_OUT_OF_MEMORY, "not enough memory to load model");
        }
        model_stream.read(model_data.get(), model_size);

        auto net_node = GetNodeByName("net_0");
        if (!net_node)
            return Status(COSMO_NN_ERR_LOAD_MODEL, "Can not find net node");

        auto* net_ptr = dynamic_cast<NetNode*>(net_node);
        if (!net_ptr)
            return Status(COSMO_NN_ERR_LOAD_MODEL, "Failed to cast to NetNode");

        return net_ptr->LoadWeight(model_data.get(), static_cast<size_t>(model_size));
    }

    if (!fs::is_directory(base_path)) {
        return Status(COSMO_NN_ERR_LOAD_MODEL,
                      "CPU multi-net backend expects model directory with per-net ONNX files");
    }

    if (net_node_num != static_cast<int>(model_infos_.size())) {
        return Status(COSMO_NN_ERR_LOAD_MODEL, "CPU graph net nodes (" + std::to_string(net_node_num) +
                                                   ") do not match config models (" +
                                                   std::to_string(model_infos_.size()) + ")");
    }

    std::vector<fs::path> onnx_files;
    for (const auto& entry : fs::directory_iterator(base_path)) {
        if (entry.is_regular_file() && entry.path().extension() == ".onnx")
            onnx_files.push_back(entry.path());
    }
    std::sort(onnx_files.begin(), onnx_files.end());

    for (int i = 0; i < net_node_num; i++) {
        fs::path part_path;
        if (!model_infos_.at(i).filename.empty()) {
            part_path = base_path / model_infos_.at(i).filename;
        } else if (static_cast<size_t>(i) < onnx_files.size()) {
            part_path = onnx_files.at(i);
        } else {
            return Status(COSMO_NN_ERR_LOAD_MODEL,
                          "CPU multi-net model missing file_name for net_" + std::to_string(i));
        }

        std::ifstream model_stream(part_path, std::ios::in | std::ios::binary);
        if (model_stream.fail()) {
            return Status(COSMO_NN_ERR_LOAD_MODEL, "open ONNX model file failed: " + part_path.string());
        }

        model_stream.seekg(0, std::ios::end);
        long int model_size = static_cast<long int>(model_stream.tellg());
        model_stream.seekg(0, std::ios::beg);
        if (model_size <= 0) {
            return Status(COSMO_NN_ERR_LOAD_MODEL,
                          "ONNX model file is empty or unreadable: " + part_path.string());
        }

        std::unique_ptr<char[]> model_data;
        try {
            model_data.reset(new char[static_cast<size_t>(model_size)]);
        } catch (const std::bad_alloc&) {
            return Status(COSMO_NN_ERR_OUT_OF_MEMORY, "not enough memory to load ONNX model");
        }
        model_stream.read(model_data.get(), model_size);

        auto net_node = GetNodeByName("net_" + std::to_string(i));
        if (!net_node)
            return Status(COSMO_NN_ERR_LOAD_MODEL, "Can not find net node");

        auto* net_ptr = dynamic_cast<NetNode*>(net_node);
        if (!net_ptr)
            return Status(COSMO_NN_ERR_LOAD_MODEL, "Failed to cast to NetNode");

        RETURN_ON_FAIL(net_ptr->LoadWeight(model_data.get(), static_cast<size_t>(model_size)));
    }

    return COSMO_NN_OK;
#else
    // Sophon: .nn model file has a header wrapping one or more bmodel segments.
    std::array<char, kPlainNnHeaderSize> header_data{};
    stream.read(header_data.data(), static_cast<std::streamsize>(header_data.size()));
    if (!stream) {
        return Status(COSMO_NN_ERR_LOAD_MODEL, "failed to read .nn model header");
    }

    auto header_result = ParsePlainNnHeader(header_data);
    if (!header_result) {
        return Status(COSMO_NN_ERR_LOAD_MODEL, "invalid .nn model header: " + header_result.error);
    }

    const PlainNnHeader& header_info = header_result.header;
    if (net_node_num != static_cast<int>(header_info.model_count))
        return Status(COSMO_NN_ERR_LOAD_MODEL, "Graph not match with model file");

    stream.seekg(0, std::ios::end);
    if (!stream) {
        return Status(COSMO_NN_ERR_LOAD_MODEL, "failed to seek .nn model file end");
    }
    std::streampos end_pos = stream.tellg();
    if (end_pos == std::streampos(-1)) {
        return Status(COSMO_NN_ERR_LOAD_MODEL, "failed to get .nn model file size");
    }
    std::streamoff raw_file_size_offset = static_cast<std::streamoff>(end_pos);
    if (raw_file_size_offset < 0) {
        return Status(COSMO_NN_ERR_LOAD_MODEL, ".nn model file size is invalid");
    }
    uint64_t raw_file_size = static_cast<uint64_t>(raw_file_size_offset);
    auto file_size_result  = ValidatePlainNnFileSize(header_info, raw_file_size);
    if (!file_size_result) {
        return Status(COSMO_NN_ERR_LOAD_MODEL, "invalid .nn model file size: " + file_size_result.error);
    }
    stream.seekg(static_cast<std::streamoff>(kPlainNnHeaderSize), std::ios::beg);
    if (!stream) {
        return Status(COSMO_NN_ERR_LOAD_MODEL, "failed to seek .nn model payload");
    }

    for (int i = 0; i < net_node_num; i++) {
        auto net_node = GetNodeByName("net_" + std::to_string(i));
        if (!net_node)
            return Status(COSMO_NN_ERR_LOAD_MODEL, "Can not find net node");

        uint64_t seg_model_size_u64 = header_info.model_sizes.at(static_cast<size_t>(i));
        if (seg_model_size_u64 > static_cast<uint64_t>(std::numeric_limits<size_t>::max())) {
            return Status(COSMO_NN_ERR_LOAD_MODEL, "model segment size exceeds size_t limit");
        }
        if (seg_model_size_u64 > static_cast<uint64_t>(std::numeric_limits<std::streamsize>::max())) {
            return Status(COSMO_NN_ERR_LOAD_MODEL, "model segment size exceeds stream read limit");
        }

        size_t seg_model_size = static_cast<size_t>(seg_model_size_u64);
        std::unique_ptr<char[]> model_data;
        try {
            model_data.reset(new char[seg_model_size]);
        } catch (const std::bad_alloc&) {
            return Status(COSMO_NN_ERR_OUT_OF_MEMORY, "not enough memory to load model segment");
        }
        stream.read(model_data.get(), static_cast<std::streamsize>(seg_model_size));
        if (!stream) {
            return Status(COSMO_NN_ERR_LOAD_MODEL, "failed to read model segment");
        }

        auto* net_ptr = dynamic_cast<NetNode*>(net_node);
        if (!net_ptr)
            return Status(COSMO_NN_ERR_LOAD_MODEL, "Failed to cast to NetNode");

        RETURN_ON_FAIL(net_ptr->LoadWeight(model_data.get(), seg_model_size));
    }

    stream.close();
    return COSMO_NN_OK;
#endif
}

Status Graph::InferTopDesc() {
    int node_idx = 0;
    for (auto& node : nodes) {
        if (NodeTypeUtils::IsInputNode(node.get())) {
            node_idx++;
            continue;
        }

        if (node->NeedBottomShapesInfered()) {
            std::vector<DimsVector> blob_dims{};
            std::vector<DataType> data_types{};

            auto bottom_blob_names = node->GetBottomBlobNames();
            for (size_t i = 0; i < bottom_blob_names.size(); i++) {
                std::shared_ptr<Blob> blob = nullptr;
                blob_store->GetBlob(bottom_blob_names.at(i), blob);
                if (!blob)
                    return Status(COSMO_NN_ERR_NULL_PARAM, "Blob not found");

                auto blob_desc = blob->GetBlobDesc();
                blob_dims.push_back(blob_desc.dims);
                data_types.push_back(blob_desc.data_type);
            }

            RETURN_ON_FAIL(node->InferTopShapesWithBottoms(blob_dims, data_types));
        } else {
            RETURN_ON_FAIL(node->InferTopShapes());
        }

        auto top_blob_names = node->GetTopBlobNames();
        auto top_blob_dims  = node->GetTopBlobShapes();
        auto top_data_types = node->GetTopBlobDataTypes();

        if (top_blob_names.size() != top_blob_dims.size() || top_blob_names.size() != top_data_types.size()) {
            return Status(COSMO_NN_ERR_PARAM, "Top blob sizes mismatch for node: " + node->GetNodeName());
        }

        for (size_t i = 0; i < top_blob_names.size(); i++) {
            std::shared_ptr<Blob> blob = nullptr;
            blob_store->GetBlob(top_blob_names.at(i), blob);
            if (!blob)
                return Status(COSMO_NN_ERR_PARAM, "Blob not found by name");

            auto desc        = blob->GetBlobDesc();
            desc.dims        = top_blob_dims.at(i);
            desc.data_type   = top_data_types.at(i);
            desc.device_type = node->GetTopBlobDeviceType();

            if (desc.dims.empty()) {
                return Status(COSMO_NN_ERR_PARAM, "Top blob " + top_blob_names.at(i) +
                                                      " has empty dims for node: " + node->GetNodeName());
            }

            blob->SetBlobDesc(desc);
        }

        node_idx++;
    }
    return COSMO_NN_OK;
}

void Graph::AddInputNodeIfNecessary(Node* node) {
    auto node_type = node->GetNodeType();
    int input_num  = node->GetBottomCount();

    int current_input_node_num = NodeTypeUtils::TypedNodeCount(nodes, NODE_INPUT);

    if (input_num <= current_input_node_num)
        return;

    for (int i = current_input_node_num; i < input_num; i++) {
        auto input = NodeTypeUtils::CreateNode(NODE_INPUT, i, node->GetMaxBatch(), device_type_);
        nodes.emplace_back(std::move(input));
    }
}

Node* Graph::GetNodeByName(const std::string& name) {
    for (auto& node : nodes) {
        if (node && node->GetNodeName() == name) {
            return node.get();
        }
    }
    return nullptr;
}

void Graph::AddNodeTopBlobs(Node* node) {
    AddNodeTopBlobs(node, node->GetTopCount());
}

void Graph::AddNodeTopBlobs(Node* node, int top_blob_count) {
    auto node_name = node->GetNodeName();
    for (int i = 0; i < top_blob_count; i++) {
        BlobDesc blob_desc;
        blob_desc.name = node_name + "/" + std::to_string(i);

        auto blob = std::make_shared<Blob>(blob_desc);
        blob_store->AddBlob(blob);

        node->AddTopBlobName(blob_desc.name);
    }
}

Status Graph::Forward(std::initializer_list<std::vector<std::shared_ptr<Blob>>> params) {
    std::vector<std::vector<std::shared_ptr<Blob>>> params_(params);
    return Forward(params_);
}

Status Graph::Forward(std::vector<std::vector<std::shared_ptr<Blob>>>& params_) {
    // Count total params needed by all first_calculate_nodes
    // Some nodes (like DinoEncodeNode) need multiple params
    int total_params_needed = 0;
    for (auto& node : nodes) {
        if (node->first_calculate_node && !NodeTypeUtils::IsInputNode(node.get())) {
            size_t bottom_count = node->GetBottomCount();
            if (bottom_count == 0)
                bottom_count = 1;  // Default to 1 if not specified
            total_params_needed += static_cast<int>(bottom_count);
        }
    }

    if (total_params_needed != static_cast<int>(params_.size())) {
        return Status(COSMO_NN_ERR_PARAM, "params not match graph requirements (expected: " +
                                              std::to_string(total_params_needed) +
                                              ", got: " + std::to_string(params_.size()) + ")");
    }

    // Track which param index we're at
    int current_param_index = 0;

    size_t node_num = nodes.size();
    for (size_t i = 0; i < node_num; i++) {
        Node* node = nodes.at(i).get();
        if (NodeTypeUtils::IsInputNode(node))
            continue;

        auto top_blob_names = node->GetTopBlobNames();
        std::vector<std::shared_ptr<Blob>> top_blobs;
        blob_store->GetBlobs(top_blob_names, top_blobs);

        if (node->first_calculate_node) {
            // For first_calculate_node, determine how many input params it needs
            size_t bottom_count = node->GetBottomCount();
            if (bottom_count == 0)
                bottom_count = 1;  // Default to 1 if not specified

            if (bottom_count == 1) {
                // Single input node (e.g., ResizeNode, IdentityNode)
                if (current_param_index >= params_.size()) {
                    return Status(COSMO_NN_ERR_PARAM,
                                  "Not enough params for first_calculate_node: " + node->GetNodeName());
                }
                RETURN_ON_FAIL(node->Forward(params_.at(current_param_index), top_blobs));
                current_param_index++;
            } else if (bottom_count == 2) {
                // Dual input node (e.g., DinoEncodeNode with images and prompts)
                if (current_param_index + 1 >= params_.size()) {
                    return Status(
                        COSMO_NN_ERR_PARAM,
                        "Not enough params for dual-input first_calculate_node: " + node->GetNodeName());
                }
                RETURN_ON_FAIL(node->Forward(params_.at(current_param_index),
                                             params_.at(current_param_index + 1), top_blobs));
                current_param_index += 2;
            } else {
                return Status(COSMO_NN_ERR_PARAM, "Unsupported bottom_count for first_calculate_node: " +
                                                      std::to_string(bottom_count));
            }
        } else {
            auto bottom_blob_names = node->GetBottomBlobNames();
            std::vector<std::shared_ptr<Blob>> bottom_blobs;
            blob_store->GetBlobs(bottom_blob_names, bottom_blobs);

            RETURN_ON_FAIL(node->Forward(bottom_blobs, top_blobs));
        }

        if (profiler)
            profiler->ReportNodeTime(node->GetNodeName().c_str(), node->GetInferTime() / 1000);
    }

    return COSMO_NN_OK;
}

std::vector<std::shared_ptr<Blob>> Graph::Output() {
    std::vector<std::shared_ptr<Blob>> blobs;
    blob_store->GetBlobs(output_blob_names, blobs);
    return blobs;
}

std::vector<std::vector<std::string>> Graph::GetModelInputNames(CombinedModelInfo& info) {
    std::vector<std::vector<std::string>> input_names;
    const int num_models = info.models.size();
    for (int i = 0; i < num_models; ++i) {
        auto& input_node_infos = info.models.at(i).input_node_infos;
        std::vector<std::string> names;
        for (int j = 0; j < input_node_infos.size(); ++j) {
            names.push_back(input_node_infos.at(j).name);
        }
        input_names.push_back(names);
    }

    return input_names;
}

std::vector<std::vector<std::string>> Graph::GetModelOutputNames(CombinedModelInfo& info) {
    std::vector<std::vector<std::string>> output_names;
    const int num_models = info.models.size();
    for (int i = 0; i < num_models; ++i) {
        auto& output_node_infos = info.models.at(i).output_node_infos;
        std::vector<std::string> names;
        for (int j = 0; j < output_node_infos.size(); ++j) {
            names.push_back(output_node_infos.at(j).name);
        }
        output_names.push_back(names);
    }

    return output_names;
}

std::string Graph::Dump() {
    std::stringstream ss;
    ss << "Graph:" << name << std::endl;
    size_t node_num = nodes.size();
    ss << "node num:" << node_num << std::endl;

    // table header
    ss << "index\t\ttype\t\tname\t\t\tbottom names\t\t\ttop name"
       << "\t\ttop shape\ttop device" << std::endl;
    for (size_t i = 0; i < node_num; i++) {
        Node* node                = nodes.at(i).get();
        NodeType type             = node->GetNodeType();
        std::string node_type_str = NodeTypeUtils::NodeTypeToStr(type);

        ss << std::setw(4) << i;
        ss << std::setw(16) << node_type_str;
        ss << std::setw(16) << node->GetNodeName();

        ss << std::setw(32);
        auto bottom_blob_names = node->GetBottomBlobNames();
        if (bottom_blob_names.empty())
            ss << "-----";
        ss << VectorToString<std::string>(bottom_blob_names);

        ss << std::setw(32);
        ss << VectorToString<std::string>(node->GetTopBlobNames());

        auto top_shapes = node->GetTopBlobShapes();
        ss << std::setw(20);

        std::stringstream buf;
        for (size_t j = 0; j < top_shapes.size(); j++) {
            buf << VectorToString<int>(top_shapes.at(j));
        }
        ss << buf.str();

        if (NodeTypeUtils::IsInputNode(node)) {
            ss << std::endl;
            continue;
        }
        auto device_type = node->GetTopBlobDeviceType();
        ss << std::setw(16) << DeviceTypeToString(device_type) << std::endl;
    }

    ss << std::endl;
    ss << "output blobs " << VectorToString<std::string>(output_blob_names) << std::endl;

    return ss.str();
}

}  // namespace cosmo::nn
