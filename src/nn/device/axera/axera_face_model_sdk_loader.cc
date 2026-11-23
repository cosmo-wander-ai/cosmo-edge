#ifdef COSMO_NN_USE_AXERA_BACKEND
#include "nn/device/axera/axera_face_model_sdk_loader.h"

#include <cstring>
#include <limits>
#include <mutex>
#include <utility>

#include "nn/device/axera/axera_native_execution_state.h"

namespace cosmo::nn::axera::face_binding {
namespace {
    constexpr std::size_t kMaxTensors = 64;
    constexpr std::size_t kMaxRank    = 8;
    constexpr std::size_t kMaxName    = 1024;

    bool CopyTensor(const AX_ENGINE_IOMETA_T& source, TensorMetadata& target) {
        if (!source.pName || !source.pShape || !source.nShapeSize || source.nShapeSize > kMaxRank ||
            !source.nSize)
            return false;
        const auto len = strnlen(source.pName, kMaxName);
        if (!len || len == kMaxName)
            return false;
        TensorMetadata staged;
        staged.name               = std::string(source.pName, len);
        staged.bytes              = source.nSize;
        staged.quantization_value = source.nQuantizationValue;
        if (source.eDataType == AX_ENGINE_DT_UINT8)
            staged.type = ElementType::Uint8;
        else if (source.eDataType == AX_ENGINE_DT_FLOAT32)
            staged.type = ElementType::Float32;
        else
            staged.type = ElementType::Unknown;
        staged.layout = source.eLayout == AX_ENGINE_TENSOR_LAYOUT_NCHW ? Layout::Nchw : Layout::Other;
        staged.sdk_codes.dtype   = static_cast<int>(source.eDataType);
        staged.sdk_codes.layout  = static_cast<int>(source.eLayout);
        staged.sdk_codes.memtype = static_cast<int>(source.eMemoryType);
        if (source.pExtraMeta) {
            staged.sdk_codes.colorspace = static_cast<int>(source.pExtraMeta->eColorSpace);
            staged.sdk_codes.present    = true;
        }
        for (std::size_t i = 0; i < source.nShapeSize; ++i) {
            if (source.pShape[i] <= 0 || (source.pStride && source.pStride[i] <= 0))
                return false;
            staged.shape.push_back(static_cast<std::size_t>(source.pShape[i]));
            if (source.pStride)
                staged.strides.push_back(static_cast<std::size_t>(source.pStride[i]));
        }
        target = std::move(staged);
        return true;
    }
}  // namespace

SdkLoadedModel::~SdkLoadedModel() = default;
AX_ENGINE_HANDLE SdkLoadedModel::HandleForOwnerThread() const noexcept {
    if (owner_thread_ != std::this_thread::get_id())
        return nullptr;
    try {
        auto& state = detail::NativeState();
        std::lock_guard<std::mutex> lock(state.mutex);
        return state.poisoned ? nullptr : handle_;
    } catch (...) {
        return nullptr;
    }
}
bool SdkLoadedModel::ReadMetadata(ModelMetadata& output) const {
    if (owner_thread_ != std::this_thread::get_id() || !handle_)
        return false;
    auto& state = detail::NativeState();
    std::lock_guard<std::mutex> lock(state.mutex);
    if (state.poisoned)
        return false;
    AX_ENGINE_IO_INFO_T* info = nullptr;
    if (AX_ENGINE_GetIOInfo(handle_, &info) != 0 || !info || !info->nInputSize || !info->nOutputSize ||
        info->nInputSize > kMaxTensors || info->nOutputSize > kMaxTensors || !info->pInputs ||
        !info->pOutputs)
        return false;
    try {
        ModelMetadata staged;
        staged.max_batch_size     = info->nMaxBatchSize;
        staged.dynamic_batch_size = info->bDynamicBatchSize != 0;
        staged.inputs.resize(info->nInputSize);
        staged.outputs.resize(info->nOutputSize);
        for (std::size_t i = 0; i < staged.inputs.size(); ++i)
            if (!CopyTensor(info->pInputs[i], staged.inputs[i]))
                return false;
        for (std::size_t i = 0; i < staged.outputs.size(); ++i)
            if (!CopyTensor(info->pOutputs[i], staged.outputs[i]))
                return false;
        output = std::move(staged);
        return true;
    } catch (...) {
        return false;
    }
}
std::shared_ptr<LoadedModel> SdkLoader::Load(
    const std::shared_ptr<const std::vector<std::uint8_t>>& exact_bytes) {
    if (!exact_bytes || exact_bytes->empty() || exact_bytes->size() > std::numeric_limits<AX_U32>::max())
        return nullptr;
    // A custom deleter preserves the handle, bytes and engine lease if the
    // final owner is released on an unverified thread or after uncertain
    // device completion. Such a case poisons this native execution domain.
    auto destroy = [](SdkLoadedModel* model) {
        auto& state = detail::NativeState();
        std::lock_guard<std::mutex> lock(state.mutex);
        if (!model->handle_ && !model->engine_acquired_) {
            delete model;
            return;
        }
        if (state.poisoned || model->owner_thread_ != std::this_thread::get_id()) {
            state.poisoned = true;
            return;
        }
        if (model->handle_ && AX_ENGINE_DestroyHandle(model->handle_) != 0) {
            state.poisoned = true;
            return;
        }
        model->handle_ = nullptr;
        delete model;
    };
    std::shared_ptr<SdkLoadedModel> model;
    try {
        auto* raw_model          = new SdkLoadedModel;
        raw_model->owner_thread_ = std::this_thread::get_id();
        model                    = std::shared_ptr<SdkLoadedModel>(raw_model, destroy);
        model->bytes_            = exact_bytes;
        auto& state              = detail::NativeState();
        {
            std::lock_guard<std::mutex> lock(state.mutex);
            if (state.poisoned)
                return nullptr;
            if (model->engine_.Acquire() != 0)
                return nullptr;
            model->engine_acquired_ = true;
            AX_ENGINE_HANDLE handle = nullptr;
            const auto result       = AX_ENGINE_CreateHandle(&handle, exact_bytes->data(),
                                                             static_cast<AX_U32>(exact_bytes->size()));
            model->handle_          = handle;
            if (result != 0 || !handle || AX_ENGINE_CreateContext(handle) != 0)
                return nullptr;
        }
        return model;
    } catch (...) {
        return nullptr;
    }
}
}  // namespace cosmo::nn::axera::face_binding
#endif
