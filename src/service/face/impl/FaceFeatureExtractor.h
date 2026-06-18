#pragma once

// FaceFeatureExtractor — Face feature extraction service.
//   1. HTTP API feature extraction:  face detect -> quality/angle -> landmark -> feature
//   2. Local feature extraction:     landmark -> feature

#include <list>
#include <memory>
#include <shared_mutex>
#include <string>
#include <vector>

#include "infer/AiClassifierUnify.h"
#include "infer/AiDetectorUnify.h"
#include "infer/AiLandmarkerUnify.h"
#include "infer/AiRecognizerUnify.h"
#include "media/VideoFrame.h"
#include "util/MsgBaseTypes.h"
#include "util/Thread.h"
#include "util/dto/CosmoFwd.h"
#include "util/dto/FilterTypes.h"
#include "util/dto/OverviewTypes.h"

namespace cosmo {

/// Face feature extraction service — detector, quality, landmark, recognizer pipeline.
/// Owned by FaceLibServiceImpl (no longer a singleton).
class FaceFeatureExtractor : public util::Thread {
public:
    FaceFeatureExtractor();
    ~FaceFeatureExtractor();

    void ServiceEnable();

    MsgGetFeaturesSend HandMsg(const MsgGetFeaturesRecv &data, std::error_condition &errc);
    std::error_condition HandFeatureImage(VideoFramePtr &image, float quality, AiFeature &feature,
                                          VideoFramePtr &cutImage);
    float CalculateScore(const AiFeature &feature1, const AiFeature &feature2);
    float GetScore(float distance);

    std::vector<float> GetScoreLevel();

protected:
    void run() override;

private:
    bool Init();
    bool Destroy();

    /// Generic model initializer: get config, create T(algCode, cfgPath, modelPath), call Init().
    template <typename T>
    bool InitModel(std::shared_ptr<T> &instance, const std::string &algCode, const char *modelLabel);

    MsgGetFeaturesFeature HandMsgImage(const MsgGetFeaturesImage &image, float quality);
    bool HandMsgFaceDetect(VideoFramePtr frame, MsgGetFeaturesFeature &feature,
                           std::vector<AiDetectRstEl> &rst);
    bool HandMsgQulityAngle(VideoFramePtr frame, MsgGetFeaturesFeature &feature,
                            std::vector<AiDetectRstEl> &inputFaces, float quality);
    bool HandMsgMask(VideoFramePtr frame, MsgGetFeaturesFeature &feature,
                     std::vector<AiDetectRstEl> &inputFaces);
    bool HandMsgFeature(VideoFramePtr frame, MsgGetFeaturesFeature &feature,
                        std::vector<AiDetectRstEl> &inputFaces);
    bool HandFeature(VideoFramePtr frame, AiFeature &feature, std::vector<AiDetectRstEl> &inputFaces);
    std::vector<AiDetectRstEl> FaceDetect(VideoFramePtr frame, bool &bInited);
    bool FaceQulityAngle(VideoFramePtr frame, std::vector<AiDetectRstEl> &ioPuts);
    bool FaceMask(VideoFramePtr frame, std::vector<AiDetectRstEl> &ioPuts);
    bool FaceLandmark(VideoFramePtr frame, std::vector<AiDetectRstEl> &ioPuts);
    bool FaceFeature(VideoFramePtr frame, std::vector<AiDetectRstEl> &ioPuts);

    bool GetQualityAngle(const std::vector<AiConfidence> &classifyRst, MsgGetFeaturesFeature &feature,
                         float quality);

    std::shared_mutex mtx_;
    std::string name_;
    bool is_running_{false};
    bool is_service_enabled_{true};
    bool is_ready_{false};

    float default_face_quality_{60.0f};
    AiDetectorUnifyPtr face_det_inst_{nullptr};        // Face detection
    AiClassifierUnifyPtr face_quality_inst_{nullptr};  // Face quality/angle
    AiClassifierUnifyPtr face_mask_inst_{nullptr};     // Mask classification
    AiLandmarkerUnifyPtr landmark_inst_{nullptr};      // Landmark positions
    AiRecognizerUnifyPtr recog_inst_{nullptr};         // Feature extraction
};
using FaceFeatureExtractorPtr = std::shared_ptr<FaceFeatureExtractor>;
}  // namespace cosmo
