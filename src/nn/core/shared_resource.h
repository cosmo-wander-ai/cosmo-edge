#pragma once

#ifdef COSMO_NN_USE_SOPHON_BACKEND
#include "bmruntime_cpp.h"
#include "bmruntime_interface.h"
#endif

#include <string>
#include <vector>

#include "nn/core/abstract_context.h"

namespace cosmo::nn {

class SharedResource {
public:
    explicit SharedResource(int i = 0) noexcept(false);

    ~SharedResource();

public:
#ifdef COSMO_NN_USE_SOPHON_BACKEND
    bm_handle_t m_handle;
#endif

    int current_device_id = 0;

    // dino
    void* tokenizer_handle = nullptr;
    std::string tokenizer_path{};
    std::vector<int32_t> prompt_token_ids{};
    float text_threshold = 0.0f;
    float box_threshold  = 0.0f;

    // sophon
    int net_input_w         = 0;
    int net_input_h         = 0;
    float model_input_scale = 1.0f;  // for INT8 quantized models
};

}  // namespace cosmo::nn
