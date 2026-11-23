#pragma once

#ifdef COSMO_NN_USE_AXERA_BACKEND

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <string_view>
#include <utility>
#include <vector>

#include "E24NativeInput.h"
#include "nn/device/axera/axera_face_model_sdk_loader.h"

namespace cosmo::nn::axera {

// Load, forward and destroy on the model's owning detector worker. The caller
// supplies model bytes from the existing resource resolver and parses the rows.
class E24NativeRunner final {
public:
    using Output = std::array<float, kE24OutputElements>;

    E24NativeRunner()                                  = default;
    E24NativeRunner(const E24NativeRunner&)            = delete;
    E24NativeRunner& operator=(const E24NativeRunner&) = delete;

    bool Load(std::string_view bytes) {
        if (bytes.empty() || bytes.size() > std::numeric_limits<AX_U32>::max())
            return false;
        try {
            auto retained = std::make_shared<const std::vector<std::uint8_t>>(bytes.begin(), bytes.end());
            return Load(retained);
        } catch (const std::bad_alloc&) {
            return false;
        }
    }

    bool Load(const std::shared_ptr<const std::vector<std::uint8_t>>& bytes) {
        if (model_ && !model_->HandleForOwnerThread())
            return false;
        face_binding::SdkLoader loader;
        auto model        = std::dynamic_pointer_cast<face_binding::SdkLoadedModel>(loader.Load(bytes));
        const auto handle = model ? model->HandleForOwnerThread() : nullptr;
        if (!handle)
            return false;
        auto& state = detail::NativeState();
        std::shared_ptr<OutputStorage> output;
        try {
            output = std::make_shared<OutputStorage>();
        } catch (const std::bad_alloc&) {
            return false;
        }
        {
            std::lock_guard<std::mutex> lock(state.mutex);
            AX_ENGINE_IO_INFO_T* info = nullptr;
            if (state.poisoned || AX_ENGINE_GetIOInfo(handle, &info) != 0 || !info ||
                !E24NativeModelMetadata(*info))
                return false;
            auto& buffer      = output->binding;
            buffer.nSize      = sizeof(Output);
            const auto status = AX_SYS_MemAlloc(&buffer.phyAddr, &buffer.pVirAddr, buffer.nSize, 128,
                                                reinterpret_cast<const AX_S8*>("cosmo-e24-output"));
            if (status != 0 || !buffer.phyAddr || !buffer.pVirAddr) {
                if (buffer.phyAddr || buffer.pVirAddr) {
                    state.poisoned     = true;
                    output->quarantine = output;
                }
                return false;
            }
        }
        output_ = std::move(output);
        model_  = std::move(model);
        return true;
    }

    [[nodiscard]] bool Ready() const {
        return model_ && model_->HandleForOwnerThread();
    }

    bool Forward(const std::shared_ptr<E24NativeInput>& prepared, Output& rows, int* sdk_error = nullptr) {
        if (sdk_error)
            *sdk_error = 0;
        const auto handle = model_ ? model_->HandleForOwnerThread() : nullptr;
        if (!handle || !prepared || !output_)
            return false;
        auto input = prepared->Binding();
        AX_ENGINE_IO_T io{};
        io.pInputs     = &input;
        io.nInputSize  = 1;
        io.pOutputs    = &output_->binding;
        io.nOutputSize = 1;
        auto& state    = detail::NativeState();
        {
            std::lock_guard<std::mutex> lock(state.mutex);
            if (state.poisoned)
                return false;
            const auto status = AX_ENGINE_RunSync(handle, &io);
            if (status != 0) {
                state.poisoned = true;
                E24NativeInput::HoldOnUncertainCompletion(prepared);
                output_->quarantine = output_;
                if (sdk_error)
                    *sdk_error = status;
                return false;
            }
        }
        std::memcpy(rows.data(), output_->binding.pVirAddr, sizeof(Output));
        return std::all_of(rows.begin(), rows.end(), [](float value) { return std::isfinite(value); });
    }

private:
    struct OutputStorage {
        AX_ENGINE_IO_BUFFER_T binding{};
        std::shared_ptr<OutputStorage> quarantine;

        ~OutputStorage() {
            if (binding.phyAddr && binding.pVirAddr)
                AX_SYS_MemFree(binding.phyAddr, binding.pVirAddr);
        }
    };

    // The model retains the AX Engine and SYS references until output storage is freed.
    std::shared_ptr<face_binding::SdkLoadedModel> model_;
    std::shared_ptr<OutputStorage> output_;
};

}  // namespace cosmo::nn::axera

#endif
