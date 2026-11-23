#pragma once

// Independent experimental fire/smoke capability. Hashes the exact immutable bytes given to the
// shared AX loader and checks metadata before publishing any capability. This
// does not authorize a scene, a frame, or runtime execution.
#include "nn/device/axera/axera_face_model_binding.h"

namespace cosmo::nn::axera::fire_binding {
// This record must come from an independently reviewed, model-bound scene
// contract. It is not a claim that visual samples establish weight identity.
struct ClassPolicy {
    std::string label;
    float threshold{0.f};
    bool enabled{false};
};
struct Contract : face_binding::Contract {
    std::string atomic_code;
    std::array<ClassPolicy, 2> classes{};
};
using face_binding::ElementType;
using face_binding::Layout;
using face_binding::LoadedModel;
using face_binding::Loader;
using face_binding::ModelMetadata;
using face_binding::ModelStrideRule;
using face_binding::SdkCodes;
using face_binding::TensorMetadata;

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
        const std::shared_ptr<LoadedModel>& Model() const noexcept {
            return model_;
        }

    private:
        friend class Capability;
        ExecutionLease(std::unique_lock<std::mutex> guard, std::shared_ptr<LoadedModel> model)
            : guard_(std::move(guard)), model_(std::move(model)) {}
        std::unique_lock<std::mutex> guard_;
        std::shared_ptr<LoadedModel> model_;
    };
    bool Active() const noexcept;
    const Contract& GetContract() const noexcept {
        return contract_;
    }
    // Model admission alone never authorizes inference. The graph must check
    // actual input and output I/O byte strides and storage on every run.
    bool RequiresExplicitRuntimeIoStrides() const noexcept {
        return true;
    }
    // Caller must hold this capability for each use, recheck Active before
    // submission, and drain in-flight work before replacing or closing it.
    std::shared_ptr<LoadedModel> AcquireModel() const noexcept;
    // Prevents revoke/rebind from racing submission. Hold through device
    // completion and metadata read; unknown completion still quarantines the
    // physical owner under the native execution protocol.
    ExecutionLease AcquireForExecution() const;

private:
    friend class Binding;
    struct State {
        std::mutex mutex;
        std::atomic<bool> active{true};
    };
    Capability(Contract, std::shared_ptr<const std::vector<std::uint8_t>>, std::shared_ptr<LoadedModel>,
               std::shared_ptr<State>);
    const Contract contract_;
    const std::shared_ptr<const std::vector<std::uint8_t>> bytes_;
    const std::shared_ptr<LoadedModel> model_;
    const std::shared_ptr<State> active_;
};

class Binding {
public:
    Binding() = default;
    ~Binding();
    Binding(const Binding&)            = delete;
    Binding& operator=(const Binding&) = delete;
    // Revokes the previous capability before any attempt, including failure.
    // Hashes the retained immutable bytes subsequently handed to the loader.
    std::shared_ptr<const Capability> Bind(const void* bytes, std::size_t size, const Contract& contract,
                                           Loader& loader);
    bool Owns(const std::shared_ptr<const Capability>& capability);
    void Revoke() noexcept;

private:
    std::mutex mutex_;
    std::shared_ptr<Capability::State> active_;
};

}  // namespace cosmo::nn::axera::fire_binding
