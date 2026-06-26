#include "nn/device/sophon/sophon_net_node.h"

#include <algorithm>
#include <cstdio>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#include "bmruntime_interface.h"
#include "nn/device/sophon/sophon_node.h"
#include "nn/utils/string_format.h"

namespace cosmo::nn {

namespace {
    // Copy net output to CPU blob. For int8/uint8, dequantize to float using output_scale.
    Status CopyOutputToCpu(bm_handle_t pbmhandle, void* dst, const bm_device_mem_t& tensor_mem, size_t sz,
                           bm_data_type_t dtype, float scale) {
        if (dtype == BM_FLOAT32) {
            if (bm_memcpy_d2s_partial(pbmhandle, dst, tensor_mem, sz) != BM_SUCCESS) {
                return Status(COSMO_NN_ERR_SOPHON_MEMCPY_ERR, "Sophon memcpy d2s failed");
            }
            return COSMO_NN_OK;
        }
        if (dtype == BM_INT8) {
            std::vector<int8_t> tmp(sz);
            if (bm_memcpy_d2s_partial(pbmhandle, tmp.data(), tensor_mem, sz) != BM_SUCCESS) {
                return Status(COSMO_NN_ERR_SOPHON_MEMCPY_ERR, "Sophon memcpy d2s failed");
            }
            float* out = reinterpret_cast<float*>(dst);
            for (size_t i = 0; i < sz; ++i) {
                out[i] = tmp[i] * scale;
            }
            return COSMO_NN_OK;
        }
        if (dtype == BM_UINT8) {
            std::vector<uint8_t> tmp(sz);
            if (bm_memcpy_d2s_partial(pbmhandle, tmp.data(), tensor_mem, sz) != BM_SUCCESS) {
                return Status(COSMO_NN_ERR_SOPHON_MEMCPY_ERR, "Sophon memcpy d2s failed");
            }
            float* out = reinterpret_cast<float*>(dst);
            for (size_t i = 0; i < sz; ++i) {
                out[i] = tmp[i] * scale;
            }
            return COSMO_NN_OK;
        }
        // Fallback for float16/bf16: direct copy (may need proper conversion in future)
        if (bm_memcpy_d2s_partial(pbmhandle, dst, tensor_mem, sz) != BM_SUCCESS) {
            return Status(COSMO_NN_ERR_SOPHON_MEMCPY_ERR, "Sophon memcpy d2s failed");
        }
        return COSMO_NN_OK;
    }
}  // namespace

std::mutex SophonNetNode::sophon_net_mutex;

SophonNetNode::SophonNetNode() : NetNode() {}

SophonNetNode::~SophonNetNode() {
    // bmrt_destroy's KernelModule::~KernelModule() may throw std::runtime_error
    // on ioctl failure (BMRuntime SDK defect). An escaping exception in a destructor
    // causes std::terminate -> SIGABRT, so we must catch all exceptions here.
    try {
        // Free output tensor device memory first while pbmhandle is still valid.
        // Use cached m_output_num instead of m_netinfo->output_num because
        // m_netinfo becomes a dangling pointer after bmrt_destroy.
        if (!output_tensors.empty() && pbmhandle != nullptr) {
            for (int i = 0; i < m_output_num; ++i) {
                if (output_tensors[i].device_mem.size != 0) {
                    bm_free_device(pbmhandle, output_tensors[i].device_mem);
                }
            }
        }
        output_tensors.clear();

        // Then destroy bmrt (releases bmodel weight memory on device).
        if (m_bmrt != NULL) {
            bmrt_destroy(m_bmrt);
            m_bmrt = NULL;
        }
        input_tensors.clear();
    } catch (const std::exception& e) {
        fprintf(stderr, "[SophonNetNode] destructor exception caught: %s\n", e.what());
        output_tensors.clear();
        input_tensors.clear();
        m_bmrt = NULL;
    } catch (...) {
        fprintf(stderr, "[SophonNetNode] destructor unknown exception caught\n");
        output_tensors.clear();
        input_tensors.clear();
        m_bmrt = NULL;
    }
}

void SophonNetNode::ModelDescInfo(const bm_net_info_t* modelDesc) {
    if (modelDesc == nullptr) {
        return;
    }

    size_t inputNum  = modelDesc->input_num;
    size_t outputNum = modelDesc->output_num;

    for (size_t i = 0; i < inputNum; ++i) {
        const char* name = modelDesc->input_names[i];
        if (name == nullptr) {
            continue;
        }
        auto ret = modelDesc->stages->input_shapes[i];
        std::vector<int> inPut;
        for (size_t j = 0; j < ret.num_dims; ++j) {
            inPut.push_back(ret.dims[j]);
        }
        netInfo_.inPuts.push_back(inPut);
    }

    for (size_t i = 0; i < outputNum; ++i) {
        const char* name = modelDesc->output_names[i];
        if (name == nullptr) {
            continue;
        }
        auto ret = modelDesc->stages->output_shapes[i];
        std::vector<int> outPut;
        for (size_t j = 0; j < ret.num_dims; ++j) {
            outPut.push_back(ret.dims[j]);
        }
        netInfo_.outPuts.push_back(outPut);
    }

    input_tensors.resize(m_netinfo->input_num);
    for (int i = 0; i < m_netinfo->input_num; ++i) {
        input_tensors[i].dtype      = m_netinfo->input_dtypes[i];
        input_tensors[i].shape      = m_netinfo->stages[0].input_shapes[i];
        input_tensors[i].st_mode    = BM_STORE_1N;
        input_tensors[i].device_mem = bm_mem_null();
    }

    output_tensors.resize(m_netinfo->output_num);
    for (int i = 0; i < m_netinfo->output_num; ++i) {
        output_tensors[i].dtype   = m_netinfo->output_dtypes[i];
        output_tensors[i].shape   = m_netinfo->stages[0].output_shapes[i];
        output_tensors[i].st_mode = BM_STORE_1N;
    }

    showInfo();
}

void SophonNetNode::showInfo() {
    // Reserved for optional model info dump
}

Status SophonNetNode::AllocateOutputDeviceMemory() {
    static constexpr size_t kMaxOutputTensorBytes = 512U * 1024 * 1024;
    for (int i = 0; i < m_netinfo->output_num; i++) {
        size_t output_tensor_size = 0;
        for (int j = 0; j < m_netinfo->stage_num; j++) {
            output_tensor_size += bmrt_shape_count(&m_netinfo->stages[j].output_shapes[i]);
        }

        output_tensor_size *= bmruntime::ByteSize(m_netinfo->output_dtypes[i]);
        if (output_tensor_size == 0 || output_tensor_size > kMaxOutputTensorBytes) {
            // Roll back: free already-allocated output tensors [0..i-1]
            for (int k = 0; k < i; k++) {
                if (output_tensors[k].device_mem.size != 0) {
                    bm_free_device(pbmhandle, output_tensors[k].device_mem);
                    output_tensors[k].device_mem = bm_mem_null();
                }
            }
            return Status(COSMO_NN_ERR_SOPHON_ALLOC_MEM_FAILED, "output tensor size invalid (0 or > 512MB)");
        }
        bm_status_t ret = bm_malloc_device_byte_heap(pbmhandle, &output_tensors[i].device_mem, USE_MEM_HEAP0,
                                                     output_tensor_size);
        if (ret != BM_SUCCESS) {
            // Roll back: free already-allocated output tensors [0..i-1]
            for (int k = 0; k < i; k++) {
                if (output_tensors[k].device_mem.size != 0) {
                    bm_free_device(pbmhandle, output_tensors[k].device_mem);
                    output_tensors[k].device_mem = bm_mem_null();
                }
            }
            return Status(COSMO_NN_ERR_SOPHON_ALLOC_MEM_FAILED, "sophon alloc memory failed");
        }
        tensor_size_vec.push_back(output_tensor_size);
    }
    return COSMO_NN_OK;
}

Status SophonNetNode::SetupNetworkAfterBmrt() {
    const char** names = nullptr;
    int num            = bmrt_get_network_number(m_bmrt);
    bmrt_get_network_names(m_bmrt, &names);
    for (int i = 0; i < num; ++i) {
        m_network_names.push_back(names[i]);
    }
    // Do not free(names): some BMLib builds return a non-malloc'd pointer.

    m_netinfo = bmrt_get_network_info(m_bmrt, m_network_names.at(0).c_str());

    bottom_count = m_netinfo->input_num;
    top_count    = m_netinfo->output_num;

    if (m_netinfo->input_num > 0) {
        shared_resource->model_input_scale = m_netinfo->input_scales[0];
    }

    ModelDescInfo(m_netinfo);

    RETURN_ON_FAIL(AllocateOutputDeviceMemory());

    // Cache output_num for destructor (m_netinfo becomes dangling after bmrt_destroy).
    m_output_num = m_netinfo->output_num;

    return COSMO_NN_OK;
}

Status SophonNetNode::LoadWeight(const char* data, size_t size) {
    pbmhandle = shared_resource->m_handle;
    if (nullptr == pbmhandle)
        return Status(COSMO_NN_ERR_SOPHON_HANDLE_FAILED, "bm_handle_t is null !");

    std::unique_lock<std::mutex> lck(sophon_net_mutex);
    char* data_ = const_cast<char*>(data);  // Sophon BMLib C API requires non-const char*
    m_bmrt      = bmrt_create(pbmhandle);
    if (NULL == m_bmrt) {
        return Status(COSMO_NN_ERR_SOPHON_NET_CREATE_FAILED, "SOPHON bmrt_create create failed !");
    }

    if (!bmrt_load_bmodel_data(m_bmrt, data_, size)) {
        return Status(COSMO_NN_ERR_LOAD_MODEL, "SOPHON load model failed !");
    }

    return SetupNetworkAfterBmrt();
}

Status SophonNetNode::AttachBmrt(void* bmrt_handle) {
    if (!bmrt_handle)
        return Status(COSMO_NN_ERR_LOAD_MODEL, "AttachBmrt: bmrt handle is null");

    pbmhandle = shared_resource->m_handle;
    if (nullptr == pbmhandle)
        return Status(COSMO_NN_ERR_SOPHON_HANDLE_FAILED, "bm_handle_t is null!");

    std::unique_lock<std::mutex> lck(sophon_net_mutex);

    // Take ownership of the externally-created bmrt.
    // The .so has already completed bmrt_create + bmrt_load_bmodel_data.
    m_bmrt = bmrt_handle;

    return SetupNetworkAfterBmrt();
}

Status SophonNetNode::InferTopShapes() {
    // get output dims
    size_t outputNum = m_netinfo->output_num;
    for (size_t i = 0; i < outputNum; ++i) {
        int dataType = m_netinfo->output_dtypes[i];
        auto ret     = m_netinfo->stages->output_shapes[i];

        DimsVector outPutDims;
        for (size_t j = 0; j < ret.num_dims; ++j) {
            outPutDims.push_back(ret.dims[j]);
        }
        top_blob_shapes.push_back(outPutDims);
        // When output_to_cpu_, host postprocess (e.g. yolov8_decode) expects float.
        // Force float type so blob is allocated for float; int8/uint8 will be dequantized in Forward.
        DataType out_type =
            output_to_cpu_ ? DATA_TYPE_FLOAT : FromTensorDataType(m_netinfo->output_dtypes[i]);
        top_blob_data_types.push_back(out_type);
    }

    return COSMO_NN_OK;
}

DeviceType SophonNetNode::GetTopBlobDeviceType() {
    return output_to_cpu_ ? DeviceType::DEVICE_NAIVE : DeviceType::DEVICE_SOPHON_TPU;
}

size_t SophonNetNode::GetTopCount() {
    return top_count;
}

size_t SophonNetNode::GetBottomCount() {
    return bottom_count;
}

DataType SophonNetNode::FromTensorDataType(bm_data_type_t data_type) {
    switch (data_type) {
        case BM_FLOAT32:
            return DATA_TYPE_FLOAT;
        case BM_FLOAT16:
            return DATA_TYPE_FLOAT;
        case BM_INT8:
            return DATA_TYPE_INT8;
        case BM_UINT8:
            return DATA_TYPE_UINT8;

        case BM_BFLOAT16:
            return DATA_TYPE_BFP16;
    }

    return DATA_TYPE_FLOAT;
}

Status SophonNetNode::CopyOutputToBlob(const std::shared_ptr<Blob>& top_blob, int tensor_idx) {
    auto top_handle            = top_blob->GetHandle();
    auto top_desc              = top_blob->GetBlobDesc();
    size_t sz                  = tensor_size_vec.at(tensor_idx);
    bm_device_mem_t tensor_mem = output_tensors[tensor_idx].device_mem;
    float scale                = (m_netinfo->output_scales && tensor_idx < m_netinfo->output_num)
                                     ? m_netinfo->output_scales[tensor_idx]
                                     : 1.0f;
    if (top_desc.device_type == DEVICE_NAIVE) {
        RETURN_ON_FAIL(CopyOutputToCpu(pbmhandle, top_handle.base, tensor_mem, sz,
                                       m_netinfo->output_dtypes[tensor_idx], scale));
    } else {
        bm_device_mem_t* dst_dev_mem = reinterpret_cast<bm_device_mem_t*>(top_handle.base);
        if (bm_memcpy_d2d_byte(pbmhandle, *dst_dev_mem, 0, tensor_mem, 0, sz) != BM_SUCCESS) {
            return Status(COSMO_NN_ERR_SOPHON_MEMCPY_ERR, "Sophon memcpy d2d failed");
        }
    }
    return COSMO_NN_OK;
}

Status SophonNetNode::Forward(std::vector<std::shared_ptr<Blob>>& bottom_blobs,
                              std::vector<std::shared_ptr<Blob>>& top_blobs) {
    bm_status_t ret = BM_SUCCESS;
    timer.Start();
    RETURN_ON_FAIL(CheckNodeInputOutput(bottom_blobs, top_blobs, !output_to_cpu_));

    // bm_handle_t pbmhandle = shared_resource->m_handle;
    if (nullptr == pbmhandle)
        return Status(COSMO_NN_ERR_SOPHON_HANDLE_FAILED, "bm_handle_t is null !");

    const int current_batch = bottom_blobs.at(0)->GetBlobDesc().dims.at(0);
    if (current_batch > max_batch)
        return Status(COSMO_NN_ERR_INVALID_INPUT, "batch size is bigger than max_batch size");

    batch_size = current_batch;

    // Set all input tensors from bottom_blobs
    if (bottom_blobs.size() != static_cast<size_t>(m_netinfo->input_num)) {
        return Status(COSMO_NN_ERR_INVALID_INPUT,
                      "bottom_blobs size (" + std::to_string(bottom_blobs.size()) +
                          ") does not match model input num (" + std::to_string(m_netinfo->input_num) + ")");
    }

    // Reorder bottom_blobs according to m_netinfo->input_names (actual model input order)
    // This handles cases where config file order differs from model file order
    std::vector<std::shared_ptr<Blob>> reordered_bottom_blobs;
    auto bottom_blob_names = GetBottomBlobNames();

    if (bottom_blob_names.size() == bottom_blobs.size() &&
        network_input_names.size() == bottom_blobs.size() &&
        network_input_names.size() == static_cast<size_t>(m_netinfo->input_num)) {
        // Create a map from config input name to bottom_blob
        std::map<std::string, std::shared_ptr<Blob>> name_to_blob;
        for (size_t i = 0; i < network_input_names.size() && i < bottom_blobs.size(); i++) {
            name_to_blob[network_input_names.at(i)] = bottom_blobs.at(i);
        }

        // Reorder according to m_netinfo->input_names (actual model order)
        bool need_reorder = false;
        for (int i = 0; i < m_netinfo->input_num; i++) {
            if (i < static_cast<int>(network_input_names.size()) &&
                network_input_names.at(i) != std::string(m_netinfo->input_names[i])) {
                need_reorder = true;
                break;
            }
        }

        if (need_reorder) {
            for (int i = 0; i < m_netinfo->input_num; i++) {
                std::string actual_name = m_netinfo->input_names[i];
                auto it                 = name_to_blob.find(actual_name);
                if (it != name_to_blob.end()) {
                    reordered_bottom_blobs.push_back(it->second);
                } else {
                    reordered_bottom_blobs = bottom_blobs;  // Fallback to original order
                    break;
                }
            }

            if (reordered_bottom_blobs.size() == bottom_blobs.size()) {
                bottom_blobs = reordered_bottom_blobs;
            }
        }
    }

    for (int i = 0; i < m_netinfo->input_num; i++) {
        auto bottom_blob             = bottom_blobs.at(i);
        auto bottom_handle           = bottom_blob->GetHandle();
        bm_device_mem_t* src_dev_mem = reinterpret_cast<bm_device_mem_t*>(bottom_handle.base);

        input_tensors[i].device_mem = *src_dev_mem;
    }

    bool infer_ret =
        bmrt_launch_tensor_ex(m_bmrt, m_netinfo->name, input_tensors.data(), m_netinfo->input_num,
                              output_tensors.data(), m_netinfo->output_num, true, false);
    if (!infer_ret) {
        return Status(COSMO_NN_ERR_SOPHON_INFER_ERR, "sophon infer failed");
    }
    ret = bm_thread_sync(pbmhandle);
    if (ret != BM_SUCCESS) {
        return Status(COSMO_NN_ERR_SOPHON_INFER_ERR, "sophon thread sync failed");
    }

    // Priority 0: SAM encoder (3 outputs). Match by shape like C++ demo, regardless of bmodel output order.
    if (m_netinfo->output_num == 3 && top_blobs.size() == 3 && network_output_names.size() == 3) {
        int high_res_0_bm = -1, high_res_1_bm = -1, image_embed_bm = -1;
        for (int i = 0; i < 3; i++) {
            const bm_shape_t& s = m_netinfo->stages[0].output_shapes[i];
            if (s.num_dims == 4) {
                if (s.dims[1] == 32 && s.dims[2] == 256 && s.dims[3] == 256)
                    high_res_0_bm = i;
                else if (s.dims[1] == 64 && s.dims[2] == 128 && s.dims[3] == 128)
                    high_res_1_bm = i;
                else if (s.dims[1] == 256 && s.dims[2] == 64 && s.dims[3] == 64)
                    image_embed_bm = i;
            }
        }
        int idx_hr0 = -1, idx_hr1 = -1, idx_emb = -1;
        for (size_t i = 0; i < network_output_names.size(); i++) {
            if (network_output_names[i] == "high_res_feats_0")
                idx_hr0 = static_cast<int>(i);
            else if (network_output_names[i] == "high_res_feats_1")
                idx_hr1 = static_cast<int>(i);
            else if (network_output_names[i] == "image_embed")
                idx_emb = static_cast<int>(i);
        }
        if (high_res_0_bm >= 0 && high_res_1_bm >= 0 && image_embed_bm >= 0 && idx_hr0 >= 0 && idx_hr1 >= 0 &&
            idx_emb >= 0) {
            auto copy_to = [this, &top_blobs](int top_idx, int bm_idx) {
                auto h               = top_blobs.at(top_idx)->GetHandle();
                bm_device_mem_t* dst = reinterpret_cast<bm_device_mem_t*>(h.base);
                return bm_memcpy_d2d_byte(pbmhandle, *dst, 0, output_tensors[bm_idx].device_mem, 0,
                                          this->tensor_size_vec.at(bm_idx)) == BM_SUCCESS;
            };
            if (copy_to(idx_hr0, high_res_0_bm) && copy_to(idx_hr1, high_res_1_bm) &&
                copy_to(idx_emb, image_embed_bm)) {
                timer.Stop();
                return COSMO_NN_OK;
            }
        }
    }

    // Priority 1: For nets with exactly 2 outputs where one is 4D and one is 2D (e.g. SAM decoder),
    // always copy by shape so that top_blobs[0]=masks (4D), top_blobs[1]=scores (2D).
    // This ensures copy_10/ copy_11 and sam_decode get [masks, scores] even when bmodel order or
    // name-matching logic would leave scores in the wrong blob.
    if (m_netinfo->output_num == 2 && top_blobs.size() == 2) {
        int masks_bmodel_idx  = -1;
        int scores_bmodel_idx = -1;
        for (int i = 0; i < 2; i++) {
            int nd = m_netinfo->stages[0].output_shapes[i].num_dims;
            if (nd == 4)
                masks_bmodel_idx = i;
            else if (nd == 2)
                scores_bmodel_idx = i;
        }
        if (masks_bmodel_idx >= 0 && scores_bmodel_idx >= 0) {
            auto t0             = top_blobs.at(0)->GetHandle();
            auto t1             = top_blobs.at(1)->GetHandle();
            bm_device_mem_t* d0 = reinterpret_cast<bm_device_mem_t*>(t0.base);
            bm_device_mem_t* d1 = reinterpret_cast<bm_device_mem_t*>(t1.base);
            if (bm_memcpy_d2d_byte(pbmhandle, *d0, 0, output_tensors[masks_bmodel_idx].device_mem, 0,
                                   tensor_size_vec.at(masks_bmodel_idx)) != BM_SUCCESS ||
                bm_memcpy_d2d_byte(pbmhandle, *d1, 0, output_tensors[scores_bmodel_idx].device_mem, 0,
                                   tensor_size_vec.at(scores_bmodel_idx)) != BM_SUCCESS) {
                return Status(COSMO_NN_ERR_SOPHON_MEMCPY_ERR, "Sophon memcpy d2d failed");
            }
            timer.Stop();
            return COSMO_NN_OK;
        }
    }

    // Copy output_tensors to top_blobs. If bmodel output order differs from config (network_output_names),
    // reorder so that top_blobs[i] receives the tensor whose name matches network_output_names[i].
    if (!network_output_names.empty() &&
        network_output_names.size() == static_cast<size_t>(m_netinfo->output_num) &&
        network_output_names.size() == top_blobs.size()) {
        bool need_reorder = false;
        for (size_t i = 0; i < network_output_names.size(); i++) {
            if (std::string(m_netinfo->output_names[i]) != network_output_names[i]) {
                need_reorder = true;
                break;
            }
        }
        if (need_reorder) {
            for (size_t config_idx = 0; config_idx < network_output_names.size(); config_idx++) {
                const std::string& want_name = network_output_names[config_idx];
                int bmodel_idx               = -1;
                for (int j = 0; j < m_netinfo->output_num; j++) {
                    if (want_name == std::string(m_netinfo->output_names[j])) {
                        bmodel_idx = j;
                        break;
                    }
                }
                if (bmodel_idx < 0) {
                    bmodel_idx = static_cast<int>(config_idx);
                }
                RETURN_ON_FAIL(CopyOutputToBlob(top_blobs.at(config_idx), bmodel_idx));
            }
        } else {
            for (int i = 0; i < m_netinfo->output_num; i++) {
                RETURN_ON_FAIL(CopyOutputToBlob(top_blobs.at(i), i));
            }
        }
    } else {
        for (int i = 0; i < m_netinfo->output_num; i++) {
            RETURN_ON_FAIL(CopyOutputToBlob(top_blobs.at(i), i));
        }
    }

    timer.Stop();
    return COSMO_NN_OK;
}

}  // namespace cosmo::nn