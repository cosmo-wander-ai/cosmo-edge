#include "nn/guard/ModelLoadPolicy.h"

#include <filesystem>
#include <fstream>
#include <system_error>
#include <utility>

namespace cosmo::nn {
namespace {

    namespace fs = std::filesystem;

    constexpr std::array<std::uint8_t, 4> kCemcMagic            = {'C', 'E', 'M', 'C'};
    constexpr std::array<std::uint8_t, 4> kCennMagic            = {'C', 'E', 'N', 'N'};
    constexpr std::array<std::uint8_t, 4> kLegacyEncryptedMagic = {0x01, 0x00, 0x01, 0xec};

    bool ReadModelMagic(const fs::path& model_path, ModelMagic& magic) {
        std::ifstream input(model_path, std::ios::binary);
        std::array<std::uint8_t, 4> bytes{};
        if (!input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()))) {
            return false;
        }
        magic = DetectModelMagic(bytes);
        return true;
    }

    ModelLoadDecision Reject(ModelLoadDecision decision, ModelPolicyError error) {
        decision.action = ModelLoadAction::kReject;
        decision.error  = error;
        return decision;
    }

}  // namespace

ModelMagic DetectModelMagic(const std::array<std::uint8_t, 4>& bytes) noexcept {
    if (bytes == kCemcMagic) {
        return ModelMagic::kCemc;
    }
    if (bytes == kCennMagic) {
        return ModelMagic::kCenn;
    }
    if (bytes == kLegacyEncryptedMagic) {
        return ModelMagic::kLegacyEncrypted;
    }
    return ModelMagic::kUnknown;
}

ModelLoadPolicy ModelLoadPolicy::Production() {
    return ModelLoadPolicy();
}

ModelLoadDecision ModelLoadPolicy::Evaluate(const std::string& model_path, ModelLoadIntent intent) const {
    ModelLoadDecision decision;
    if (model_path.empty()) {
        return Reject(std::move(decision), ModelPolicyError::kPathNotRegularFile);
    }

    std::error_code error;
    const fs::path input_path(model_path);
    if (!fs::is_regular_file(fs::status(input_path, error)) || error) {
        return Reject(std::move(decision), ModelPolicyError::kPathNotRegularFile);
    }

    const fs::path absolute_path = fs::absolute(input_path, error).lexically_normal();
    if (error || absolute_path.empty()) {
        return Reject(std::move(decision), ModelPolicyError::kPathNotRegularFile);
    }
    decision.model_path = absolute_path.string();
    if (!ReadModelMagic(absolute_path, decision.magic)) {
        return Reject(std::move(decision), ModelPolicyError::kHeaderReadFailed);
    }

    if (decision.magic == ModelMagic::kCemc) {
        decision.action = ModelLoadAction::kGuardV2;
        return decision;
    }
    if (decision.magic == ModelMagic::kCenn && intent == ModelLoadIntent::kCosmoNn) {
        decision.action = ModelLoadAction::kNativeCenn;
        return decision;
    }
    if (decision.magic == ModelMagic::kUnknown && intent == ModelLoadIntent::kRawBmodel) {
        decision.action = ModelLoadAction::kNativeRawBmodel;
        return decision;
    }
    if (decision.magic == ModelMagic::kUnknown && intent == ModelLoadIntent::kRawRknn) {
        decision.action = ModelLoadAction::kNativeRawRknn;
        return decision;
    }
    return Reject(std::move(decision), ModelPolicyError::kFormatRejected);
}

}  // namespace cosmo::nn
