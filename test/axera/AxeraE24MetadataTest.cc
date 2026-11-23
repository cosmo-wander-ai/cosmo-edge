#include <cassert>

#include "nn/device/axera/E24NativeInput.h"

int main() {
    using namespace cosmo::nn::axera;
    int input_shape[]{1, 1104, 1280, 1};
    int output_shape[]{1, 300, 10};
    AX_ENGINE_IOMETA_EX_T colors{};
    colors.eColorSpace = AX_ENGINE_CS_NV12;
    AX_ENGINE_IOMETA_T input{};
    input.eDataType  = AX_ENGINE_DT_UINT8;
    input.eLayout    = AX_ENGINE_TENSOR_LAYOUT_NHWC;
    input.nShapeSize = 4;
    input.pShape     = input_shape;
    input.nSize      = kE24InputBytes;
    input.pExtraMeta = &colors;
    AX_ENGINE_IOMETA_T output{};
    output.eDataType  = AX_ENGINE_DT_FLOAT32;
    output.nShapeSize = 3;
    output.pShape     = output_shape;
    output.nSize      = kE24OutputElements * sizeof(float);
    AX_ENGINE_IO_INFO_T io{};
    io.nInputSize = io.nOutputSize = 1;
    io.pInputs                     = &input;
    io.pOutputs                    = &output;

    assert(E24NativeModelMetadata(io));
    colors.eColorSpace = AX_ENGINE_CS_RGB;
    assert(!E24NativeModelMetadata(io));
    colors.eColorSpace = AX_ENGINE_CS_NV12;
    output_shape[2]    = 6;
    assert(!E24NativeModelMetadata(io));
    output_shape[2] = 10;
    input.nSize -= 1;
    assert(!E24NativeModelMetadata(io));
}
