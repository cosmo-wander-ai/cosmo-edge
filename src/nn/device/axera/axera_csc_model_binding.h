#pragma once

// Independent CSC model identity boundary. This neither enables JPEG nor
// authorizes image publication; caller must separately verify frame witness,
// runtime strides, device completion, cache visibility and privacy policy.
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "nn/device/axera/axera_face_model_binding.h"

namespace cosmo::nn::axera::csc_binding {
struct Contract {
    std::string model_sha256;
    std::string authority_revision;
    // Identifies 1920x1080 BT.709 limited NV12 -> BT.601 full NV12, with
    // separate 2048-pitch inputs and 1920-pitch outputs. It is a publisher
    // assertion, not a substitute for real conversion quality tests.
    std::string semantic_contract;
    std::array<std::string, 4> names;  // input Y, input UV, output Y, output UV
    face_binding::ModelStrideRule model_stride_rule{face_binding::ModelStrideRule::RequireExplicitMetadata};
};

class Capability {
public:
    class ExecutionLease {
    public:
        ExecutionLease(ExecutionLease&&) noexcept            = default;
        ExecutionLease& operator=(ExecutionLease&&) noexcept = default;
        ExecutionLease(const ExecutionLease&)                = delete;
        ExecutionLease& operator=(const ExecutionLease&)     = delete;
        explicit operator bool() const noexcept {
            return static_cast<bool>(model_);
        }
        const std::shared_ptr<face_binding::LoadedModel>& Model() const noexcept {
            return model_;
        }

    private:
        friend class Capability;
        ExecutionLease(std::unique_lock<std::mutex> lock, std::shared_ptr<face_binding::LoadedModel> model)
            : lock_(std::move(lock)), model_(std::move(model)) {}
        std::unique_lock<std::mutex> lock_;
        std::shared_ptr<face_binding::LoadedModel> model_;
    };
    bool Active() const noexcept;
    const Contract& GetContract() const noexcept {
        return contract_;
    }
    // Required even when model metadata imposes no stride restriction.
    bool RequiresExplicitRuntimeIoStrides() const noexcept {
        return true;
    }
    // Hold through known synchronous device completion. Unknown completion
    // must quarantine the frame and all model/device owners.
    ExecutionLease AcquireForExecution() const;
    // Fail closed instead of waiting for a concurrently owned model.
    ExecutionLease TryAcquireForExecution() const;

private:
    friend class Binding;
    struct State {
        std::mutex mutex;
        std::atomic<bool> active{true};
    };
    Capability(Contract, std::shared_ptr<const std::vector<std::uint8_t>>,
               std::shared_ptr<face_binding::LoadedModel>, std::shared_ptr<State>);
    const Contract contract_;
    const std::shared_ptr<const std::vector<std::uint8_t>> bytes_;
    const std::shared_ptr<face_binding::LoadedModel> model_;
    const std::shared_ptr<State> state_;
};

class Binding {
public:
    ~Binding();
    Binding()                          = default;
    Binding(const Binding&)            = delete;
    Binding& operator=(const Binding&) = delete;
    // Copies once to immutable storage, hashes these same bytes, then hands
    // that owner to the loader. Any failed replacement revokes the old model.
    std::shared_ptr<const Capability> Bind(const void* bytes, std::size_t size, const Contract& contract,
                                           face_binding::Loader& loader);
    bool Owns(const std::shared_ptr<const Capability>& capability);
    void Revoke() noexcept;

private:
    std::mutex mutex_;
    std::shared_ptr<Capability::State> active_;
};
}  // namespace cosmo::nn::axera::csc_binding
