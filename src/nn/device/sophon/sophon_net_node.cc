#include "nn/device/sophon/sophon_net_node.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <map>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <vector>

#include "bmruntime_interface.h"
#include "nn/device/sophon/sophon_node.h"
#include "nn/utils/blob_memory_size_info.h"
#include "nn/utils/blob_memory_size_utils.h"
#include "nn/utils/string_format.h"

namespace cosmo::nn {

namespace {
    constexpr size_t kMaxOutputTensorBytes = 512U * 1024 * 1024;

    struct NetworkNamesDeleter {
        void operator()(const char** names) const noexcept {
            std::free(names);
        }
    };

    bool GetTensorByteSize(const bm_shape_t& shape, bm_data_type_t dtype, size_t* byte_size) {
        if (byte_size == nullptr || shape.num_dims <= 0 || shape.num_dims > BM_MAX_DIMS_NUM) {
            return false;
        }

        const int element_bytes = bmruntime::ByteSize(dtype);
        if (element_bytes <= 0) {
            return false;
        }

        size_t count = 1;
        for (int i = 0; i < shape.num_dims; ++i) {
            const int dim = shape.dims[i];
            if (dim <= 0 || count > kMaxOutputTensorBytes / static_cast<size_t>(dim)) {
                return false;
            }
            count *= static_cast<size_t>(dim);
        }
        if (count > kMaxOutputTensorBytes / static_cast<size_t>(element_bytes)) {
            return false;
        }

        *byte_size = count * static_cast<size_t>(element_bytes);
        return *byte_size > 0 && *byte_size <= kMaxOutputTensorBytes;
    }

    float Float16ToFloat(uint16_t value) {
        const uint32_t sign     = static_cast<uint32_t>(value & 0x8000U) << 16U;
        const uint32_t exponent = (value >> 10U) & 0x1fU;
        const uint32_t mantissa = value & 0x03ffU;
        uint32_t bits           = 0;
        if (exponent == 0) {
            if (mantissa == 0) {
                bits = sign;
            } else {
                uint32_t normalized_mantissa = mantissa;
                int shift                    = 0;
                while ((normalized_mantissa & 0x0400U) == 0U) {
                    normalized_mantissa <<= 1U;
                    ++shift;
                }
                normalized_mantissa &= 0x03ffU;
                const uint32_t normalized_exponent = static_cast<uint32_t>(127 - 14 - shift);
                bits = sign | (normalized_exponent << 23U) | (normalized_mantissa << 13U);
            }
        } else if (exponent == 0x1fU) {
            bits = sign | 0x7f800000U | (mantissa << 13U);
        } else {
            bits = sign | ((exponent + (127U - 15U)) << 23U) | (mantissa << 13U);
        }

        float result = 0.0F;
        std::memcpy(&result, &bits, sizeof(result));
        return result;
    }

    float BFloat16ToFloat(uint16_t value) {
        const uint32_t bits = static_cast<uint32_t>(value) << 16U;
        float result        = 0.0F;
        std::memcpy(&result, &bits, sizeof(result));
        return result;
    }

    template <typename T, typename Converter>
    void ConvertToFloat(const uint8_t* source, size_t count, float scale, float* destination,
                        Converter converter) {
        for (size_t i = 0; i < count; ++i) {
            T value{};
            std::memcpy(&value, source + i * sizeof(T), sizeof(T));
            destination[i] = static_cast<float>(converter(value)) * scale;
        }
    }

    void DestroyBmRuntime(void** runtime) noexcept {
        if (runtime == nullptr || *runtime == nullptr) {
            return;
        }
        try {
            bmrt_destroy(*runtime);
        } catch (const std::exception& e) {
            fprintf(stderr, "[SophonNetNode] bmrt_destroy exception caught: %s\n", e.what());
        } catch (...) {
            fprintf(stderr, "[SophonNetNode] bmrt_destroy unknown exception caught\n");
        }
        *runtime = nullptr;
    }

    // Copy net output to CPU blob. For int8/uint8, dequantize to float using output_scale.
    Status CopyOutputToCpu(bm_handle_t pbmhandle, void* dst, size_t dst_size,
                           const bm_device_mem_t& tensor_mem, size_t sz, bm_data_type_t dtype, float scale) {
        if (dst == nullptr || sz == 0 || bm_mem_get_device_size(tensor_mem) < sz) {
            return Status(COSMO_NN_ERR_INVALID_INPUT, "Sophon output buffer is invalid");
        }
        if (dtype == BM_FLOAT32) {
            if (dst_size < sz) {
                return Status(COSMO_NN_ERR_INVALID_INPUT, "Sophon CPU output buffer is too small");
            }
            if (bm_memcpy_d2s_partial(pbmhandle, dst, tensor_mem, static_cast<unsigned int>(sz)) !=
                BM_SUCCESS) {
                return Status(COSMO_NN_ERR_SOPHON_MEMCPY_ERR, "Sophon memcpy d2s failed");
            }
            return Status{COSMO_NN_OK};
        }

        const size_t source_element_size = static_cast<size_t>(bmruntime::ByteSize(dtype));
        if (source_element_size == 0 || (sz % source_element_size) != 0) {
            return Status(COSMO_NN_ERR_INVALID_INPUT, "Sophon output data type is unsupported");
        }
        const size_t element_count = sz / source_element_size;
        if (element_count > std::numeric_limits<size_t>::max() / sizeof(float) ||
            dst_size < element_count * sizeof(float)) {
            return Status(COSMO_NN_ERR_INVALID_INPUT, "Sophon CPU output buffer is too small");
        }

        std::vector<uint8_t> tmp;
        try {
            tmp.resize(sz);
        } catch (const std::bad_alloc&) {
            return Status(COSMO_NN_ERR_OUT_OF_MEMORY, "Sophon CPU output conversion allocation failed");
        }
        if (bm_memcpy_d2s_partial(pbmhandle, tmp.data(), tensor_mem, static_cast<unsigned int>(sz)) !=
            BM_SUCCESS) {
            return Status(COSMO_NN_ERR_SOPHON_MEMCPY_ERR, "Sophon memcpy d2s failed");
        }

        auto* out = static_cast<float*>(dst);
        switch (dtype) {
            case BM_FLOAT16:
                ConvertToFloat<uint16_t>(tmp.data(), element_count, 1.0F, out, Float16ToFloat);
                return Status{COSMO_NN_OK};
            case BM_BFLOAT16:
                ConvertToFloat<uint16_t>(tmp.data(), element_count, 1.0F, out, BFloat16ToFloat);
                return Status{COSMO_NN_OK};
            case BM_INT8:
                ConvertToFloat<int8_t>(tmp.data(), element_count, scale, out,
                                       [](int8_t value) { return value; });
                return Status{COSMO_NN_OK};
            case BM_UINT8:
                ConvertToFloat<uint8_t>(tmp.data(), element_count, scale, out,
                                        [](uint8_t value) { return value; });
                return Status{COSMO_NN_OK};
            case BM_INT32:
                ConvertToFloat<int32_t>(tmp.data(), element_count, scale, out,
                                        [](int32_t value) { return value; });
                return Status{COSMO_NN_OK};
            default:
                return Status(COSMO_NN_ERR_INVALID_INPUT, "Sophon output data type is unsupported");
        }
    }
}  // namespace

void BmrtDeleter::operator()(void* runtime) const noexcept {
    DestroyBmRuntime(&runtime);
}

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
            const int output_count = std::min(m_output_num, static_cast<int>(output_tensors.size()));
            for (int i = 0; i < output_count; ++i) {
                if (output_tensors[i].device_mem.size != 0) {
                    (void)bm_free_device(pbmhandle, output_tensors[i].device_mem);
                    output_tensors[i].device_mem = bm_mem_null();
                }
            }
        }
        output_tensors.clear();
        tensor_size_vec.clear();
        m_output_num = 0;

        // Then destroy bmrt (releases bmodel weight memory on device).
        if (m_bmrt != NULL) {
            DestroyBmRuntime(&m_bmrt);
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
    netInfo_.inPuts.clear();
    netInfo_.outPuts.clear();

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
        output_tensors[i].dtype      = m_netinfo->output_dtypes[i];
        output_tensors[i].shape      = m_netinfo->stages[0].output_shapes[i];
        output_tensors[i].st_mode    = BM_STORE_1N;
        output_tensors[i].device_mem = bm_mem_null();
    }

    showInfo();
}

void SophonNetNode::showInfo() {
    // Reserved for optional model info dump
}

Status SophonNetNode::AllocateOutputDeviceMemory() {
    if (m_netinfo == nullptr || m_netinfo->stage_num <= 0 || m_netinfo->stages == nullptr ||
        m_netinfo->output_num <= 0 || output_tensors.size() != static_cast<size_t>(m_netinfo->output_num)) {
        return Status(COSMO_NN_ERR_INVALID_INPUT, "Sophon network output metadata is invalid");
    }

    tensor_size_vec.clear();
    tensor_capacity_vec_.clear();
    tensor_size_vec.reserve(static_cast<size_t>(m_netinfo->output_num));
    for (int i = 0; i < m_netinfo->output_num; i++) {
        size_t output_tensor_size = 0;
        // Allocate the selected stage only; dynamic networks additionally need their maximum capacity.
        if (!GetTensorByteSize(m_netinfo->stages[selected_stage_].output_shapes[i],
                               m_netinfo->output_dtypes[i], &output_tensor_size)) {
            // Roll back: free already-allocated output tensors [0..i-1]
            for (int k = 0; k < i; k++) {
                if (output_tensors[k].device_mem.size != 0) {
                    (void)bm_free_device(pbmhandle, output_tensors[k].device_mem);
                    output_tensors[k].device_mem = bm_mem_null();
                }
            }
            tensor_size_vec.clear();
            return Status(COSMO_NN_ERR_SOPHON_ALLOC_MEM_FAILED, "output tensor size is invalid");
        }
        if (!input_sizes_.empty() && m_netinfo->is_dynamic) {
            if (m_netinfo->max_output_bytes == nullptr || m_netinfo->max_output_bytes[i] == 0 ||
                m_netinfo->max_output_bytes[i] > kMaxOutputTensorBytes)
                return Status(COSMO_NN_ERR_SOPHON_ALLOC_MEM_FAILED, "Invalid dynamic output capacity");
            output_tensor_size = std::max(output_tensor_size, m_netinfo->max_output_bytes[i]);
        }
        bm_status_t ret = bm_malloc_device_byte_heap(pbmhandle, &output_tensors[i].device_mem, USE_MEM_HEAP0,
                                                     static_cast<unsigned int>(output_tensor_size));
        if (ret != BM_SUCCESS) {
            // Roll back: free already-allocated output tensors [0..i-1]
            for (int k = 0; k < i; k++) {
                if (output_tensors[k].device_mem.size != 0) {
                    bm_free_device(pbmhandle, output_tensors[k].device_mem);
                    output_tensors[k].device_mem = bm_mem_null();
                }
            }
            tensor_size_vec.clear();
            return Status(COSMO_NN_ERR_SOPHON_ALLOC_MEM_FAILED, "sophon alloc memory failed");
        }
        tensor_size_vec.push_back(static_cast<uint32_t>(output_tensor_size));
        tensor_capacity_vec_.push_back(output_tensor_size);
    }
    return Status{COSMO_NN_OK};
}

Status SophonNetNode::SetupNetworkAfterBmrt() {
    if (m_bmrt == nullptr) {
        return Status(COSMO_NN_ERR_SOPHON_NET_CREATE_FAILED, "Sophon runtime is null");
    }

    const char** names = nullptr;
    int num            = bmrt_get_network_number(m_bmrt);
    if (num <= 0) {
        return Status(COSMO_NN_ERR_LOAD_MODEL, "Sophon model has no network");
    }
    bmrt_get_network_names(m_bmrt, &names);
    if (names == nullptr) {
        return Status(COSMO_NN_ERR_LOAD_MODEL, "Sophon network names are unavailable");
    }
    std::unique_ptr<const char*, NetworkNamesDeleter> names_guard(names);
    m_network_names.clear();
    for (int i = 0; i < num; ++i) {
        if (names[i] != nullptr && names[i][0] != '\0') {
            m_network_names.emplace_back(names[i]);
        }
    }
    if (m_network_names.empty()) {
        return Status(COSMO_NN_ERR_LOAD_MODEL, "Sophon model has no valid network name");
    }

    m_netinfo = bmrt_get_network_info(m_bmrt, m_network_names.front().c_str());
    if (m_netinfo == nullptr || m_netinfo->stages == nullptr || m_netinfo->stage_num <= 0 ||
        m_netinfo->input_num <= 0 || m_netinfo->output_num <= 0 || m_netinfo->input_names == nullptr ||
        m_netinfo->output_names == nullptr || m_netinfo->input_dtypes == nullptr ||
        m_netinfo->output_dtypes == nullptr || m_netinfo->name == nullptr ||
        m_netinfo->stages[0].input_shapes == nullptr || m_netinfo->stages[0].output_shapes == nullptr ||
        m_netinfo->stages[0].input_mems == nullptr || m_netinfo->stages[0].output_mems == nullptr) {
        return Status(COSMO_NN_ERR_LOAD_MODEL, "Sophon network metadata is invalid");
    }
    for (int i = 0; i < m_netinfo->input_num; ++i) {
        const auto& shape = m_netinfo->stages[0].input_shapes[i];
        if (m_netinfo->input_names[i] == nullptr || shape.num_dims <= 0 || shape.num_dims > BM_MAX_DIMS_NUM) {
            return Status(COSMO_NN_ERR_LOAD_MODEL, "Sophon input metadata is invalid");
        }
    }
    for (int i = 0; i < m_netinfo->output_num; ++i) {
        const auto& shape = m_netinfo->stages[0].output_shapes[i];
        if (m_netinfo->output_names[i] == nullptr || shape.num_dims <= 0 ||
            shape.num_dims > BM_MAX_DIMS_NUM) {
            return Status(COSMO_NN_ERR_LOAD_MODEL, "Sophon output metadata is invalid");
        }
    }

    bottom_count = m_netinfo->input_num;
    top_count    = m_netinfo->output_num;

    if (m_netinfo->input_num > 0) {
        shared_resource->model_input_scale =
            m_netinfo->input_scales != nullptr ? m_netinfo->input_scales[0] : 1.0F;
    }

    ModelDescInfo(m_netinfo);
    // Cache output_num for destructor (m_netinfo becomes dangling after bmrt_destroy).
    m_output_num = m_netinfo->output_num;
    RETURN_ON_FAIL(PrepareInputSize());
    RETURN_ON_FAIL(AllocateOutputDeviceMemory());
    if (!input_sizes_.empty() && m_netinfo->is_dynamic)
        RETURN_ON_FAIL(ProbeInputSize());

    return Status{COSMO_NN_OK};
}

Status SophonNetNode::PrepareInputSize() {
    if (input_sizes_.empty())
        return Status{COSMO_NN_OK};
    if (m_netinfo->input_num != 1 || m_netinfo->output_num != 1)
        return Status(COSMO_NN_ERR_INVALID_CFG, "Three input sizes require a single-input YOLO26 bmodel");
    if (!m_netinfo->is_dynamic) {
        for (const auto& size : input_sizes_) {
            int matched_stage = -1;
            for (int stage = 0; stage < m_netinfo->stage_num; ++stage) {
                const auto& info = m_netinfo->stages[stage];
                if (info.input_shapes == nullptr || info.output_shapes == nullptr)
                    return Status(COSMO_NN_ERR_INVALID_CFG, "Invalid bmodel stage metadata");
                const auto& shape = info.input_shapes[0];
                if (shape.num_dims == 4 && shape.dims[0] == 1 && shape.dims[1] == 3 &&
                    shape.dims[2] == size.height && shape.dims[3] == size.width)
                    matched_stage = stage;
            }
            if (matched_stage < 0)
                return Status(COSMO_NN_ERR_INVALID_CFG, "Declared input size is absent from bmodel stages");
            if (size == selected_input_size_)
                selected_stage_ = matched_stage;
        }
        input_tensors[0].shape  = m_netinfo->stages[selected_stage_].input_shapes[0];
        output_tensors[0].shape = m_netinfo->stages[selected_stage_].output_shapes[0];
        selected_output_shapes_ = {output_tensors[0].shape};
        return Status{COSMO_NN_OK};
    }
    const auto& limit = m_netinfo->stages[0].input_shapes[0];
    if (limit.num_dims != 4 || limit.dims[0] != 1 || limit.dims[1] != 3)
        return Status(COSMO_NN_ERR_INVALID_CFG, "Dynamic detector input must be NCHW with batch 1");
    for (const auto& size : input_sizes_) {
        if (size.height > limit.dims[2] || size.width > limit.dims[3])
            return Status(COSMO_NN_ERR_INVALID_CFG, "Declared input size exceeds the bmodel shape limit");
    }
    input_tensors[0].shape.dims[2] = selected_input_size_.height;
    input_tensors[0].shape.dims[3] = selected_input_size_.width;
    return Status{COSMO_NN_OK};
}

Status SophonNetNode::ProbeInputSize() {
    struct ProbeMemory {
        bm_handle_t handle;
        bm_device_mem_t memory{bm_mem_null()};
        ~ProbeMemory() {
            if (memory.size != 0)
                (void)bm_free_device(handle, memory);
        }
    } probe{pbmhandle};
    size_t bytes = 0;
    if (!GetTensorByteSize(input_tensors[0].shape, input_tensors[0].dtype, &bytes) ||
        bm_malloc_device_byte_heap(pbmhandle, &probe.memory, USE_MEM_HEAP0,
                                   static_cast<unsigned int>(bytes)) != BM_SUCCESS)
        return Status(COSMO_NN_ERR_SOPHON_ALLOC_MEM_FAILED, "Dynamic shape probe allocation failed");
    if (bm_memset_device(pbmhandle, 0, probe.memory) != BM_SUCCESS)
        return Status(COSMO_NN_ERR_SOPHON_MEMCPY_ERR, "Dynamic shape probe initialization failed");
    auto input       = input_tensors[0];
    input.device_mem = probe.memory;
    if (!bmrt_launch_tensor_ex(m_bmrt, m_netinfo->name, &input, 1, output_tensors.data(),
                               m_netinfo->output_num, true, false) ||
        bm_thread_sync(pbmhandle) != BM_SUCCESS)
        return Status(COSMO_NN_ERR_SOPHON_INFER_ERR, "Dynamic shape probe inference failed");
    for (const auto& output : output_tensors)
        selected_output_shapes_.push_back(output.shape);
    RETURN_ON_FAIL(CheckInputSizeOutputs());
    return Status{COSMO_NN_OK};
}

Status SophonNetNode::CheckInputSizeOutputs() {
    for (size_t i = 0; i < output_tensors.size(); ++i) {
        size_t bytes = 0;
        if (!bmrt_shape_is_same(&output_tensors[i].shape, &selected_output_shapes_[i]) ||
            !GetTensorByteSize(output_tensors[i].shape, output_tensors[i].dtype, &bytes) ||
            bytes > tensor_capacity_vec_[i])
            return Status(COSMO_NN_ERR_INVALID_INPUT, "Dynamic output shape or capacity changed");
        tensor_size_vec[i] = static_cast<uint32_t>(bytes);
    }
    return Status{COSMO_NN_OK};
}

Status SophonNetNode::LoadWeight(const char* data, size_t size) {
    if (shared_resource == nullptr) {
        return Status(COSMO_NN_ERR_GRAPH_NOT_INIT, "Sophon shared resource is null");
    }
    pbmhandle = shared_resource->m_handle;
    if (nullptr == pbmhandle)
        return Status(COSMO_NN_ERR_SOPHON_HANDLE_FAILED, "bm_handle_t is null !");

    std::unique_lock<std::mutex> lck(sophon_net_mutex);
    if (data == nullptr || size == 0) {
        return Status(COSMO_NN_ERR_INVALID_INPUT, "Sophon model data is empty");
    }
    if (m_bmrt != nullptr) {
        return Status(COSMO_NN_ERR_LOAD_MODEL, "Sophon model is already loaded");
    }

    char* data_ = const_cast<char*>(data);  // Sophon BMLib C API requires non-const char*
    m_bmrt      = bmrt_create(pbmhandle);
    if (NULL == m_bmrt) {
        return Status(COSMO_NN_ERR_SOPHON_NET_CREATE_FAILED, "SOPHON bmrt_create create failed !");
    }

    if (!bmrt_load_bmodel_data(m_bmrt, data_, size)) {
        DestroyBmRuntime(&m_bmrt);
        return Status(COSMO_NN_ERR_LOAD_MODEL, "SOPHON load model failed !");
    }

    try {
        Status status = SetupNetworkAfterBmrt();
        if (!bool(status)) {
            DestroyBmRuntime(&m_bmrt);
            m_netinfo = nullptr;
        }
        return status;
    } catch (const std::bad_alloc&) {
        DestroyBmRuntime(&m_bmrt);
        m_netinfo = nullptr;
        return Status(COSMO_NN_ERR_OUT_OF_MEMORY, "Sophon model setup ran out of memory");
    } catch (const std::exception& error) {
        DestroyBmRuntime(&m_bmrt);
        m_netinfo = nullptr;
        return Status(COSMO_NN_ERR_LOAD_MODEL, error.what());
    }
}

Status SophonNetNode::AttachOwnedBmrt(OwnedBmrt bmrt_handle) {
    if (!bmrt_handle)
        return Status(COSMO_NN_ERR_LOAD_MODEL, "AttachOwnedBmrt: bmrt handle is null");

    if (shared_resource == nullptr) {
        return Status(COSMO_NN_ERR_GRAPH_NOT_INIT, "Sophon shared resource is null");
    }
    pbmhandle = shared_resource->m_handle;
    if (nullptr == pbmhandle)
        return Status(COSMO_NN_ERR_SOPHON_HANDLE_FAILED, "bm_handle_t is null!");

    std::unique_lock<std::mutex> lck(sophon_net_mutex);

    if (m_bmrt != nullptr) {
        return Status(COSMO_NN_ERR_LOAD_MODEL, "AttachOwnedBmrt: runtime is already attached");
    }

    m_bmrt = bmrt_handle.release();

    try {
        Status status = SetupNetworkAfterBmrt();
        if (!bool(status)) {
            DestroyBmRuntime(&m_bmrt);
            m_netinfo = nullptr;
        }
        return status;
    } catch (const std::bad_alloc&) {
        DestroyBmRuntime(&m_bmrt);
        m_netinfo = nullptr;
        return Status(COSMO_NN_ERR_OUT_OF_MEMORY, "Sophon model setup ran out of memory");
    } catch (const std::exception& error) {
        DestroyBmRuntime(&m_bmrt);
        m_netinfo = nullptr;
        return Status(COSMO_NN_ERR_LOAD_MODEL, error.what());
    } catch (...) {
        DestroyBmRuntime(&m_bmrt);
        m_netinfo = nullptr;
        return Status(COSMO_NN_ERR_LOAD_MODEL, "Sophon model setup failed with an unknown error");
    }
}

Status SophonNetNode::InferTopShapes() {
    if (m_netinfo == nullptr || m_netinfo->stages == nullptr || m_netinfo->stage_num <= 0) {
        return Status(COSMO_NN_ERR_GRAPH_NOT_INIT, "Sophon network metadata is unavailable");
    }

    top_blob_shapes.clear();
    top_blob_data_types.clear();

    // get output dims
    size_t outputNum = m_netinfo->output_num;
    for (size_t i = 0; i < outputNum; ++i) {
        int dataType = m_netinfo->output_dtypes[i];
        auto ret     = input_sizes_.empty() ? m_netinfo->stages->output_shapes[i] : output_tensors[i].shape;

        DimsVector outPutDims;
        for (size_t j = 0; j < ret.num_dims; ++j) {
            outPutDims.push_back(ret.dims[j]);
        }
        top_blob_shapes.push_back(outPutDims);
        // When output_to_cpu_, host postprocess (e.g. yolov8_decode) expects float.
        // Force float type so blob is allocated for float; int8/uint8 will be dequantized in Forward.
        const auto output_dtype    = m_netinfo->output_dtypes[i];
        const bool supported_dtype = output_dtype == BM_FLOAT32 || output_dtype == BM_FLOAT16 ||
                                     output_dtype == BM_BFLOAT16 || output_dtype == BM_INT8 ||
                                     output_dtype == BM_UINT8 || output_dtype == BM_INT32;
        if (!supported_dtype) {
            return Status(COSMO_NN_ERR_INVALID_INPUT, "Sophon output data type is unsupported");
        }
        DataType out_type = output_to_cpu_ ? DATA_TYPE_FLOAT : FromTensorDataType(output_dtype);
        top_blob_data_types.push_back(out_type);
    }

    return Status{COSMO_NN_OK};
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
            return DATA_TYPE_HALF;
        case BM_INT8:
            return DATA_TYPE_INT8;
        case BM_UINT8:
            return DATA_TYPE_UINT8;
        case BM_INT32:
            return DATA_TYPE_INT32;

        case BM_BFLOAT16:
            return DATA_TYPE_BFP16;
    }

    return DATA_TYPE_FLOAT;
}

Status SophonNetNode::CopyOutputToBlob(const std::shared_ptr<Blob>& top_blob, int tensor_idx) {
    if (!top_blob || tensor_idx < 0 || tensor_idx >= m_output_num ||
        static_cast<size_t>(tensor_idx) >= tensor_size_vec.size() ||
        static_cast<size_t>(tensor_idx) >= output_tensors.size()) {
        return Status(COSMO_NN_ERR_INVALID_INPUT, "Sophon output tensor index is invalid");
    }

    auto top_handle            = top_blob->GetHandle();
    auto top_desc              = top_blob->GetBlobDesc();
    size_t sz                  = tensor_size_vec.at(tensor_idx);
    bm_device_mem_t tensor_mem = output_tensors[tensor_idx].device_mem;
    auto top_size_info         = Calculate1DMemorySize(top_desc);
    const int64_t top_size     = GetBlobMemoryBytesSize(top_size_info);
    if (top_handle.base == nullptr || top_size <= 0) {
        return Status(COSMO_NN_ERR_INVALID_INPUT, "Sophon top blob is invalid");
    }
    float scale = (m_netinfo->output_scales && tensor_idx < m_netinfo->output_num)
                      ? m_netinfo->output_scales[tensor_idx]
                      : 1.0f;
    if (top_desc.device_type == DEVICE_NAIVE) {
        RETURN_ON_FAIL(CopyOutputToCpu(pbmhandle, top_handle.base, static_cast<size_t>(top_size), tensor_mem,
                                       sz, m_netinfo->output_dtypes[tensor_idx], scale));
    } else {
        bm_device_mem_t* dst_dev_mem = reinterpret_cast<bm_device_mem_t*>(top_handle.base);
        if (static_cast<size_t>(top_size) < sz || bm_mem_get_device_size(*dst_dev_mem) < sz) {
            return Status(COSMO_NN_ERR_INVALID_INPUT, "Sophon device output buffer is too small");
        }
        if (bm_memcpy_d2d_byte(pbmhandle, *dst_dev_mem, 0, tensor_mem, 0, sz) != BM_SUCCESS) {
            return Status(COSMO_NN_ERR_SOPHON_MEMCPY_ERR, "Sophon memcpy d2d failed");
        }
    }
    return Status{COSMO_NN_OK};
}

Status SophonNetNode::Forward(std::vector<std::shared_ptr<Blob>>& bottom_blobs,
                              std::vector<std::shared_ptr<Blob>>& top_blobs) {
    bm_status_t ret = BM_SUCCESS;
    timer.Start();
    RETURN_ON_FAIL(CheckNodeInputOutput(bottom_blobs, top_blobs, !output_to_cpu_));

    // bm_handle_t pbmhandle = shared_resource->m_handle;
    if (nullptr == pbmhandle)
        return Status(COSMO_NN_ERR_SOPHON_HANDLE_FAILED, "bm_handle_t is null !");

    if (m_bmrt == nullptr || m_netinfo == nullptr || m_netinfo->stages == nullptr ||
        m_netinfo->stage_num <= 0) {
        return Status(COSMO_NN_ERR_GRAPH_NOT_INIT, "Sophon network is not initialized");
    }

    if (top_blobs.size() != static_cast<size_t>(m_netinfo->output_num)) {
        return Status(COSMO_NN_ERR_INVALID_INPUT, "top_blobs size (" + std::to_string(top_blobs.size()) +
                                                      ") does not match model output num (" +
                                                      std::to_string(m_netinfo->output_num) + ")");
    }

    const auto& first_dims = bottom_blobs.at(0)->GetBlobDesc().dims;
    if (first_dims.empty() || first_dims.front() <= 0) {
        return Status(COSMO_NN_ERR_INVALID_INPUT, "batch size is invalid");
    }
    const int current_batch = first_dims.front();
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
        auto bottom_blob = bottom_blobs.at(i);
        if (!bottom_blob || bottom_blob->GetHandle().base == nullptr) {
            return Status(COSMO_NN_ERR_INVALID_INPUT, "Sophon input blob is invalid");
        }
        auto bottom_handle           = bottom_blob->GetHandle();
        bm_device_mem_t* src_dev_mem = reinterpret_cast<bm_device_mem_t*>(bottom_handle.base);

        size_t required_input_size = 0;
        const auto& expected_shape = input_tensors[i].shape;
        if (!input_sizes_.empty()) {
            const auto& dims = bottom_blob->GetBlobDesc().dims;
            if (dims.size() != 4 ||
                dims != DimsVector{1, 3, selected_input_size_.height, selected_input_size_.width})
                return Status(COSMO_NN_ERR_INVALID_INPUT, "Input does not match the selected size");
        }
        if (!GetTensorByteSize(expected_shape, m_netinfo->input_dtypes[i], &required_input_size) ||
            bm_mem_get_device_size(*src_dev_mem) < required_input_size) {
            return Status(COSMO_NN_ERR_INVALID_INPUT, "Sophon input tensor buffer is too small");
        }

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
    if (!input_sizes_.empty())
        RETURN_ON_FAIL(CheckInputSizeOutputs());

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
            RETURN_ON_FAIL(CopyOutputToBlob(top_blobs.at(idx_hr0), high_res_0_bm));
            RETURN_ON_FAIL(CopyOutputToBlob(top_blobs.at(idx_hr1), high_res_1_bm));
            RETURN_ON_FAIL(CopyOutputToBlob(top_blobs.at(idx_emb), image_embed_bm));
            timer.Stop();
            return Status{COSMO_NN_OK};
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
            RETURN_ON_FAIL(CopyOutputToBlob(top_blobs.at(0), masks_bmodel_idx));
            RETURN_ON_FAIL(CopyOutputToBlob(top_blobs.at(1), scores_bmodel_idx));
            timer.Stop();
            return Status{COSMO_NN_OK};
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
    return Status{COSMO_NN_OK};
}

}  // namespace cosmo::nn
