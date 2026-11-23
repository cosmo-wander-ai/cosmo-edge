#include "nn/device/axera/axera_face_model_binding.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <utility>

#include "cryptopp/sha.h"
#include "nn/device/axera/axera_hs_contract.h"
#include "nn/device/axera/axera_jfif_face_contract.h"

namespace cosmo::nn::axera::face_binding {
namespace {
    constexpr const char* kSemantic = "ax650-nv12-1080p-pitch2048-bt709-limited-face-pad112-nms-v1";

    bool ValidContract(const Contract& c) {
        const auto hex = [](char ch) { return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f'); };
#if defined(COSMO_AXERA_HS_ROUTE_CANDIDATE) &&                                                               \
    (defined(COSMO_AXERA_FACE_ISOLATED_DEVICE_TEST) || defined(COSMO_AXERA_HS_HOST_TEST) ||                  \
     defined(COSMO_AXERA_HS_ROUTE_PRODUCTION))
        if (c.model_sha256 == hs_contract::ModelSha256())
            return false;
#endif
        return c.model_sha256.size() == 64 &&
               std::all_of(c.model_sha256.begin(), c.model_sha256.end(), hex) &&
               !c.authority_revision.empty() && !c.scene_id.empty() && !c.y_name.empty() &&
               !c.uv_name.empty() && !c.output_name.empty() && c.y_name != c.uv_name &&
               c.y_name != c.output_name && c.uv_name != c.output_name && c.result_capacity > 0 &&
               c.result_capacity <= 1000 && c.required_capacity > 0 &&
               c.required_capacity <= c.result_capacity && std::isfinite(c.graph_confidence) &&
               c.graph_confidence > 0.f && c.graph_confidence <= 1.f && std::isfinite(c.graph_iou) &&
               c.graph_iou >= 0.f && c.graph_iou < 1.f &&
               ((c.semantic_contract == kSemantic && c.model_sha256 != jfif_face_contract::ModelSha256()) ||
                (c.semantic_contract == jfif_face_contract::kSemantic &&
                 c.model_sha256 == jfif_face_contract::ModelSha256() &&
                 jfif_face_contract::Scene(c.scene_id) && c.result_capacity == 64 &&
                 c.graph_confidence == .1f && c.graph_iou == .35f)) &&
               c.output_layout != Layout::Unknown &&
               std::all_of(c.expected_sdk_codes.begin(), c.expected_sdk_codes.end(),
                           [](const SdkCodes& codes) { return codes.present; }) &&
               (c.model_stride_rule == ModelStrideRule::RequireExplicitMetadata ||
                c.model_stride_rule == ModelStrideRule::AllowUnconstrainedWithExplicitRuntimeIo);
    }
    std::string Sha256(const std::vector<std::uint8_t>& data) {
        std::array<std::uint8_t, CryptoPP::SHA256::DIGESTSIZE> digest{};
        CryptoPP::SHA256().CalculateDigest(digest.data(), data.data(), data.size());
        std::string hex;
        hex.reserve(digest.size() * 2);
        constexpr char kHex[] = "0123456789abcdef";
        for (auto byte : digest) {
            hex.push_back(kHex[byte >> 4]);
            hex.push_back(kHex[byte & 15]);
        }
        return hex;
    }
    bool Matches(const TensorMetadata& t, const std::string& name, ElementType type, Layout layout,
                 std::vector<std::size_t> shape, std::size_t bytes, std::vector<std::size_t> strides,
                 ModelStrideRule rule) {
        const bool stride_ok =
            t.strides == strides ||
            (t.strides.empty() && rule == ModelStrideRule::AllowUnconstrainedWithExplicitRuntimeIo);
        return t.name == name && t.type == type && t.layout == layout && t.shape == shape &&
               t.bytes == bytes && stride_ok;
    }
    bool Matches(const ModelMetadata& m, const Contract& c) {
        if (m.inputs.size() != 2 || m.outputs.size() != 1)
            return false;
        const std::size_t y = 2048 * 1080, uv = 2048 * 540;
        const std::size_t elements = (c.result_capacity + 1) * 6;
        const auto codes_match     = [](const SdkCodes& actual, const SdkCodes& expected) {
            return actual.present && expected.present && actual.dtype == expected.dtype &&
                   actual.layout == expected.layout && actual.memtype == expected.memtype &&
                   actual.colorspace == expected.colorspace;
        };
        return m.max_batch_size == c.expected_max_batch_size &&
               m.dynamic_batch_size == c.expected_dynamic_batch_size &&
               m.inputs[0].quantization_value == c.expected_quantization_values[0] &&
               m.inputs[1].quantization_value == c.expected_quantization_values[1] &&
               m.outputs[0].quantization_value == c.expected_quantization_values[2] &&
               codes_match(m.inputs[0].sdk_codes, c.expected_sdk_codes[0]) &&
               codes_match(m.inputs[1].sdk_codes, c.expected_sdk_codes[1]) &&
               codes_match(m.outputs[0].sdk_codes, c.expected_sdk_codes[2]) &&
               Matches(m.inputs[0], c.y_name, ElementType::Uint8, Layout::Nchw, {1, 1, 1080, 2048}, y,
                       {y, y, 2048, 1}, c.model_stride_rule) &&
               Matches(m.inputs[1], c.uv_name, ElementType::Uint8, Layout::Nchw, {1, 1, 540, 2048}, uv,
                       {uv, uv, 2048, 1}, c.model_stride_rule) &&
               Matches(m.outputs[0], c.output_name, ElementType::Float32, c.output_layout,
                       {1, c.result_capacity + 1, 6}, elements * sizeof(float), {elements, 6, 1},
                       c.model_stride_rule);
    }
}  // namespace

Capability::Capability(Contract c, std::shared_ptr<const std::vector<std::uint8_t>> bytes,
                       std::shared_ptr<LoadedModel> model, std::shared_ptr<State> active)
    : contract_(std::move(c)),
      bytes_(std::move(bytes)),
      model_(std::move(model)),
      active_(std::move(active)) {}
bool Capability::Active() const noexcept {
    return active_ && active_->active.load(std::memory_order_acquire);
}
std::shared_ptr<LoadedModel> Capability::AcquireModel() const noexcept {
    return Active() ? model_ : nullptr;
}
Capability::ExecutionLease Capability::AcquireForExecution() const {
    std::unique_lock<std::mutex> guard(active_->mutex);
    return ExecutionLease(std::move(guard), Active() ? model_ : nullptr);
}
Binding::~Binding() {
    Revoke();
}
bool Binding::Owns(const std::shared_ptr<const Capability>& capability) {
    std::lock_guard<std::mutex> lock(mutex_);
    return capability && active_ && capability->active_ == active_ && capability->Active();
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
                                                Loader& loader) {
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
        ModelMetadata metadata;
        if (!model || !model->ReadMetadata(metadata) || !Matches(metadata, contract))
            return nullptr;
        auto active     = std::make_shared<Capability::State>();
        auto capability = std::shared_ptr<const Capability>(new Capability(contract, owned, model, active));
        active_         = std::move(active);
        return capability;
    } catch (...) {
        return nullptr;
    }
}
}  // namespace cosmo::nn::axera::face_binding
