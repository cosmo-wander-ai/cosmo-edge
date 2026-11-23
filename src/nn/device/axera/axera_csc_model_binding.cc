#include "nn/device/axera/axera_csc_model_binding.h"

#include <algorithm>
#include <array>
#include <limits>

#include "cryptopp/sha.h"

namespace cosmo::nn::axera::csc_binding {
namespace {
    using namespace face_binding;
    constexpr const char* kSemantic = "ax650-nv12-1080p-pitch2048-bt709-limited-to-pitch1920-bt601-full-v1";

    bool ValidContract(const Contract& c) {
        const auto hex = [](char ch) { return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f'); };
        for (std::size_t i = 0; i < c.names.size(); ++i) {
            const auto& name = c.names[i];
            if (name.empty() || name.size() > 200 || name.find('\0') != name.npos)
                return false;
            for (std::size_t j = 0; j < i; ++j)
                if (c.names[j] == name)
                    return false;
        }
        return c.model_sha256.size() == 64 &&
               std::all_of(c.model_sha256.begin(), c.model_sha256.end(), hex) &&
               !c.authority_revision.empty() && c.authority_revision.size() <= 200 &&
               c.authority_revision.find('\0') == c.authority_revision.npos &&
               c.semantic_contract == kSemantic &&
               (c.model_stride_rule == ModelStrideRule::RequireExplicitMetadata ||
                c.model_stride_rule == ModelStrideRule::AllowUnconstrainedWithExplicitRuntimeIo);
    }
    std::string Sha256(const std::vector<std::uint8_t>& bytes) {
        std::array<std::uint8_t, CryptoPP::SHA256::DIGESTSIZE> digest{};
        CryptoPP::SHA256().CalculateDigest(digest.data(), bytes.data(), bytes.size());
        constexpr char hex[] = "0123456789abcdef";
        std::string result;
        result.reserve(64);
        for (const auto byte : digest) {
            result.push_back(hex[byte >> 4]);
            result.push_back(hex[byte & 15]);
        }
        return result;
    }
    bool MatchesTensor(const TensorMetadata& tensor, const std::string& name, std::size_t height,
                       std::size_t pitch, bool input, ModelStrideRule rule) {
        const std::size_t bytes = height * pitch;
        const std::vector<std::size_t> shape{1, 1, height, pitch};
        const std::vector<std::size_t> strides{bytes, bytes, pitch, 1};
        const bool stride_ok =
            tensor.strides == strides ||
            (tensor.strides.empty() && rule == ModelStrideRule::AllowUnconstrainedWithExplicitRuntimeIo);
        // AX SDK enum values, checked against the observed 650N headers:
        // UINT8=1; input NCHW=2 / output UNKNOWN=0; PHYSICAL=0;
        // input GRAY=9 / output FEATUREMAP=0. UNKNOWN layout is preserved as
        // generic Other; four dimensions do not establish an NCHW layout.
        const auto& codes = tensor.sdk_codes;
        return tensor.name == name && tensor.type == ElementType::Uint8 &&
               tensor.layout == (input ? Layout::Nchw : Layout::Other) && tensor.shape == shape &&
               tensor.bytes == bytes && stride_ok && tensor.quantization_value == 0 && codes.present &&
               codes.dtype == 1 && codes.layout == (input ? 2 : 0) && codes.memtype == 0 &&
               codes.colorspace == (input ? 9 : 0);
    }
    bool Matches(const ModelMetadata& metadata, const Contract& contract) {
        if (metadata.inputs.size() != 2 || metadata.outputs.size() != 2 || metadata.max_batch_size != 1 ||
            metadata.dynamic_batch_size)
            return false;
        return MatchesTensor(metadata.inputs[0], contract.names[0], 1080, 2048, true,
                             contract.model_stride_rule) &&
               MatchesTensor(metadata.inputs[1], contract.names[1], 540, 2048, true,
                             contract.model_stride_rule) &&
               MatchesTensor(metadata.outputs[0], contract.names[2], 1080, 1920, false,
                             contract.model_stride_rule) &&
               MatchesTensor(metadata.outputs[1], contract.names[3], 540, 1920, false,
                             contract.model_stride_rule);
    }
}  // namespace

Capability::Capability(Contract c, std::shared_ptr<const std::vector<std::uint8_t>> bytes,
                       std::shared_ptr<face_binding::LoadedModel> model, std::shared_ptr<State> state)
    : contract_(std::move(c)), bytes_(std::move(bytes)), model_(std::move(model)), state_(std::move(state)) {}
bool Capability::Active() const noexcept {
    return state_ && state_->active.load(std::memory_order_acquire);
}
Capability::ExecutionLease Capability::AcquireForExecution() const {
    std::unique_lock<std::mutex> lock(state_->mutex);
    return ExecutionLease(std::move(lock), Active() ? model_ : nullptr);
}
Capability::ExecutionLease Capability::TryAcquireForExecution() const {
    std::unique_lock<std::mutex> lock(state_->mutex, std::try_to_lock);
    const bool available = lock.owns_lock() && Active();
    return ExecutionLease(std::move(lock), available ? model_ : nullptr);
}
Binding::~Binding() {
    Revoke();
}
bool Binding::Owns(const std::shared_ptr<const Capability>& capability) {
    std::lock_guard<std::mutex> lock(mutex_);
    return capability && active_ && capability->state_ == active_ && capability->Active();
}
void Binding::Revoke() noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    if (active_) {
        std::lock_guard<std::mutex> execution_lock(active_->mutex);
        active_->active.store(false, std::memory_order_release);
    }
    active_.reset();
}
std::shared_ptr<const Capability> Binding::Bind(const void* bytes, std::size_t size, const Contract& contract,
                                                face_binding::Loader& loader) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (active_) {
        std::lock_guard<std::mutex> execution_lock(active_->mutex);
        active_->active.store(false, std::memory_order_release);
    }
    active_.reset();
    if (!bytes || !size || size > std::numeric_limits<std::uint32_t>::max() || !ValidContract(contract))
        return nullptr;
    try {
        const auto* first = static_cast<const std::uint8_t*>(bytes);
        auto owned        = std::make_shared<const std::vector<std::uint8_t>>(first, first + size);
        if (Sha256(*owned) != contract.model_sha256)
            return nullptr;
        auto model = loader.Load(owned);
        face_binding::ModelMetadata metadata;
        if (!model || !model->ReadMetadata(metadata) || !Matches(metadata, contract))
            return nullptr;
        auto state      = std::make_shared<Capability::State>();
        auto capability = std::shared_ptr<const Capability>(new Capability(contract, owned, model, state));
        active_         = std::move(state);
        return capability;
    } catch (...) {
        return nullptr;
    }
}
}  // namespace cosmo::nn::axera::csc_binding
