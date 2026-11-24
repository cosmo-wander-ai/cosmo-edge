#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <vector>

#include "rknn_api.h"

namespace {

class ContextGuard {
public:
    ~ContextGuard() {
        if (context_ != 0) {
            rknn_destroy(context_);
        }
    }

    rknn_context* Out() {
        return &context_;
    }
    rknn_context Get() const {
        return context_;
    }

private:
    rknn_context context_{0};
};

std::vector<std::uint8_t> ReadFile(const std::string& path) {
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream) {
        throw std::runtime_error("cannot open model: " + path);
    }
    const auto size = stream.tellg();
    if (size <= 0 || static_cast<std::uint64_t>(size) > std::numeric_limits<std::uint32_t>::max()) {
        throw std::runtime_error("invalid model size: " + path);
    }
    std::vector<std::uint8_t> data(static_cast<std::size_t>(size));
    stream.seekg(0);
    stream.read(reinterpret_cast<char*>(data.data()), size);
    if (!stream) {
        throw std::runtime_error("cannot read complete model: " + path);
    }
    return data;
}

void Check(int result, const std::string& action) {
    if (result != RKNN_SUCC) {
        throw std::runtime_error(action + " failed with RKNN code " + std::to_string(result));
    }
}

std::optional<rknn_core_mask> ParseCoreMask(const std::string& value) {
    if (value == "auto")
        return std::nullopt;
    if (value == "0")
        return RKNN_NPU_CORE_0;
    if (value == "1")
        return RKNN_NPU_CORE_1;
    if (value == "01")
        return RKNN_NPU_CORE_0_1;
    throw std::runtime_error("core mask must be one of: auto, 0, 1, 01");
}

std::string Shape(const rknn_tensor_attr& attr) {
    std::string result;
    for (std::uint32_t index = 0; index < attr.n_dims; ++index) {
        if (!result.empty())
            result += 'x';
        result += std::to_string(attr.dims[index]);
    }
    return result;
}

void PrintTensor(const char* direction, const rknn_tensor_attr& attr) {
    std::cout << direction << '[' << attr.index << "] name=" << attr.name << " shape=" << Shape(attr)
              << " type=" << get_type_string(attr.type) << " format=" << get_format_string(attr.fmt)
              << " quant=" << get_qnt_type_string(attr.qnt_type) << " bytes=" << attr.size
              << " stride_bytes=" << attr.size_with_stride << " zp=" << attr.zp << " scale=" << attr.scale
              << '\n';
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2 || argc > 3) {
        std::cerr << "Usage: " << argv[0] << " <model.rknn> [auto|0|1|01]\n";
        return 2;
    }

    try {
        const auto model     = ReadFile(argv[1]);
        const auto core_mask = ParseCoreMask(argc == 3 ? argv[2] : "auto");

        ContextGuard context;
        Check(rknn_init(context.Out(), const_cast<std::uint8_t*>(model.data()),
                        static_cast<std::uint32_t>(model.size()), 0, nullptr),
              "rknn_init");
        if (core_mask) {
            Check(rknn_set_core_mask(context.Get(), *core_mask), "rknn_set_core_mask");
        }

        rknn_sdk_version version{};
        Check(rknn_query(context.Get(), RKNN_QUERY_SDK_VERSION, &version, sizeof(version)),
              "RKNN_QUERY_SDK_VERSION");
        std::cout << "api_version=" << version.api_version << '\n';
        std::cout << "driver_version=" << version.drv_version << '\n';

        rknn_input_output_num counts{};
        Check(rknn_query(context.Get(), RKNN_QUERY_IN_OUT_NUM, &counts, sizeof(counts)),
              "RKNN_QUERY_IN_OUT_NUM");
        std::cout << "inputs=" << counts.n_input << " outputs=" << counts.n_output << '\n';

        std::vector<rknn_tensor_attr> input_attrs(counts.n_input);
        std::vector<rknn_input> inputs(counts.n_input);
        std::vector<std::vector<std::uint8_t>> input_buffers(counts.n_input);
        for (std::uint32_t index = 0; index < counts.n_input; ++index) {
            auto& attr = input_attrs[index];
            attr.index = index;
            Check(rknn_query(context.Get(), RKNN_QUERY_INPUT_ATTR, &attr, sizeof(attr)),
                  "RKNN_QUERY_INPUT_ATTR[" + std::to_string(index) + ']');
            PrintTensor("input", attr);
            rknn_tensor_attr native{};
            native.index = index;
            if (rknn_query(context.Get(), RKNN_QUERY_NATIVE_INPUT_ATTR, &native, sizeof(native)) == RKNN_SUCC)
                PrintTensor("native_input", native);

            input_buffers[index].resize(attr.size, 0);
            inputs[index].index        = index;
            inputs[index].buf          = input_buffers[index].data();
            inputs[index].size         = attr.size;
            inputs[index].pass_through = 0;
            inputs[index].type         = attr.type;
            inputs[index].fmt          = attr.fmt;
        }

        std::vector<rknn_tensor_attr> output_attrs(counts.n_output);
        for (std::uint32_t index = 0; index < counts.n_output; ++index) {
            auto& attr = output_attrs[index];
            attr.index = index;
            Check(rknn_query(context.Get(), RKNN_QUERY_OUTPUT_ATTR, &attr, sizeof(attr)),
                  "RKNN_QUERY_OUTPUT_ATTR[" + std::to_string(index) + ']');
            PrintTensor("output", attr);
        }

        Check(rknn_inputs_set(context.Get(), counts.n_input, inputs.data()), "rknn_inputs_set");
        Check(rknn_run(context.Get(), nullptr), "rknn_run");

        std::vector<rknn_output> outputs(counts.n_output);
        for (std::uint32_t index = 0; index < counts.n_output; ++index) {
            outputs[index].index       = index;
            outputs[index].want_float  = 1;
            outputs[index].is_prealloc = 0;
        }
        Check(rknn_outputs_get(context.Get(), counts.n_output, outputs.data(), nullptr), "rknn_outputs_get");

        for (std::uint32_t index = 0; index < counts.n_output; ++index) {
            const auto count   = outputs[index].size / sizeof(float);
            const auto* values = static_cast<const float*>(outputs[index].buf);
            std::cout << "output[" << index << "] float_count=" << count;
            if (count > 0) {
                std::cout << " first=" << std::setprecision(8) << values[0];
            }
            std::cout << '\n';
        }
        Check(rknn_outputs_release(context.Get(), counts.n_output, outputs.data()), "rknn_outputs_release");

        rknn_perf_run perf{};
        if (rknn_query(context.Get(), RKNN_QUERY_PERF_RUN, &perf, sizeof(perf)) == RKNN_SUCC) {
            std::cout << "npu_run_us=" << perf.run_duration << '\n';
        }

        rknn_mem_size memory{};
        if (rknn_query(context.Get(), RKNN_QUERY_MEM_SIZE, &memory, sizeof(memory)) == RKNN_SUCC) {
            std::cout << "weight_bytes=" << memory.total_weight_size
                      << " internal_bytes=" << memory.total_internal_size
                      << " dma_bytes=" << memory.total_dma_allocated_size << '\n';
        }

        std::cout << "probe_status=PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "probe_status=FAIL error=" << error.what() << '\n';
        return 1;
    }
}
