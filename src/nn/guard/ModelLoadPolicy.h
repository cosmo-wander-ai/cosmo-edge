#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace cosmo::nn {

/// Format selected solely from the first four bytes of a model file.
enum class ModelMagic {
    kUnknown,
    kCemc,
    kCenn,
    kLegacyEncrypted,
};

/// Native loader selected by the model consumer.
enum class ModelLoadIntent {
    kCosmoNn,
    kRawBmodel,
    kRawRknn,
};

/// Loader action selected from the file format and consumer intent.
enum class ModelLoadAction {
    kReject,
    kGuardV2,
    kNativeCenn,
    kNativeRawBmodel,
    kNativeRawRknn,
};

/// Stable reason for a rejected decision.
enum class ModelPolicyError {
    kNone,
    kPathNotRegularFile,
    kHeaderReadFailed,
    kFormatRejected,
};

/// Result of inspecting one model file.
struct ModelLoadDecision {
    ModelMagic magic       = ModelMagic::kUnknown;
    ModelLoadAction action = ModelLoadAction::kReject;
    ModelPolicyError error = ModelPolicyError::kNone;
    std::string model_path;

    [[nodiscard]] bool IsAllowed() const {
        return action != ModelLoadAction::kReject;
    }
};

/// Detect reserved model formats without host-endian integer conversion.
[[nodiscard]] ModelMagic DetectModelMagic(const std::array<std::uint8_t, 4>& bytes) noexcept;

/// Minimal format router. CEMC always goes through Guard, while native CENN and
/// raw bmodel/RKNN loading follow the caller's already selected model type.
class ModelLoadPolicy final {
public:
    [[nodiscard]] static ModelLoadPolicy Production();

    [[nodiscard]] ModelLoadDecision Evaluate(const std::string& model_path, ModelLoadIntent intent) const;

private:
    ModelLoadPolicy() = default;
};

}  // namespace cosmo::nn
