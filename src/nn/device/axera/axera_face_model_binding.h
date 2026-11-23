#pragma once

// Fail-closed model identity boundary for the experimental AX raw NV12 face
// graph. This is not a production admission record and does not enable a graph.
// The caller must supply a trusted contract and an SDK loader that creates a
// handle from the supplied immutable bytes and reports metadata from that same
// handle. Source color, device completion and cache visibility are separate
// per-frame/runtime requirements.
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace cosmo::nn::axera::face_binding {

enum class ElementType { Unknown, Uint8, Float32 };
enum class Layout { Unknown, Nchw, Other };
enum class ModelStrideRule {
    RequireExplicitMetadata,
    // Only for a verified SDK contract where null IOMETA pStride means no
    // model restriction. Runtime I/O must still carry exact explicit byte
    // strides and be checked against the leased buffer before every submit.
    AllowUnconstrainedWithExplicitRuntimeIo,
};
struct SdkCodes {
    int dtype{0};
    int layout{0};
    int memtype{0};
    int colorspace{0};
    bool present{false};
};

struct TensorMetadata {
    std::string name;
    ElementType type{ElementType::Unknown};
    Layout layout{Layout::Unknown};
    std::vector<std::size_t> shape;
    std::size_t bytes{0};
    // Element strides as returned by the SDK, never synthesized from shape.
    std::vector<std::size_t> strides;
    // Preserve the exact SDK enums even when the generic layout is Other.
    SdkCodes sdk_codes;
    std::uint32_t quantization_value{0};
};
struct ModelMetadata {
    std::vector<TensorMetadata> inputs;
    std::vector<TensorMetadata> outputs;
    std::uint32_t max_batch_size{0};
    bool dynamic_batch_size{false};
};

// The application must only construct this from an independently trusted
// model-to-scenario record. A JSON flag or matching digest alone does not
// establish that the chosen model is appropriate for a production scene.
struct Contract {
    std::string model_sha256;
    std::string authority_revision;
    std::string scene_id;
    std::string y_name;
    std::string uv_name;
    std::string output_name;
    std::size_t result_capacity{0};
    std::size_t required_capacity{0};
    float graph_confidence{0.f};
    float graph_iou{0.f};
    // This ABI denotes NV12 1920x1080 pitch 2048, BT.709 limited, graph
    // content 512x288 centered within 512x512, one face class, and graph NMS.
    std::string semantic_contract;
    Layout output_layout{Layout::Unknown};
    ModelStrideRule model_stride_rule{ModelStrideRule::RequireExplicitMetadata};
    // Ordered as Y, UV, output, and supplied from a trusted SDK contract.
    std::array<SdkCodes, 3> expected_sdk_codes{};
    std::array<std::uint32_t, 3> expected_quantization_values{};
    std::uint32_t expected_max_batch_size{0};
    bool expected_dynamic_batch_size{false};
};

class LoadedModel {
public:
    virtual ~LoadedModel() = default;
    // Must query/copy actual metadata from this loaded handle, not a file or
    // caller-provided expected shape. Failure or incomplete metadata rejects.
    virtual bool ReadMetadata(ModelMetadata& output) const = 0;
};
class Loader {
public:
    virtual ~Loader() = default;
    // A trusted backend implementation must load these exact bytes and keep
    // its engine handle/resources alive in the returned LoadedModel owner.
    virtual std::shared_ptr<LoadedModel> Load(
        const std::shared_ptr<const std::vector<std::uint8_t>>& exact_bytes) = 0;
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

}  // namespace cosmo::nn::axera::face_binding
