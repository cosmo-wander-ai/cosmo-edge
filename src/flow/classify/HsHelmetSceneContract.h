#pragma once
// Private exploratory contract only. NPU ownership, frame witnesses, task/model
// lifecycle permits and publication remain the caller's responsibility.
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hshelmet_scene_candidate {
enum class Scene : int { NoHeadHelmet = 88112 };
enum class Profile { Strict, Recommended };
struct SceneSpec {
    Scene scene;
    std::string_view output_label;
    int reference_config_label_id;
    int reference_sidecar_label_id;
    int repository_scene_label_id;
    std::size_t tensor_channel;
    float reference_strict;
    float reference_recommended;
};
// IDs are metadata from distinct sources, not interchangeable tensor indices.
// The reference model/ONNX hash binding is unverified. Channel 1 is supported
// by a small number of visually inspected face examples, not field accuracy.
inline constexpr SceneSpec kNoHeadHelmet{Scene::NoHeadHelmet, "hatless", 1, 1, 1, 1, .997f, .924f};
inline constexpr const SceneSpec* Lookup(Scene scene) noexcept {
    return scene == Scene::NoHeadHelmet ? &kNoHeadHelmet : nullptr;
}
inline constexpr std::optional<float> ReferenceThreshold(Scene scene, Profile profile) noexcept {
    const auto* s = Lookup(scene);
    if (!s || (profile != Profile::Strict && profile != Profile::Recommended))
        return {};
    return profile == Profile::Strict ? s->reference_strict : s->reference_recommended;
}
inline bool IsProbability(float x) noexcept {
    return std::isfinite(x) && x >= 0.f && x <= 1.f;
}
// Contract fields are separately bound from the signed-off task template,
// reference mapping, actual model bytes, and runtime metadata by the caller.
// Equal names/shapes/hashes here do not establish .26-to-ONNX provenance.
struct Admission {
    Scene scene;
    std::string_view classifier_atomic_code;
    std::string_view detector_atomic_code;
    std::string_view scene_label;
    int scene_external_label_id;
    int reference_config_label_id;
    std::size_t tensor_channel;
    std::size_t tensor_channels;
    std::string_view frozen_model_sha256;
    std::string_view loaded_model_sha256;
};
inline bool ValidSha256(std::string_view x) noexcept {
    if (x.size() != 64)
        return false;
    bool nonzero = false;
    for (char c : x) {
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
            return false;
        if (c != '0')
            nonzero = true;
    }
    return nonzero;
}
inline bool ValidAdmission(const Admission& a) noexcept {
    const auto* s = Lookup(a.scene);
    return s && a.classifier_atomic_code == "2001002" && a.detector_atomic_code == "1001001" &&
           a.scene_label == s->output_label && a.scene_external_label_id == s->repository_scene_label_id &&
           a.reference_config_label_id == s->reference_config_label_id &&
           a.tensor_channel == s->tensor_channel && a.tensor_channels == 5 &&
           ValidSha256(a.frozen_model_sha256) && a.frozen_model_sha256 == a.loaded_model_sha256;
}
struct FrameKey {
    std::string channel;
    std::uint64_t generation{}, decoder_token{}, frame_index{};
    std::int64_t source_timestamp{};
};
inline bool operator==(const FrameKey& a, const FrameKey& b) noexcept {
    return a.channel == b.channel && a.generation == b.generation && a.decoder_token == b.decoder_token &&
           a.frame_index == b.frame_index && a.source_timestamp == b.source_timestamp;
}
inline bool ValidFrame(const FrameKey& f) noexcept {
    return !f.channel.empty() && f.generation != 0 && f.decoder_token != 0;
}
struct Roi {
    std::int32_t x0{}, y0{}, x1{}, y1{};
};
inline bool ValidRoi(const Roi& r) noexcept {
    // Exact graph admission: frame-contained, half-open, and crop height >= 1.
    if (r.x0 < 0 || r.y0 < 0 || r.x1 > 1920 || r.y1 > 1080 || r.x0 >= r.x1 || r.y0 >= r.y1)
        return false;
    const auto h = static_cast<std::int64_t>(r.y1) - r.y0;
    return h >= 2;
}
enum class Origin { Detector, PredictedOrUnknown };
struct InferenceKey {
    std::string task_id;
    std::uint64_t task_generation{}, model_generation{}, config_generation{};
    std::string model_sha256;
    bool operator==(const InferenceKey& o) const noexcept {
        return task_id == o.task_id && task_generation == o.task_generation &&
               model_generation == o.model_generation && config_generation == o.config_generation &&
               model_sha256 == o.model_sha256;
    }
    bool valid() const noexcept {
        return !task_id.empty() && task_generation && model_generation && config_generation &&
               ValidSha256(model_sha256);
    }
};
struct Observation {
    std::string target_id;
    FrameKey frame;
    InferenceKey inference;
    Roi roi;
    int person_class_id{1};
    std::string detector_atomic_code;
    std::string detector_label;
    Origin origin{Origin::PredictedOrUnknown};
    float detection_confidence{};
    std::uint8_t model_valid{};
    std::array<float, 5> scores{};
};
// Independently captured from the task-local post-filter/tracker snapshot,
// before any inference output exists. The engine adapter must retain that
// snapshot and cannot derive this roster from classifier results.
struct TargetBinding {
    std::string target_id;
    FrameKey frame;
    std::string task_id;
    std::uint64_t task_generation{};
    Roi roi;
    int person_class_id{1};
    std::string detector_atomic_code, detector_label;
    Origin origin{Origin::PredictedOrUnknown};
    float detection_confidence{};
};
inline bool SameBinding(const TargetBinding& e, const Observation& o) noexcept {
    return e.target_id == o.target_id && e.frame == o.frame && e.task_id == o.inference.task_id &&
           e.task_generation == o.inference.task_generation && e.roi.x0 == o.roi.x0 && e.roi.y0 == o.roi.y0 &&
           e.roi.x1 == o.roi.x1 && e.roi.y1 == o.roi.y1 && e.person_class_id == o.person_class_id &&
           e.detector_atomic_code == o.detector_atomic_code && e.detector_label == o.detector_label &&
           e.origin == o.origin && e.detection_confidence == o.detection_confidence;
}
struct Measurement {
    std::string target_id;
    FrameKey frame;
    Roi roi;
    Scene scene;
    std::string label;
    std::size_t tensor_channel;
    float confidence;
    float threshold;
    bool matched;  // Reference scene logic is GE, after caller resolves threshold.
};
// These are score/metadata operations only. Caller must prove frame witness,
// NPU completion and task/model permits, and publish atomically after staging.
inline std::optional<std::vector<Measurement>> Stage(const Admission& admission, const FrameKey& expected,
                                                     const InferenceKey& inference, float resolved_threshold,
                                                     const std::vector<TargetBinding>& expected_targets,
                                                     const std::vector<Observation>& observations,
                                                     std::size_t capacity = 64) noexcept {
    try {
        const auto* spec = Lookup(admission.scene);
        if (!ValidAdmission(admission) || !spec || !ValidFrame(expected) || !inference.valid() ||
            inference.model_sha256 != admission.frozen_model_sha256 || !IsProbability(resolved_threshold) ||
            !capacity || capacity > 64 || observations.size() > capacity ||
            expected_targets.size() != observations.size())
            return {};
        std::vector<Measurement> staged;
        staged.reserve(observations.size());
        for (std::size_t i = 0; i < observations.size(); ++i) {
            const auto& o = observations[i];
            if (!(o.inference == inference) || !SameBinding(expected_targets[i], o) || o.target_id.empty() ||
                !(o.frame == expected) || !ValidRoi(o.roi) || o.person_class_id != 1 ||
                o.detector_atomic_code != "1001001" || o.detector_label != "headShoulder" ||
                o.origin != Origin::Detector || !IsProbability(o.detection_confidence) || o.model_valid != 1)
                return {};
            for (const auto& prior : staged)
                if (prior.target_id == o.target_id)
                    return {};
            for (float score : o.scores)
                if (!IsProbability(score))
                    return {};
            const float score = o.scores[spec->tensor_channel];
            staged.push_back({o.target_id, o.frame, o.roi, admission.scene, std::string(spec->output_label),
                              spec->tensor_channel, score, resolved_threshold, score >= resolved_threshold});
        }
        return staged;
    } catch (...) {
        return {};
    }
}
}  // namespace hshelmet_scene_candidate
