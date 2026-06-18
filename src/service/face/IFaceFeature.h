/// @file IFaceFeature.h
/// @brief Face feature extraction and comparison interface.
///        ISP split from IFaceLibService.
///        Consumed by AiRecognizer, FaceLib, PersonImport, MessageFaceLibHandler, app_init.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "service/face/dto/FaceLibFwd.h"
#include "service/media/dto/VideoFrameFwd.h"
#include "util/AiTypes.h"
#include "util/ErrorCode.h"

namespace cosmo::service {

/// Face feature extraction and comparison operations.
///
/// Provides NPU-accelerated face feature extraction from images, pairwise
/// feature similarity scoring, and 1:N face comparison against registered
/// face libraries.  JPEG codec operations live in IVideoFrameCodec.
class IFaceFeature {
public:
    virtual ~IFaceFeature() = default;

    // ── Feature Extraction ──

    /// Extract a face feature vector from an image.
    /// @param image    [in/out] Input image (may be modified for alignment).
    /// @param quality  Minimum face quality threshold.
    /// @param feature  [out] Extracted feature vector.
    /// @param cutImage [out] Cropped and aligned face image.
    /// @return ErrorEnum::kSuccess on success.
    virtual cosmo::util::ErrorEnum ExtractFaceFeature(VideoFramePtr& image, float quality,
                                                      cosmo::AiFeature& feature, VideoFramePtr& cutImage) = 0;

    // ── Feature Comparison ──

    /// Calculate similarity score between two face feature vectors.
    /// @param f1 First feature vector.
    /// @param f2 Second feature vector.
    /// @return Similarity score (higher = more similar).
    virtual float CalculateFaceScore(const cosmo::AiFeature& f1, const cosmo::AiFeature& f2) = 0;

    /// Convert a raw distance value to a normalized score.
    /// @param distance Raw feature distance.
    /// @return Normalized similarity score.
    virtual float GetFaceScore(float distance) = 0;

    /// Compare a face feature against all persons in the specified libraries.
    /// @param sets           List of face library IDs to search.
    /// @param feature        Feature vector to compare.
    /// @param info           [out] Best match result.
    /// @param param_limit_score Score threshold for a valid match.
    /// @return true if a match exceeding the threshold was found.
    virtual bool FaceCompare(std::vector<std::string> sets, cosmo::AiFeature& feature,
                             cosmo::AiDetectMatchHighScoreInfo& info, float param_limit_score) = 0;

    // ── Data Loading ──

    /// Load all face data (features) into memory from the database.
    virtual void LoadFaceData() = 0;

    /// Release face feature extraction models from VRAM.
    /// Call after library entry operations (single add / batch import) complete
    /// to free device memory for other models.
    virtual void ReleaseFaceModels() = 0;
};

}  // namespace cosmo::service
