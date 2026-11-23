#include <array>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "ax_engine_api.h"
#include "ax_sys_api.h"
#include "infer/BmodelTool.h"
#include "nn/device/axera/axera_net_node.h"
#include "nn/node/node_type_utils.h"
#include "util/Log.h"

namespace {

int input_shape[]{1, 3, 1, 2};
int output_shape[]{1, 1, 2};
int padded_stride[]{12, 4, 4, 1};
AX_ENGINE_IOMETA_T input{};
AX_ENGINE_IOMETA_T output{};
AX_ENGINE_IO_INFO_T metadata{};
const std::array<float, 6> input_pixels{0.0f, 0.5f, 0.25f, 0.75f, 1.0f, 0.25f};
const std::array<float, 2> output_values{0.25f, 0.75f};
int sdk_handle{};
int runs{};
int handles{};
int buffers{};
bool fail_run{};

void InitMetadata() {
    input.eDataType      = AX_ENGINE_DT_FLOAT32;
    input.eLayout        = AX_ENGINE_TENSOR_LAYOUT_NCHW;
    input.nSize          = sizeof(input_pixels);
    input.nShapeSize     = 4;
    input.pShape         = input_shape;
    output.eDataType     = AX_ENGINE_DT_FLOAT32;
    output.nSize         = sizeof(output_values);
    output.nShapeSize    = 3;
    output.pShape        = output_shape;
    metadata.nInputSize  = 1;
    metadata.nOutputSize = 1;
    metadata.pInputs     = &input;
    metadata.pOutputs    = &output;
}

std::shared_ptr<cosmo::nn::Blob> FloatBlob(std::vector<int> dims, void* pixels) {
    cosmo::nn::BlobDesc desc{};
    desc.dims        = std::move(dims);
    desc.data_type   = cosmo::nn::DATA_TYPE_FLOAT;
    desc.data_format = cosmo::nn::DATA_FORMAT_NCHW;
    cosmo::nn::BlobHandle handle{};
    handle.base = pixels;
    return std::make_shared<cosmo::nn::Blob>(desc, handle);
}

}  // namespace

namespace cosmo::log {
size_t LogFormatArg(const char*, const char*, int, int, int, fmt::string_view, fmt::format_args) {
    return 0;
}
}  // namespace cosmo::log

namespace cosmo::nn {
std::string NodeTypeUtils::NodeTypeToStr(NodeType) {
    return "net";
}
}  // namespace cosmo::nn

extern "C" {
AX_S32 AX_SYS_Init() {
    return 0;
}
AX_S32 AX_SYS_Deinit() {
    return 0;
}
AX_S32 AX_ENGINE_Init(AX_ENGINE_NPU_ATTR_T*) {
    return 0;
}
AX_S32 AX_ENGINE_Deinit() {
    return 0;
}
AX_S32 AX_ENGINE_CreateHandle(AX_ENGINE_HANDLE* handle, const AX_VOID* bytes, AX_U32 size) {
    assert(bytes && size == 1);
    *handle = &sdk_handle;
    ++handles;
    return 0;
}
AX_S32 AX_ENGINE_DestroyHandle(AX_ENGINE_HANDLE handle) {
    assert(handle == &sdk_handle);
    --handles;
    return 0;
}
AX_S32 AX_ENGINE_GetIOInfo(AX_ENGINE_HANDLE handle, AX_ENGINE_IO_INFO_T** info) {
    assert(handle == &sdk_handle);
    *info = &metadata;
    return 0;
}
AX_S32 AX_ENGINE_CreateContext(AX_ENGINE_HANDLE handle) {
    assert(handle == &sdk_handle);
    return 0;
}
AX_S32 AX_ENGINE_RunSync(AX_ENGINE_HANDLE handle, AX_ENGINE_IO_T* io) {
    assert(handle == &sdk_handle && io->nInputSize == 1 && io->nOutputSize == 1);
    assert(io->pInputs[0].nSize == sizeof(input_pixels));
    assert(io->pOutputs[0].nSize == sizeof(output_values));
    assert(std::memcmp(io->pInputs[0].pVirAddr, input_pixels.data(), sizeof(input_pixels)) == 0);
    ++runs;
    if (fail_run)
        return 1;
    std::memcpy(io->pOutputs[0].pVirAddr, output_values.data(), sizeof(output_values));
    return 0;
}
AX_S32 AX_SYS_MemAlloc(AX_U64* physical, AX_VOID** virtual_address, AX_U32 size, AX_U32, const AX_S8*) {
    *virtual_address = std::malloc(size);
    if (!*virtual_address)
        return 1;
    *physical = reinterpret_cast<AX_U64>(*virtual_address);
    ++buffers;
    return 0;
}
AX_S32 AX_SYS_MemFree(AX_U64 physical, AX_VOID* virtual_address) {
    assert(physical == reinterpret_cast<AX_U64>(virtual_address));
    std::free(virtual_address);
    --buffers;
    return 0;
}
}  // extern "C"

int main(int argc, char** argv) {
    using namespace cosmo::nn;
    assert(argc == 2);
    InitMetadata();
    const char model = 'm';
    {
        AxeraNetNode node;
        node.SetNetworkInputNames({"input"});
        node.SetNetworkOutputNames({"output"});
        assert(!node.LoadWeight(nullptr, 0));
        assert(node.LoadWeight(&model, 1));
        assert(node.InferTopShapes());
        assert(buffers == 2 && handles == 1);
        auto floats = input_pixels;
        std::array<float, 2> values{};
        std::vector<std::shared_ptr<Blob>> inputs{FloatBlob({1, 3, 1, 2}, floats.data())};
        std::vector<std::shared_ptr<Blob>> outputs{FloatBlob({1, 1, 2}, values.data())};
        assert(node.Forward(inputs, outputs));
        assert(values == output_values && runs == 1);
        inputs.front()->GetBlobDesc().dims = {1, 3, 2, 1};
        assert(!node.Forward(inputs, outputs) && runs == 1);
        inputs.front()->GetBlobDesc().dims = {1, 3, 1, 2};
        values.fill(0);
        fail_run = true;
        assert(!node.Forward(inputs, outputs) && runs == 2);
        assert(values[0] == 0 && values[1] == 0);
        input.pStride = padded_stride;
        assert(!node.LoadWeight(&model, 1));
        assert(handles == 0 && buffers == 0);
        input.pStride = nullptr;
    }

    const std::string source = std::string(argv[1]) + "/model.axmodel";
    {
        std::ofstream model_file(source, std::ios::binary);
        model_file.put(model);
        assert(model_file.good());
    }
    const auto model_info = cosmo::BmodelTool::GetBmodelInfo(source);
    assert(model_info.valid && model_info.networks.size() == 1);
    input.eDataType       = AX_ENGINE_DT_UINT8;
    const auto unverified = cosmo::BmodelTool::GetBmodelInfo(source);
    assert(!unverified.valid && unverified.error_msg.find("verified preprocessing") != std::string::npos);
    assert(handles == 0 && buffers == 0);
}
