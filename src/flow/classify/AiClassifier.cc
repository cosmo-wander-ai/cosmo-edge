#include <cstdlib>

#include "nn/device/axera/axera_route_identity.h"
#ifdef COSMO_AXERA_REID_ROUTE_CANDIDATE
#include "flow/classify/ReidFeatureTask.h"
#include "nn/device/axera/reid/ReidModelBundle.h"
#endif
// AI classifier action implementation.
// Consumes data via registered processing queues.

#include "flow/classify/AiClassifier.h"
#include "flow/common/AlgDataRecord.h"
#include "flow/common/FlowTaskUtil.h"
#include "service/detail/ServiceRegistry.h"
#include "service/system/IConfigReadService.h"
#include "util/Log.h"
#ifdef COSMO_AXERA_HELMET_ROUTE_CANDIDATE
#include "flow/classify/HelmetBatchStage.h"
#ifdef COSMO_AXERA_SCENARIOBENCH_CANDIDATE
#include "nn/device/axera/benchmark/BenchCentralHelmet.h"
#include "nn/device/axera/benchmark/BenchHelmetExecutor.h"
#endif
#ifdef COSMO_AXERA_FACE_FEATURE_ROUTE_CANDIDATE
#include "flow/classify/FaceFeatureTask.h"
#endif
#ifdef COSMO_AXERA_HSHELMET_ROUTE_CANDIDATE
#include "flow/classify/HsHelmetBatchStage.h"
#include "flow/classify/HsHelmetResultCommit.h"
#include "nn/device/axera/axera_hshelmet_model_loader.h"
#include "nn/device/axera/hshelmet/HsHelmetSdkExecutor.h"
#endif
#include "flow/classify/HelmetResultCommit.h"
#include "nn/device/axera/axera_helmet_model_loader.h"
#include "nn/device/axera/helmet/HelmetSdkExecutor.h"
#ifdef COSMO_AXERA_SLEEP_ROUTE_CANDIDATE
#include "flow/classify/SleepBatchStage.h"
#include "flow/classify/SleepResultCommit.h"
#include "nn/device/axera/axera_sleep_model_loader.h"
#include "nn/device/axera/helmet/SleepSdkExecutor.h"
#endif
#include "util/UuidUtil.h"
#ifdef COSMO_AXERA_PHONE_ROUTE_CANDIDATE
#include "flow/classify/PhoneBatchStage.h"
#include "flow/classify/PhoneResultCommit.h"
#include "nn/device/axera/axera_phone_model_loader.h"
#include "nn/device/axera/phone/PhoneSdkExecutor.h"
#ifdef COSMO_AXERA_GARMENT_ROUTE_CANDIDATE
#include "flow/classify/GarmentBatchStage.h"
#include "flow/classify/GarmentResultCommit.h"
#include "nn/device/axera/axera_garment_model_loader.h"
#include "nn/device/axera/garment/GarmentSdkExecutor.h"
#endif
#ifdef COSMO_AXERA_ELECTRIC_ROUTE_CANDIDATE
#include "flow/classify/ElectricBatchStage.h"
#include "flow/classify/ElectricResultCommit.h"
#include "nn/device/axera/axera_electric_model_loader.h"
#include "nn/device/axera/electric/ElectricSdkExecutor.h"
#endif
#ifdef COSMO_AXERA_VEHICLECOLOR_ROUTE_CANDIDATE
#include "flow/classify/VehicleColorBatchStage.h"
#include "flow/classify/VehicleColorResultCommit.h"
#include "nn/device/axera/axera_vehiclecolor_model_loader.h"
#include "nn/device/axera/vehiclecolor/VehicleColorSdkExecutor.h"
#endif
#ifdef COSMO_AXERA_FACEQUALITY_ROUTE_CANDIDATE
#include "flow/classify/FaceQualityBatchStage.h"
#include "flow/classify/FaceQualityResultCommit.h"
#include "nn/device/axera/axera_facequality_model_loader.h"
#include "nn/device/axera/facequality/FaceQualitySdkExecutor.h"
#endif
#ifdef COSMO_AXERA_MASK_ROUTE_CANDIDATE
#include "flow/classify/MaskBatchStage.h"
#include "flow/classify/MaskResultCommit.h"
#include "nn/device/axera/axera_mask_model_loader.h"
#include "nn/device/axera/mask/MaskSdkExecutor.h"
#endif
#endif
#include <stdexcept>
#ifdef COSMO_AXERA_FALL_ROUTE_CANDIDATE
#include "flow/classify/FallBatchStage.h"
#include "flow/classify/FallResultCommit.h"
#include "nn/device/axera/axera_fall_model_loader.h"
#include "nn/device/axera/helmet/FallSdkExecutor.h"
#endif
#endif

namespace cosmo {

AiClassifier::AiClassifier(const std::string& taskId, ActionNode& action)
    : AiClassifierBase(AlgActionType::AlgActionAiClassify, taskId, action, "AI-CLASSIFIER ",
                       "classify_" + action.atomicCode) {
    batch_count_ = 1;
    LOG_INFO("{}[{} {} {}] Init ", log_tag_, GetTaskId(), GetName(), uuid);
}

AiClassifier::~AiClassifier() {
#ifdef COSMO_AXERA_HELMET_ROUTE_CANDIDATE
    // Candidate model must be joined before derived members are destroyed.
    if (native_helmet_enabled_)
        Stop();
#endif
}

#ifdef COSMO_AXERA_HELMET_ROUTE_CANDIDATE
bool AiClassifier::Start() {
    if (!native_helmet_enabled_)
        return AiClassifierBase::Start();
    std::lock_guard<std::mutex> control(native_helmet_control_mutex_);
    {
        std::lock_guard<std::mutex> lock(native_helmet_start_mutex_);
        if (running)
            return native_helmet_start_done_ && native_helmet_start_ok_;
        native_helmet_start_done_ = false;
        native_helmet_start_ok_   = false;
    }
    if (!AiClassifierBase::Start())
        return false;
    bool ok;
    {
        std::unique_lock<std::mutex> lock(native_helmet_start_mutex_);
        native_helmet_start_cv_.wait(lock, [this] { return native_helmet_start_done_; });
        ok = native_helmet_start_ok_;
    }
    if (!ok)
        AiClassifierBase::Stop();  // joins failed worker on this control thread
    return ok;
}
void AiClassifier::Stop() {
    if (!native_helmet_enabled_) {
        AiClassifierBase::Stop();
        return;
    }
    std::lock_guard<std::mutex> control(native_helmet_control_mutex_);
    AiClassifierBase::Stop();
}
bool AiClassifier::ConfigureNativeHelmet(std::shared_ptr<const std::vector<std::uint8_t>> bytes) {
    if (running || !bytes || bytes->empty() || batch_count_ != 1 || alg_code_ != "2001036")
        return false;
    if (native_helmet_enabled_)
        return native_helmet_bytes_ && *native_helmet_bytes_ == *bytes;
    native_helmet_bytes_   = std::move(bytes);
    native_helmet_enabled_ = true;
    return true;
}
#ifdef COSMO_AXERA_REID_ROUTE_CANDIDATE
bool AiClassifier::ConfigureNativeReid(std::array<std::shared_ptr<const std::vector<std::uint8_t>>, 2> bytes,
                                       const std::string& task,
                                       std::shared_ptr<const std::vector<std::uint8_t>> quality) {
    if (running || batch_count_ != 1 || alg_code_ != "1001007" || task.empty() || !bytes[0] || !bytes[1] ||
        bytes[0]->empty() || bytes[1]->empty())
        return false;
    if (native_helmet_enabled_) {
        const bool same =
            native_reid_enabled_ && native_reid_task_ == task && native_reid_bytes_[0] &&
            native_reid_bytes_[1] && *native_reid_bytes_[0] == *bytes[0] &&
            *native_reid_bytes_[1] == *bytes[1] &&
            ((!native_reid_quality_bytes_ && !quality) ||
             (native_reid_quality_bytes_ && quality && *native_reid_quality_bytes_ == *quality));
        if (same)
            native_reid_ranking_.Reset();
        return same;
    }
    if (quality && quality->empty())
        return false;
    native_reid_quality_bytes_ = std::move(quality);
    native_reid_ranking_.Reset();
    native_helmet_bytes_   = bytes[0];
    native_reid_bytes_     = std::move(bytes);
    native_reid_task_      = task;
    native_reid_enabled_   = true;
    native_helmet_enabled_ = true;
    return true;
}
#endif
#ifdef COSMO_AXERA_FACE_FEATURE_ROUTE_CANDIDATE
bool AiClassifier::ConfigureNativeFaceFeature(
    std::array<std::shared_ptr<const std::vector<std::uint8_t>>, 5> bytes, const std::string& route_task_id) {
    if (running || batch_count_ != 1 || alg_code_ != "1000005" || route_task_id.empty())
        return false;
    for (const auto& b : bytes)
        if (!b || b->empty())
            return false;
    if (native_helmet_enabled_) {
        if (!native_face_feature_enabled_ || native_face_feature_task_ != route_task_id)
            return false;
        for (size_t i = 0; i < bytes.size(); ++i) {
            if (!native_face_feature_bytes_[i] || *native_face_feature_bytes_[i] != *bytes[i])
                return false;
        }
        return true;
    }
    native_helmet_bytes_         = bytes[0];
    native_face_feature_bytes_   = std::move(bytes);
    native_face_feature_task_    = route_task_id;
    native_face_feature_enabled_ = true;
    native_helmet_enabled_       = true;
    return true;
}
#endif
#ifdef COSMO_AXERA_FALL_ROUTE_CANDIDATE
bool AiClassifier::ConfigureNativeFall(std::shared_ptr<const std::vector<std::uint8_t>> bytes) {
    if (running || !bytes || bytes->empty() || batch_count_ != 1 || alg_code_ != "2002008")
        return false;
    if (native_helmet_enabled_)
        return native_fall_enabled_ && native_helmet_bytes_ && *native_helmet_bytes_ == *bytes;
    native_helmet_bytes_   = std::move(bytes);
    native_fall_enabled_   = true;
    native_helmet_enabled_ = true;
    return true;
}
#endif
#ifdef COSMO_AXERA_ELECTRIC_ROUTE_CANDIDATE
bool AiClassifier::ConfigureNativeElectric(std::shared_ptr<const std::vector<std::uint8_t>> bytes,
                                           electric_scene_candidate::Scene scene, float threshold,
                                           const std::string& route_task_id) {
    if (running || !bytes || bytes->empty() || batch_count_ != 1 || alg_code_ != "2000020" ||
        !electric_scene_candidate::Lookup(scene) || !electric_scene_candidate::IsProbability(threshold) ||
        route_task_id.empty())
        return false;
    if (native_helmet_enabled_)
        return native_electric_enabled_ && native_electric_scene_ == scene &&
               native_electric_threshold_ == threshold && native_electric_task_ == route_task_id &&
               native_helmet_bytes_ && *native_helmet_bytes_ == *bytes;
    native_helmet_bytes_       = std::move(bytes);
    native_electric_enabled_   = true;
    native_helmet_enabled_     = true;
    native_electric_scene_     = scene;
    native_electric_threshold_ = threshold;
    native_electric_task_      = route_task_id;
    return true;
}
#endif
#ifdef COSMO_AXERA_VEHICLECOLOR_ROUTE_CANDIDATE
bool AiClassifier::ConfigureNativeVehicleColor(std::shared_ptr<const std::vector<std::uint8_t>> bytes,
                                               vehiclecolor_scene_candidate::Scene scene, float threshold,
                                               const std::string& route_task_id) {
    if (running || !bytes || bytes->empty() || batch_count_ != 1 || alg_code_ != "2000011" ||
        !vehiclecolor_scene_candidate::Lookup(scene) ||
        !vehiclecolor_scene_candidate::IsProbability(threshold) || route_task_id.empty())
        return false;
    if (native_helmet_enabled_)
        return native_vehiclecolor_enabled_ && native_vehiclecolor_scene_ == scene &&
               native_vehiclecolor_threshold_ == threshold && native_vehiclecolor_task_ == route_task_id &&
               native_helmet_bytes_ && *native_helmet_bytes_ == *bytes;
    native_helmet_bytes_           = std::move(bytes);
    native_vehiclecolor_enabled_   = true;
    native_helmet_enabled_         = true;
    native_vehiclecolor_scene_     = scene;
    native_vehiclecolor_threshold_ = threshold;
    native_vehiclecolor_task_      = route_task_id;
    return true;
}
#endif
#ifdef COSMO_AXERA_FACEQUALITY_ROUTE_CANDIDATE
bool AiClassifier::ConfigureNativeFaceQuality(std::shared_ptr<const std::vector<std::uint8_t>> bytes,
                                              facequality_scene_candidate::Scene scene, float threshold,
                                              const std::string& route_task_id) {
    if (running || !bytes || bytes->empty() || batch_count_ != 1 || alg_code_ != "1000012" ||
        !facequality_scene_candidate::Lookup(scene) ||
        !facequality_scene_candidate::IsProbability(threshold) || route_task_id.empty())
        return false;
    if (native_helmet_enabled_)
        return native_facequality_enabled_ && native_facequality_scene_ == scene &&
               native_facequality_threshold_ == threshold && native_facequality_task_ == route_task_id &&
               native_helmet_bytes_ && *native_helmet_bytes_ == *bytes;
    native_helmet_bytes_          = std::move(bytes);
    native_facequality_enabled_   = true;
    native_helmet_enabled_        = true;
    native_facequality_scene_     = scene;
    native_facequality_threshold_ = threshold;
    native_facequality_task_      = route_task_id;
    return true;
}
#endif
#ifdef COSMO_AXERA_MASK_ROUTE_CANDIDATE
bool AiClassifier::ConfigureNativeMask(std::shared_ptr<const std::vector<std::uint8_t>> bytes,
                                       mask_scene_candidate::Scene scene, float threshold,
                                       const std::string& route_task_id) {
    if (running || !bytes || bytes->empty() || batch_count_ != 1 || alg_code_ != "1000010" ||
        !mask_scene_candidate::Lookup(scene) || !mask_scene_candidate::IsProbability(threshold) ||
        route_task_id.empty())
        return false;
    if (native_helmet_enabled_)
        return native_mask_enabled_ && native_mask_scene_ == scene && native_mask_threshold_ == threshold &&
               native_mask_task_ == route_task_id && native_helmet_bytes_ && *native_helmet_bytes_ == *bytes;
    native_helmet_bytes_   = std::move(bytes);
    native_mask_enabled_   = true;
    native_helmet_enabled_ = true;
    native_mask_scene_     = scene;
    native_mask_threshold_ = threshold;
    native_mask_task_      = route_task_id;
    return true;
}
#endif
#ifdef COSMO_AXERA_HSHELMET_ROUTE_CANDIDATE
bool AiClassifier::ConfigureNativeHsHelmet(std::shared_ptr<const std::vector<std::uint8_t>> bytes,
                                           hshelmet_scene_candidate::Scene scene, float threshold,
                                           const std::string& route_task_id) {
    if (running || !bytes || bytes->empty() || batch_count_ != 1 || alg_code_ != "2001002" ||
        !hshelmet_scene_candidate::Lookup(scene) || !hshelmet_scene_candidate::IsProbability(threshold) ||
        route_task_id.empty())
        return false;
    if (native_helmet_enabled_)
        return native_hshelmet_enabled_ && native_hshelmet_scene_ == scene &&
               native_hshelmet_threshold_ == threshold && native_hshelmet_task_ == route_task_id &&
               native_helmet_bytes_ && *native_helmet_bytes_ == *bytes;
    native_helmet_bytes_       = std::move(bytes);
    native_hshelmet_enabled_   = true;
    native_helmet_enabled_     = true;
    native_hshelmet_scene_     = scene;
    native_hshelmet_threshold_ = threshold;
    native_hshelmet_task_      = route_task_id;
    return true;
}
#endif
#ifdef COSMO_AXERA_PHONE_ROUTE_CANDIDATE
bool AiClassifier::ConfigureNativePhone(std::shared_ptr<const std::vector<std::uint8_t>> bytes,
                                        phone_scene_candidate::Scene scene, float threshold,
                                        const std::string& route_task_id) {
    if (running || !bytes || bytes->empty() || batch_count_ != 1 || alg_code_ != "2002011" ||
        !phone_scene_candidate::Lookup(scene) || !phone_scene_candidate::IsProbability(threshold) ||
        route_task_id.empty())
        return false;
    if (native_helmet_enabled_)
        return native_phone_enabled_ && native_phone_scene_ == scene &&
               native_phone_threshold_ == threshold && native_phone_task_ == route_task_id &&
               native_helmet_bytes_ && *native_helmet_bytes_ == *bytes;
    native_helmet_bytes_    = std::move(bytes);
    native_phone_enabled_   = true;
    native_helmet_enabled_  = true;
    native_phone_scene_     = scene;
    native_phone_threshold_ = threshold;
    native_phone_task_      = route_task_id;
    return true;
}
#endif
#ifdef COSMO_AXERA_GARMENT_ROUTE_CANDIDATE
bool AiClassifier::ConfigureNativeGarment(std::shared_ptr<const std::vector<std::uint8_t>> bytes,
                                          garment_scene_candidate::Scene scene, float threshold,
                                          const std::string& route_task_id) {
    if (running || !bytes || bytes->empty() || batch_count_ != 1 || alg_code_ != "1001005" ||
        !garment_scene_candidate::Lookup(scene) || !garment_scene_candidate::IsProbability(threshold) ||
        route_task_id.empty())
        return false;
    if (native_helmet_enabled_)
        return native_garment_enabled_ && native_garment_scene_ == scene &&
               native_garment_threshold_ == threshold && native_garment_task_ == route_task_id &&
               native_helmet_bytes_ && *native_helmet_bytes_ == *bytes;
    native_helmet_bytes_      = std::move(bytes);
    native_garment_enabled_   = true;
    native_helmet_enabled_    = true;
    native_garment_scene_     = scene;
    native_garment_threshold_ = threshold;
    native_garment_task_      = route_task_id;
    return true;
}
#endif
#ifdef COSMO_AXERA_SLEEP_ROUTE_CANDIDATE
bool AiClassifier::ConfigureNativeSleep(std::shared_ptr<const std::vector<std::uint8_t>> bytes) {
    if (running || !bytes || bytes->empty() || batch_count_ != 1 || alg_code_ != "1001010")
        return false;
    if (native_helmet_enabled_)
        return IsNativeSleep() && native_helmet_bytes_ && *native_helmet_bytes_ == *bytes;
    native_helmet_bytes_   = std::move(bytes);
    native_helmet_enabled_ = true;
    return true;
}
#endif
bool AiClassifier::NativeHelmetWorkerStart() {
    bool ok = false;
    try {
        if (native_helmet_bytes_) {
#ifdef COSMO_AXERA_SCENARIOBENCH_CANDIDATE
            if (native_bench_helmet_enabled_) {
                if (bench_ax::BenchmarkEngineWorkerEnabled())
                    native_helmet_model_ = bench_ax::LoadCentralHelmet(native_helmet_bytes_->data(),
                                                                       native_helmet_bytes_->size());
                else
                    native_helmet_model_ =
                        bench_ax::LoadHelmetModel(native_helmet_bytes_->data(), native_helmet_bytes_->size());
            } else
#endif
#ifdef COSMO_AXERA_REID_ROUTE_CANDIDATE
                if (native_reid_enabled_) {
                std::array<helmet_candidate::ReidModelBytes, 2> inputs{};
                for (std::size_t i = 0; i < 2; ++i)
                    inputs[i] = {native_reid_bytes_[i]->data(), native_reid_bytes_[i]->size()};
                native_reid_models_ = helmet_candidate::LoadReidBundle(
                    inputs, helmet_candidate::ReidInputProfile::Video709Limited,
                    native_reid_quality_bytes_
                        ? helmet_candidate::ReidModelBytes{native_reid_quality_bytes_->data(),
                                                           native_reid_quality_bytes_->size()}
                        : helmet_candidate::ReidModelBytes{});
                if (native_reid_models_)
                    native_helmet_model_ = native_reid_models_;
            } else
#endif
#ifdef COSMO_AXERA_FACE_FEATURE_ROUTE_CANDIDATE
                if (native_face_feature_enabled_) {
                std::array<helmet_candidate::FaceModelBytes, 5> inputs{};
                for (std::size_t i = 0; i < inputs.size(); ++i)
                    inputs[i] = {native_face_feature_bytes_[i]->data(),
                                 native_face_feature_bytes_[i]->size()};
                native_face_feature_models_ = helmet_candidate::LoadFaceModelBundle(
                    helmet_candidate::FaceInputProfile::Video709Limited, inputs);
                if (native_face_feature_models_)
                    native_helmet_model_ = native_face_feature_models_->models[0];
            } else
#endif
#ifdef COSMO_AXERA_VEHICLECOLOR_ROUTE_CANDIDATE
                if (native_vehiclecolor_enabled_)
                native_helmet_model_ = helmet_candidate::LoadVehicleColorModel(native_helmet_bytes_->data(),
                                                                               native_helmet_bytes_->size());
            else
#endif
#ifdef COSMO_AXERA_ELECTRIC_ROUTE_CANDIDATE
                if (native_electric_enabled_)
                native_helmet_model_ = helmet_candidate::LoadElectricModel(native_helmet_bytes_->data(),
                                                                           native_helmet_bytes_->size());
            else
#endif
#ifdef COSMO_AXERA_HSHELMET_ROUTE_CANDIDATE
                if (native_hshelmet_enabled_)
                native_helmet_model_ = helmet_candidate::LoadHsHelmetModel(native_helmet_bytes_->data(),
                                                                           native_helmet_bytes_->size());
            else
#endif
#ifdef COSMO_AXERA_FACEQUALITY_ROUTE_CANDIDATE
                if (native_facequality_enabled_)
                native_helmet_model_ = helmet_candidate::LoadFaceQualityModel(native_helmet_bytes_->data(),
                                                                              native_helmet_bytes_->size());
            else
#endif
#ifdef COSMO_AXERA_MASK_ROUTE_CANDIDATE
                if (native_mask_enabled_)
                native_helmet_model_ = helmet_candidate::LoadMaskModel(native_helmet_bytes_->data(),
                                                                       native_helmet_bytes_->size());
            else
#endif
#ifdef COSMO_AXERA_GARMENT_ROUTE_CANDIDATE
                if (native_garment_enabled_)
                native_helmet_model_ = helmet_candidate::LoadGarmentModel(native_helmet_bytes_->data(),
                                                                          native_helmet_bytes_->size());
            else
#endif
#ifdef COSMO_AXERA_PHONE_ROUTE_CANDIDATE
                if (native_phone_enabled_)
                native_helmet_model_ = helmet_candidate::LoadPhoneModel(native_helmet_bytes_->data(),
                                                                        native_helmet_bytes_->size());
            else
#endif
#ifdef COSMO_AXERA_SLEEP_ROUTE_CANDIDATE
                if (IsNativeSleep())
                native_helmet_model_ = helmet_candidate::LoadSleepModel(native_helmet_bytes_->data(),
                                                                        native_helmet_bytes_->size());
            else
#endif
#ifdef COSMO_AXERA_FALL_ROUTE_CANDIDATE
                if (native_fall_enabled_)
                native_helmet_model_ = helmet_candidate::LoadFallModel(native_helmet_bytes_->data(),
                                                                       native_helmet_bytes_->size());
            else
#endif
                native_helmet_model_ = helmet_candidate::LoadHelmetModel(native_helmet_bytes_->data(),
                                                                         native_helmet_bytes_->size());
        }
        ok = static_cast<bool>(native_helmet_model_);
    } catch (...) {
        ok = false;
    }
    {
        std::lock_guard<std::mutex> lock(native_helmet_start_mutex_);
        native_helmet_start_ok_   = ok;
        native_helmet_start_done_ = true;
    }
    native_helmet_start_cv_.notify_all();
    return ok;
}
void AiClassifier::NativeHelmetWorkerStop() noexcept {
    native_helmet_model_.reset();
#ifdef COSMO_AXERA_FACE_FEATURE_ROUTE_CANDIDATE
    native_face_feature_models_.reset();
#ifdef COSMO_AXERA_REID_ROUTE_CANDIDATE
    native_reid_models_.reset();
#endif
#endif
    try {
        std::lock_guard<std::mutex> lock(native_helmet_start_mutex_);
        native_helmet_start_ok_ = false;
    } catch (...) {
    }
}
#endif

bool AiClassifier::CheckDataAvailable(AlgDataPtr algData) {
    if (!algData) {
        invalid_frame_cnt += 1;
        if (0 == invalid_frame_cnt % 100) {
            LOG_WARN("{}[{} {} {}] Filter {} Frames", log_tag_, GetTaskId(), GetName(), uuid,
                     invalid_frame_cnt);
        }
        action_status = util::ErrorEnum::FlowDataInvalid;
        return false;
    }
#ifdef COSMO_NN_USE_AXERA_BACKEND
#ifdef COSMO_AXERA_REID_ROUTE_CANDIDATE
    if (alg_code_ == "1001007" && !native_reid_enabled_) {
        action_status = util::ErrorEnum::AI_INST_NOTCREATED;
        return false;
    }
#endif
    if (alg_code_ == "1001005") {
#ifdef COSMO_AXERA_GARMENT_ROUTE_CANDIDATE
        if (!native_garment_enabled_) {
            action_status = util::ErrorEnum::AI_INST_NOTCREATED;
            return false;
        }
#else
        action_status = util::ErrorEnum::AI_INST_NOTCREATED;
        return false;
#endif
    }
    if (alg_code_ == "2000011") {
#ifdef COSMO_AXERA_VEHICLECOLOR_ROUTE_CANDIDATE
        if (!native_vehiclecolor_enabled_) {
            action_status = util::ErrorEnum::AI_INST_NOTCREATED;
            return false;
        }
#else
        action_status = util::ErrorEnum::AI_INST_NOTCREATED;
        return false;
#endif
    }
    if (alg_code_ == "1000012") {
#ifdef COSMO_AXERA_FACEQUALITY_ROUTE_CANDIDATE
        if (!native_facequality_enabled_) {
            action_status = util::ErrorEnum::AI_INST_NOTCREATED;
            return false;
        }
#else
        action_status = util::ErrorEnum::AI_INST_NOTCREATED;
        return false;
#endif
    }
    if (alg_code_ == "2000020") {
#ifdef COSMO_AXERA_ELECTRIC_ROUTE_CANDIDATE
        if (!native_electric_enabled_) {
            action_status = util::ErrorEnum::AI_INST_NOTCREATED;
            return false;
        }
#else
        action_status = util::ErrorEnum::AI_INST_NOTCREATED;
        return false;
#endif
    }
#endif
#ifdef COSMO_AXERA_HELMET_ROUTE_CANDIDATE
    if (native_helmet_enabled_) {
        if (!native_helmet_model_ || !algData->native_helmet_ticket) {
            action_status = util::ErrorEnum::AI_INST_NOTCREATED;
            return false;
        }
    } else if (algData->native_helmet_ticket) {
        action_status = util::ErrorEnum::AI_FORWARD_FAILED;
        return false;
    }
    if (!classifier_ && !native_helmet_enabled_) {
#else
    if (!classifier_) {
#endif
        invalid_frame_cnt += 1;
        if (0 == invalid_frame_cnt % 100) {
            LOG_WARN("{}[{} {} {}] Filter {} Frames", log_tag_, GetTaskId(), GetName(), uuid,
                     invalid_frame_cnt);
        }
        action_status = util::ErrorEnum::AI_INST_NOTCREATED;
        if (!AiSdkInit()) {
            return false;
        }
    }

    if (!algData->chanDataDec.frame || !algData->chanDataDec.frame->Active()) {
        action_status = util::ErrorEnum::FrameDataInvalid;
        return false;
    }

    // Validate the incoming data type is supported for classification.
    if (!((AlgDataType::ChannelDataDec == algData->dataType) ||
          (AlgDataType::ChannelDataDetect == algData->dataType) ||
          (AlgDataType::TaskDataTrack == algData->dataType) ||
          (AlgDataType::TaskDataPersonFace == algData->dataType) ||
          (AlgDataType::TaskDataClassify == algData->dataType) ||
          (AlgDataType::TaskDataLandmark == algData->dataType))) {
        invalid_frame_cnt += 1;
        if (0 == invalid_frame_cnt % 100) {
            LOG_WARN("{}[{} {} {}] Filter {} Frames dataType:{}", log_tag_, GetTaskId(), GetName(), uuid,
                     invalid_frame_cnt, algData->dataType);
        }
        action_status = util::ErrorEnum::FlowDataInvalid;
        return false;
    }

    return true;
}

#ifdef COSMO_AXERA_HELMET_ROUTE_CANDIDATE
bool AiClassifier::HandNativeHelmet(AlgDataPtr data) {
    using namespace helmet_candidate;
    if (!data || !native_helmet_model_ || !data->native_helmet_ticket ||
        data->dataType != AlgDataType::TaskDataTrack || !data->native_helmet_ticket->ReadyForClassifier())
        return false;
    auto input = data->GetTaskResult(AlgDataType::TaskDataTrack);
    if (!input)
        return false;
    auto ticket = std::move(data->native_helmet_ticket);
    auto frame  = ticket->Claim();
    if (!frame || !frame->frame_witness || !frame->frame_witness->Matches(*frame) || input->frameIndex < 0 ||
        frame->frame_index != static_cast<std::uint64_t>(input->frameIndex) ||
        frame->source_timestamp != input->timestamp)
        return false;
    auto task  = ticket->TakeTaskPermit();
    auto model = ticket->TakeModelPermit();
    if (!task || !model)
        return false;
    std::shared_ptr<NativeHelmetTicket> lease(std::move(ticket));
    const FrameKey key{data->channelId, frame->stream_generation, frame->decoder_token, frame->frame_index,
                       frame->source_timestamp};
    std::vector<BoundTarget> targets;
    std::vector<Roi> rois;
    targets.reserve(input->targets.size());
    rois.reserve(input->targets.size());
    for (const auto& target : input->targets) {
        targets.push_back({target, key});
        if (target.classId != 1 || ClassifierEligibility(target, input->bHaveArea) == Eligibility::Skip)
            continue;
        if (target.confidence.source != AiAnnotationSource::Detection ||
            target.confidence.atomic_code != "1001003" || target.confidence.label != "pedestrian")
            return false;
        auto roi = BoxToRoi(target.relatedEl.bActive ? target.relatedEl.box : target.box, 1920, 1080);
        if (!roi || rois.size() >= 64)
            return false;
        rois.push_back(*roi);
    }
    std::vector<std::array<float, 5>> scores;
    if (!rois.empty()) {
        struct IoMarker final : IoOwner {};
        struct MapMarker final : MappingOwner {};
        auto pending           = std::make_unique<PendingResources>();
        pending->frame         = frame;
        pending->frame_lease   = lease;
        pending->model         = native_helmet_model_;
        pending->handle        = native_helmet_model_->HandleForOwnerThread();
        pending->owner_thread  = std::this_thread::get_id();
        pending->task_permit   = task;
        pending->model_permit  = model;
        pending->io_owner      = std::make_shared<IoMarker>();
        pending->mapping_owner = std::make_shared<MapMarker>();
        pending->sdk           = std::make_shared<HelmetSystemSdk>(
            [counters = execution_counters_](AX_ENGINE_HANDLE h, AX_ENGINE_IO_T* io) {
                AX_S32 rc = -1;
                counters->RunForward([&]() {
                    rc = AX_ENGINE_RunSync(h, io);
                    return rc == 0;
                });
                return rc;
            });
        auto result = ExecuteHelmetSdkBatch(std::move(pending), rois);
        if (result.result == GuardResult::CompletionUnknown || result.result == GuardResult::Poisoned)
            throw std::runtime_error("native helmet device completion unknown");
        if (result.result != GuardResult::Complete || result.scores.size() != rois.size() ||
            !result.task_permit || !result.model_permit)
            return false;
        task   = std::move(result.task_permit);
        model  = std::move(result.model_permit);
        scores = std::move(result.scores);
    } else if (!task->permit.TryBegin() || !model->permit.TryBegin()) {
        return false;
    }
    const auto eligible = [&](const AiDetectRstEl& t) { return ClassifierEligibility(t, input->bHaveArea); };
    const auto current  = [&]() { return task->permit.StillCurrent() && model->permit.StillCurrent(); };
    std::size_t cursor  = 0;
    const auto fetch    = [&](const AiDetectRstEl&, const Roi& roi, std::array<float, 5>& out) {
        if (cursor >= scores.size() || roi != rois[cursor])
            return RunStatus::KnownFailure;
        out = scores[cursor++];
        return RunStatus::Complete;
    };
    // SDK work has definitively completed before staged result construction.
    const auto isolate = []() noexcept {};
    const Thresholds reference_thresholds{};
    auto batch = Stage(targets, key, 64, reference_thresholds, current, eligible, fetch, isolate);
    if (batch.status != BatchStatus::Complete || cursor != scores.size())
        return false;
    const auto next_id = []() { return util::GenerateUUID(); };
    auto prepared      = Materialize(targets, key, batch, reference_thresholds, {}, eligible, next_id);
    if (!prepared)
        return false;
    TaskOutput sink;
    if (!CommitPrepared(task->permit, model->permit, sink, prepared))
        return false;
    auto output = AlgDataCopy(data);
    if (!output)
        return false;
    output->dataType      = AlgDataType::TaskDataClassify;
    output->bHaveClassify = true;
    auto classify         = std::make_shared<DataDetTrackClassify>(*input);
    classify->targets.swap(sink);
    output->SetTaskResult(AlgDataType::TaskDataClassify, std::move(classify));
    RecordHistory(output);
    distributor->DistributorData(output);
    return true;
}
#ifdef COSMO_AXERA_FALL_ROUTE_CANDIDATE
bool AiClassifier::HandNativeFall(AlgDataPtr data) {
    using namespace fall_candidate;
    if (!data || !native_helmet_model_ || !data->native_helmet_ticket ||
        data->dataType != AlgDataType::TaskDataTrack || !data->native_helmet_ticket->ReadyForClassifier())
        return false;
    auto input = data->GetTaskResult(AlgDataType::TaskDataTrack);
    if (!input)
        return false;
    auto ticket = std::move(data->native_helmet_ticket);
    auto frame  = ticket->Claim();
    if (!frame || !frame->frame_witness || !frame->frame_witness->Matches(*frame) || input->frameIndex < 0 ||
        frame->frame_index != static_cast<std::uint64_t>(input->frameIndex) ||
        frame->source_timestamp != input->timestamp)
        return false;
    auto task  = ticket->TakeTaskPermit();
    auto model = ticket->TakeModelPermit();
    if (!task || !model)
        return false;
    std::shared_ptr<NativeHelmetTicket> lease(std::move(ticket));
    const FrameKey key{data->channelId, frame->stream_generation, frame->decoder_token, frame->frame_index,
                       frame->source_timestamp};
    std::vector<BoundTarget> targets;
    std::vector<Roi> rois;
    targets.reserve(input->targets.size());
    rois.reserve(input->targets.size());
    for (const auto& target : input->targets) {
        targets.push_back({target, key});
        if (target.classId != 1 || ClassifierEligibility(target, input->bHaveArea) == Eligibility::Skip)
            continue;
        if (target.confidence.source != AiAnnotationSource::Detection ||
            target.confidence.atomic_code != "1001003" || target.confidence.label != "pedestrian")
            return false;
        auto roi = BoxToRoi(target.relatedEl.bActive ? target.relatedEl.box : target.box, 1920, 1080);
        if (!roi || rois.size() >= 64)
            return false;
        rois.push_back(*roi);
    }
    std::vector<std::array<float, 2>> scores;
    if (!rois.empty()) {
        struct IoMarker final : helmet_candidate::IoOwner {};
        struct MapMarker final : helmet_candidate::MappingOwner {};
        auto pending           = std::make_unique<helmet_candidate::PendingResources>();
        pending->frame         = frame;
        pending->frame_lease   = lease;
        pending->model         = native_helmet_model_;
        pending->handle        = native_helmet_model_->HandleForOwnerThread();
        pending->owner_thread  = std::this_thread::get_id();
        pending->task_permit   = task;
        pending->model_permit  = model;
        pending->io_owner      = std::make_shared<IoMarker>();
        pending->mapping_owner = std::make_shared<MapMarker>();
        pending->sdk           = std::make_shared<helmet_candidate::HelmetSystemSdk>(
            [counters = execution_counters_](AX_ENGINE_HANDLE h, AX_ENGINE_IO_T* io) {
                AX_S32 rc = -1;
                counters->RunForward([&]() {
                    rc = AX_ENGINE_RunSync(h, io);
                    return rc == 0;
                });
                return rc;
            });
        auto result = helmet_candidate::ExecuteFallSdkBatch(std::move(pending), rois);
        if (result.result == helmet_candidate::GuardResult::CompletionUnknown ||
            result.result == helmet_candidate::GuardResult::Poisoned)
            throw std::runtime_error("native fall device completion unknown");
        if (result.result != helmet_candidate::GuardResult::Complete || result.scores.size() != rois.size() ||
            !result.task_permit || !result.model_permit)
            return false;
        task   = std::move(result.task_permit);
        model  = std::move(result.model_permit);
        scores = std::move(result.scores);
    } else if (!task->permit.TryBegin() || !model->permit.TryBegin()) {
        return false;
    }
    const auto eligible = [&](const AiDetectRstEl& t) { return ClassifierEligibility(t, input->bHaveArea); };
    const auto current  = [&]() { return task->permit.StillCurrent() && model->permit.StillCurrent(); };
    std::size_t cursor  = 0;
    const auto fetch    = [&](const AiDetectRstEl&, const Roi& roi, std::array<float, 2>& out) {
        if (cursor >= scores.size() || roi != rois[cursor])
            return RunStatus::KnownFailure;
        out = scores[cursor++];
        return RunStatus::Complete;
    };
    // SDK work has definitively completed before staged result construction.
    const auto isolate = []() noexcept {};
    const Thresholds reference_thresholds{};
    auto batch = Stage(targets, key, 64, reference_thresholds, current, eligible, fetch, isolate);
    if (batch.status != BatchStatus::Complete || cursor != scores.size())
        return false;
    const auto next_id = []() { return util::GenerateUUID(); };
    auto prepared      = Materialize(targets, key, batch, reference_thresholds, {}, eligible, next_id);
    if (!prepared)
        return false;
    TaskOutput sink;
    if (!fall_candidate::CommitPrepared(task->permit, model->permit, sink, prepared))
        return false;
    auto output = AlgDataCopy(data);
    if (!output)
        return false;
    output->dataType      = AlgDataType::TaskDataClassify;
    output->bHaveClassify = true;
    auto classify         = std::make_shared<DataDetTrackClassify>(*input);
    classify->targets.swap(sink);
    output->SetTaskResult(AlgDataType::TaskDataClassify, std::move(classify));
    RecordHistory(output);
    distributor->DistributorData(output);
    return true;
}
#endif

#ifdef COSMO_AXERA_SLEEP_ROUTE_CANDIDATE
bool AiClassifier::HandNativeSleep(AlgDataPtr data) {
    using namespace sleep_candidate;
    if (!data || !native_helmet_model_ || !data->native_helmet_ticket ||
        data->dataType != AlgDataType::TaskDataTrack || !data->native_helmet_ticket->ReadyForClassifier())
        return false;
    auto input = data->GetTaskResult(AlgDataType::TaskDataTrack);
    if (!input)
        return false;
    auto ticket = std::move(data->native_helmet_ticket);
    auto frame  = ticket->Claim();
    if (!frame || !frame->frame_witness || !frame->frame_witness->Matches(*frame) || input->frameIndex < 0 ||
        frame->frame_index != static_cast<std::uint64_t>(input->frameIndex) ||
        frame->source_timestamp != input->timestamp)
        return false;
    auto task  = ticket->TakeTaskPermit();
    auto model = ticket->TakeModelPermit();
    if (!task || !model)
        return false;
    std::shared_ptr<NativeHelmetTicket> lease(std::move(ticket));
    const FrameKey key{data->channelId, frame->stream_generation, frame->decoder_token, frame->frame_index,
                       frame->source_timestamp};
    std::vector<BoundTarget> targets;
    std::vector<Roi> rois;
    targets.reserve(input->targets.size());
    rois.reserve(input->targets.size());
    for (const auto& target : input->targets) {
        targets.push_back({target, key});
        if (target.classId != 1 || ClassifierEligibility(target, input->bHaveArea) == Eligibility::Skip)
            continue;
        if (target.confidence.source != AiAnnotationSource::Detection ||
            target.confidence.atomic_code != "1001003" || target.confidence.label != "pedestrian")
            return false;
        auto roi = BoxToRoi(target.relatedEl.bActive ? target.relatedEl.box : target.box, 1920, 1080);
        if (!roi || rois.size() >= 64)
            return false;
        rois.push_back(*roi);
    }
    std::vector<std::array<float, 7>> scores;
    if (!rois.empty()) {
        struct IoMarker final : helmet_candidate::IoOwner {};
        struct MapMarker final : helmet_candidate::MappingOwner {};
        auto pending           = std::make_unique<helmet_candidate::PendingResources>();
        pending->frame         = frame;
        pending->frame_lease   = lease;
        pending->model         = native_helmet_model_;
        pending->handle        = native_helmet_model_->HandleForOwnerThread();
        pending->owner_thread  = std::this_thread::get_id();
        pending->task_permit   = task;
        pending->model_permit  = model;
        pending->io_owner      = std::make_shared<IoMarker>();
        pending->mapping_owner = std::make_shared<MapMarker>();
        pending->sdk           = std::make_shared<helmet_candidate::HelmetSystemSdk>(
            [counters = execution_counters_](AX_ENGINE_HANDLE h, AX_ENGINE_IO_T* io) {
                AX_S32 rc = -1;
                counters->RunForward([&]() {
                    rc = AX_ENGINE_RunSync(h, io);
                    return rc == 0;
                });
                return rc;
            });
        auto result = helmet_candidate::ExecuteSleepSdkBatch(std::move(pending), rois);
        if (result.result == helmet_candidate::GuardResult::CompletionUnknown ||
            result.result == helmet_candidate::GuardResult::Poisoned)
            throw std::runtime_error("native sleep device completion unknown");
        if (result.result != helmet_candidate::GuardResult::Complete || result.scores.size() != rois.size() ||
            !result.task_permit || !result.model_permit)
            return false;
        task   = std::move(result.task_permit);
        model  = std::move(result.model_permit);
        scores = std::move(result.scores);
    } else if (!task->permit.TryBegin() || !model->permit.TryBegin()) {
        return false;
    }
    const auto eligible = [&](const AiDetectRstEl& t) { return ClassifierEligibility(t, input->bHaveArea); };
    const auto current  = [&]() { return task->permit.StillCurrent() && model->permit.StillCurrent(); };
    std::size_t cursor  = 0;
    const auto fetch    = [&](const AiDetectRstEl&, const Roi& roi, std::array<float, 7>& out) {
        if (cursor >= scores.size() || roi != rois[cursor])
            return RunStatus::KnownFailure;
        out = scores[cursor++];
        return RunStatus::Complete;
    };
    // SDK work has definitively completed before staged result construction.
    const auto isolate = []() noexcept {};
    auto batch         = Stage(targets, key, 64, current, eligible, fetch, isolate);
    if (batch.status != BatchStatus::Complete || cursor != scores.size())
        return false;
    const auto next_id = []() { return util::GenerateUUID(); };
    auto prepared      = Materialize(targets, key, batch, eligible, next_id);
    if (!prepared)
        return false;
    TaskOutput sink;
    if (!sleep_candidate::CommitPrepared(task->permit, model->permit, sink, prepared))
        return false;
    auto output = AlgDataCopy(data);
    if (!output)
        return false;
    output->dataType      = AlgDataType::TaskDataClassify;
    output->bHaveClassify = true;
    auto classify         = std::make_shared<DataDetTrackClassify>(*input);
    classify->targets.swap(sink);
    output->SetTaskResult(AlgDataType::TaskDataClassify, std::move(classify));
    RecordHistory(output);
    distributor->DistributorData(output);
    return true;
}
#endif

#endif

#ifdef COSMO_AXERA_ELECTRIC_ROUTE_CANDIDATE
bool AiClassifier::HandNativeElectric(AlgDataPtr data) {
    using namespace electric_candidate;
    if (!data || !native_electric_enabled_ || !native_helmet_model_ || !data->native_helmet_ticket ||
        data->dataType != AlgDataType::TaskDataTrack || !data->native_helmet_ticket->ReadyForClassifier())
        return false;
    auto input = data->GetTaskResult(AlgDataType::TaskDataTrack);
    if (!input)
        return false;
    auto ticket = std::move(data->native_helmet_ticket);
    if (ticket->TaskId() != native_electric_task_ || ticket->Channel() != data->channelId)
        return false;
    const auto identity = ticket->RouteIdentity();
    auto frame          = ticket->Claim();
    if (!frame || !frame->frame_witness || !frame->frame_witness->Matches(*frame) || input->frameIndex < 0 ||
        frame->frame_index != static_cast<std::uint64_t>(input->frameIndex) ||
        frame->source_timestamp != input->timestamp)
        return false;
    auto task  = ticket->TakeTaskPermit();
    auto model = ticket->TakeModelPermit();
    if (!task || !model)
        return false;
    std::shared_ptr<NativeHelmetTicket> lease(std::move(ticket));
    const auto& hash = cosmo::nn::axera::route_identity::Digest("electric.sha256");
    if (hash.empty())
        return false;
    const auto* spec = electric_scene_candidate::Lookup(native_electric_scene_);
    if (!spec)
        return false;
    const Admission binding{native_electric_scene_,
                            "2000020",
                            "2000001",
                            spec->output_label,
                            spec->repository_scene_label_id,
                            spec->reference_config_label_id,
                            spec->tensor_channel,
                            3,
                            hash,
                            hash};
    const InferenceKey inference{native_electric_task_, identity.task_generation, identity.model_generation,
                                 identity.config_generation, hash};
    const FrameKey key{data->channelId, frame->stream_generation, frame->decoder_token, frame->frame_index,
                       frame->source_timestamp};
    std::vector<BoundTarget> targets;
    std::vector<Roi> rois;
    targets.reserve(input->targets.size());
    rois.reserve(input->targets.size());
    for (const auto& target : input->targets) {
        targets.push_back({target, key, inference});
        if ((target.classId != 2 && target.classId != 3) ||
            ClassifierEligibility(target, input->bHaveArea) == Eligibility::Skip)
            continue;
        if (target.confidence.source != AiAnnotationSource::Detection ||
            target.confidence.atomic_code != "2000001" ||
            !((target.classId == 2 && target.confidence.label == "Nonvehicle") ||
              (target.classId == 3 && target.confidence.label == "Vehicle")))
            return false;
        auto roi = BoxToRoi(target.relatedEl.bActive ? target.relatedEl.box : target.box, 1920, 1080);
        if (!roi || rois.size() >= 64)
            return false;
        rois.push_back(*roi);
    }
    std::vector<std::array<float, 3>> scores;
    if (!rois.empty()) {
        struct IoMarker final : helmet_candidate::IoOwner {};
        struct MapMarker final : helmet_candidate::MappingOwner {};
        auto pending           = std::make_unique<helmet_candidate::PendingResources>();
        pending->frame         = frame;
        pending->frame_lease   = lease;
        pending->model         = native_helmet_model_;
        pending->handle        = native_helmet_model_->HandleForOwnerThread();
        pending->owner_thread  = std::this_thread::get_id();
        pending->task_permit   = task;
        pending->model_permit  = model;
        pending->io_owner      = std::make_shared<IoMarker>();
        pending->mapping_owner = std::make_shared<MapMarker>();
        pending->sdk           = std::make_shared<helmet_candidate::HelmetSystemSdk>(
            [counters = execution_counters_](AX_ENGINE_HANDLE h, AX_ENGINE_IO_T* io) {
                AX_S32 rc = -1;
                counters->RunForward([&]() {
                    rc = AX_ENGINE_RunSync(h, io);
                    return rc == 0;
                });
                return rc;
            });
        auto result = helmet_candidate::ExecuteElectricSdkBatch(std::move(pending), rois);
        if (result.result == helmet_candidate::GuardResult::CompletionUnknown ||
            result.result == helmet_candidate::GuardResult::Poisoned)
            throw std::runtime_error("native electric device completion unknown");
        if (result.result != helmet_candidate::GuardResult::Complete || result.scores.size() != rois.size() ||
            !result.task_permit || !result.model_permit)
            return false;
        task   = std::move(result.task_permit);
        model  = std::move(result.model_permit);
        scores = std::move(result.scores);
    } else if (!task->permit.TryBegin() || !model->permit.TryBegin())
        return false;
    const auto eligible = [&](const AiDetectRstEl& t) { return ClassifierEligibility(t, input->bHaveArea); };
    const auto current  = [&]() { return task->permit.StillCurrent() && model->permit.StillCurrent(); };
    std::size_t cursor  = 0;
    const auto fetch    = [&](const AiDetectRstEl&, const Roi& roi, std::array<float, 3>& out) {
        if (cursor >= scores.size() || roi != rois[cursor])
            return RunStatus::KnownFailure;
        out = scores[cursor++];
        return RunStatus::Complete;
    };
    const auto isolate = []() noexcept {};
    const Thresholds thresholds{native_electric_scene_, native_electric_threshold_};
    auto batch = Stage(targets, key, binding, inference, 64, thresholds, current, eligible, fetch, isolate);
    if (batch.status != BatchStatus::Complete || cursor != scores.size())
        return false;
    const auto next_id = []() { return util::GenerateUUID(); };
    auto prepared      = Materialize(targets, key, batch, binding, inference, thresholds, eligible, next_id);
    if (!prepared)
        return false;
    TaskOutput sink;
    if (!electric_candidate::CommitPrepared(task->permit, model->permit, sink, prepared))
        return false;
    auto output = AlgDataCopy(data);
    if (!output)
        return false;
    output->dataType      = AlgDataType::TaskDataClassify;
    output->bHaveClassify = true;
    auto classify         = std::make_shared<DataDetTrackClassify>(*input);
    classify->targets.swap(sink);
    output->SetTaskResult(AlgDataType::TaskDataClassify, std::move(classify));
    RecordHistory(output);
    distributor->DistributorData(output);
    return true;
}
#endif
#ifdef COSMO_AXERA_VEHICLECOLOR_ROUTE_CANDIDATE
bool AiClassifier::HandNativeVehicleColor(AlgDataPtr data) {
    using namespace vehiclecolor_candidate;
    if (!data || !native_vehiclecolor_enabled_ || !native_helmet_model_ || !data->native_helmet_ticket ||
        data->dataType != AlgDataType::TaskDataTrack || !data->native_helmet_ticket->ReadyForClassifier())
        return false;
    auto input = data->GetTaskResult(AlgDataType::TaskDataTrack);
    if (!input)
        return false;
    auto ticket = std::move(data->native_helmet_ticket);
    if (ticket->TaskId() != native_vehiclecolor_task_ || ticket->Channel() != data->channelId)
        return false;
    const auto identity = ticket->RouteIdentity();
    auto frame          = ticket->Claim();
    if (!frame || !frame->frame_witness || !frame->frame_witness->Matches(*frame) || input->frameIndex < 0 ||
        frame->frame_index != static_cast<std::uint64_t>(input->frameIndex) ||
        frame->source_timestamp != input->timestamp)
        return false;
    auto task  = ticket->TakeTaskPermit();
    auto model = ticket->TakeModelPermit();
    if (!task || !model)
        return false;
    std::shared_ptr<NativeHelmetTicket> lease(std::move(ticket));
    const auto& hash = cosmo::nn::axera::route_identity::Digest("vehicle_color.sha256");
    if (hash.empty())
        return false;
    const auto* spec = vehiclecolor_scene_candidate::Lookup(native_vehiclecolor_scene_);
    if (!spec)
        return false;
    const Admission binding{native_vehiclecolor_scene_,
                            "2000011",
                            "2000001",
                            spec->output_label,
                            spec->repository_scene_label_id,
                            spec->reference_config_label_id,
                            spec->tensor_channel,
                            27,
                            hash,
                            hash};
    const InferenceKey inference{native_vehiclecolor_task_, identity.task_generation,
                                 identity.model_generation, identity.config_generation, hash};
    const FrameKey key{data->channelId, frame->stream_generation, frame->decoder_token, frame->frame_index,
                       frame->source_timestamp};
    std::vector<BoundTarget> targets;
    std::vector<Roi> rois;
    targets.reserve(input->targets.size());
    rois.reserve(input->targets.size());
    for (const auto& target : input->targets) {
        targets.push_back({target, key, inference});
        if ((target.classId != 3) || ClassifierEligibility(target, input->bHaveArea) == Eligibility::Skip)
            continue;
        if (target.confidence.source != AiAnnotationSource::Detection ||
            target.confidence.atomic_code != "2000001" ||
            !(target.classId == 3 && target.confidence.label == "Vehicle"))
            return false;
        auto roi = BoxToRoi(target.relatedEl.bActive ? target.relatedEl.box : target.box, 1920, 1080);
        if (!roi || rois.size() >= 64)
            return false;
        rois.push_back(*roi);
    }
    std::vector<std::array<float, 27>> scores;
    if (!rois.empty()) {
        struct IoMarker final : helmet_candidate::IoOwner {};
        struct MapMarker final : helmet_candidate::MappingOwner {};
        auto pending           = std::make_unique<helmet_candidate::PendingResources>();
        pending->frame         = frame;
        pending->frame_lease   = lease;
        pending->model         = native_helmet_model_;
        pending->handle        = native_helmet_model_->HandleForOwnerThread();
        pending->owner_thread  = std::this_thread::get_id();
        pending->task_permit   = task;
        pending->model_permit  = model;
        pending->io_owner      = std::make_shared<IoMarker>();
        pending->mapping_owner = std::make_shared<MapMarker>();
        pending->sdk           = std::make_shared<helmet_candidate::HelmetSystemSdk>(
            [counters = execution_counters_](AX_ENGINE_HANDLE h, AX_ENGINE_IO_T* io) {
                AX_S32 rc = -1;
                counters->RunForward([&]() {
                    rc = AX_ENGINE_RunSync(h, io);
                    return rc == 0;
                });
                return rc;
            });
        auto result = helmet_candidate::ExecuteVehicleColorSdkBatch(std::move(pending), rois);
        if (result.result == helmet_candidate::GuardResult::CompletionUnknown ||
            result.result == helmet_candidate::GuardResult::Poisoned)
            throw std::runtime_error("native electric device completion unknown");
        if (result.result != helmet_candidate::GuardResult::Complete || result.scores.size() != rois.size() ||
            !result.task_permit || !result.model_permit)
            return false;
        task   = std::move(result.task_permit);
        model  = std::move(result.model_permit);
        scores = std::move(result.scores);
    } else if (!task->permit.TryBegin() || !model->permit.TryBegin())
        return false;
    const auto eligible = [&](const AiDetectRstEl& t) { return ClassifierEligibility(t, input->bHaveArea); };
    const auto current  = [&]() { return task->permit.StillCurrent() && model->permit.StillCurrent(); };
    std::size_t cursor  = 0;
    const auto fetch    = [&](const AiDetectRstEl&, const Roi& roi, std::array<float, 27>& out) {
        if (cursor >= scores.size() || roi != rois[cursor])
            return RunStatus::KnownFailure;
        out = scores[cursor++];
        return RunStatus::Complete;
    };
    const auto isolate = []() noexcept {};
    const Thresholds thresholds{native_vehiclecolor_scene_, native_vehiclecolor_threshold_};
    auto batch = Stage(targets, key, binding, inference, 64, thresholds, current, eligible, fetch, isolate);
    if (batch.status != BatchStatus::Complete || cursor != scores.size())
        return false;
    const auto next_id = []() { return util::GenerateUUID(); };
    auto prepared      = Materialize(targets, key, batch, binding, inference, thresholds, eligible, next_id);
    if (!prepared)
        return false;
    TaskOutput sink;
    if (!vehiclecolor_candidate::CommitPrepared(task->permit, model->permit, sink, prepared))
        return false;
    auto output = AlgDataCopy(data);
    if (!output)
        return false;
    output->dataType      = AlgDataType::TaskDataClassify;
    output->bHaveClassify = true;
    auto classify         = std::make_shared<DataDetTrackClassify>(*input);
    classify->targets.swap(sink);
    output->SetTaskResult(AlgDataType::TaskDataClassify, std::move(classify));
    RecordHistory(output);
    distributor->DistributorData(output);
    return true;
}
#endif
#ifdef COSMO_AXERA_REID_ROUTE_CANDIDATE
bool AiClassifier::HandNativeReid(AlgDataPtr data) {
    if (!data || !native_reid_enabled_ || !native_reid_models_ || !data->native_helmet_ticket ||
        data->dataType != AlgDataType::TaskDataTrack || !data->native_helmet_ticket->ReadyForClassifier())
        return false;
    auto input = data->GetTaskResult(AlgDataType::TaskDataTrack);
    if (!input)
        return false;
    auto ticket = std::move(data->native_helmet_ticket);
    if (ticket->TaskId() != native_reid_task_ || ticket->Channel() != data->channelId)
        return false;
    auto frame = ticket->Claim();
    if (!frame || !frame->frame_witness || !frame->frame_witness->Matches(*frame) || input->frameIndex < 0 ||
        frame->frame_index != static_cast<std::uint64_t>(input->frameIndex) ||
        frame->source_timestamp != input->timestamp)
        return false;
    auto task  = ticket->TakeTaskPermit();
    auto model = ticket->TakeModelPermit();
    if (!task || !model)
        return false;
    std::shared_ptr<NativeHelmetTicket> lease(std::move(ticket));
    const auto selected = reid_feature_task::Select(input->targets, input->bHaveArea, frame->frame_index);
    if (!selected)
        return false;
    if (selected->rois.empty()) {
        if (native_reid_quality_bytes_)
            native_reid_ranking_.Reset();
        return false;
    }
    struct MapMarker final : helmet_candidate::MappingOwner {};
    auto pending           = std::make_unique<helmet_candidate::PendingResources>();
    pending->frame         = frame;
    pending->frame_lease   = lease;
    pending->owner_thread  = std::this_thread::get_id();
    pending->task_permit   = task;
    pending->model_permit  = model;
    pending->mapping_owner = std::make_shared<MapMarker>();
    pending->model         = native_reid_models_;
    pending->handle        = native_reid_models_->HandleForOwnerThread();
    pending->sdk           = std::make_shared<helmet_candidate::HelmetSystemSdk>(
        [counters = execution_counters_](AX_ENGINE_HANDLE h, AX_ENGINE_IO_T* io) {
            AX_S32 rc = -1;
            counters->RunForward([&]() {
                rc = AX_ENGINE_RunSync(h, io);
                return rc == 0;
            });
            return rc;
        });
    auto result = helmet_candidate::ExecuteReidBatch(std::move(pending), selected->rois);
    if (result.result == helmet_candidate::GuardResult::CompletionUnknown ||
        result.result == helmet_candidate::GuardResult::Poisoned)
        throw std::runtime_error("native ReID device completion unknown");
    if (result.result != helmet_candidate::GuardResult::Complete || !result.task_permit ||
        !result.model_permit)
        return false;
    auto prepared = reid_feature_task::Materialize(input->targets, *selected, result);
    std::vector<AiDetectRstEl> sink;
    auto ranking = native_reid_ranking_;
    if (native_reid_quality_bytes_) {
        if (!prepared || result.quality.size() != selected->indices.size() || !frame->frame_witness)
            return false;
        std::vector<pedquality_candidate::Candidate> candidates;
        candidates.reserve(selected->indices.size());
        for (std::size_t i = 0; i < selected->indices.size(); ++i) {
            auto idx = selected->indices[i];
            if (idx >= prepared->size() || (*prepared)[idx].trackId < 0)
                return false;
            auto& target = (*prepared)[idx];
            candidates.push_back({std::to_string(target.trackId), frame->frame_index, result.quality[i]});
        }
        auto improved = ranking.Select(frame->frame_witness->Generation(), frame->frame_index, candidates);
        if (!improved)
            return false;
        for (std::size_t i = 0; i < selected->indices.size(); ++i) {
            auto& target = (*prepared)[selected->indices[i]];
            target.classifyRst.push_back(
                {"quality0", "1001008", result.quality[i][0], AiAnnotationSource::Classification, {}});
            target.classifyRst.push_back(
                {"quality4", "1001008", result.quality[i][4], AiAnnotationSource::Classification, {}});
            if (std::getenv("COSMO_PEDQUALITY_DIAGNOSTIC"))
                LOG_INFO("PedQuality frame={} track={} quality0={} quality4={} selected={}",
                         frame->frame_index, target.trackId, result.quality[i][0], result.quality[i][4],
                         (*improved)[i]);
            if (!(*improved)[i])
                target.feature.feature.clear();
        }
    } else if (!result.quality.empty())
        return false;
    if (!mask_candidate::CommitPrepared(result.task_permit->permit, result.model_permit->permit, sink,
                                        prepared))
        return false;
    auto output = AlgDataCopy(data);
    if (!output)
        return false;
    if (native_reid_quality_bytes_)
        native_reid_ranking_ = std::move(ranking);
    output->dataType      = AlgDataType::TaskDataClassify;
    output->bHaveClassify = true;
    auto features         = std::make_shared<DataDetTrackClassify>(*input);
    features->targets.swap(sink);
    output->native_body_feature_proof = std::unique_ptr<NativeBodyFeatureProof>(
        new NativeBodyFeatureProof(native_reid_task_, data->channelId, features));
    output->SetTaskResult(AlgDataType::TaskDataClassify, std::move(features));
    distributor->DistributorData(output);
    return true;
}
#endif
#ifdef COSMO_AXERA_FACE_FEATURE_ROUTE_CANDIDATE
bool AiClassifier::HandNativeFaceFeature(AlgDataPtr data) {
    using namespace face_feature_task;
    if (!data || !native_face_feature_enabled_ || !native_face_feature_models_ ||
        !data->native_helmet_ticket || data->dataType != AlgDataType::TaskDataTrack ||
        !data->native_helmet_ticket->ReadyForClassifier())
        return false;
    auto input = data->GetTaskResult(AlgDataType::TaskDataTrack);
    if (!input)
        return false;
    auto ticket = std::move(data->native_helmet_ticket);
    if (ticket->TaskId() != native_face_feature_task_ || ticket->Channel() != data->channelId)
        return false;
    auto frame = ticket->Claim();
    if (!frame || !frame->frame_witness || !frame->frame_witness->Matches(*frame) || input->frameIndex < 0 ||
        frame->frame_index != static_cast<std::uint64_t>(input->frameIndex) ||
        frame->source_timestamp != input->timestamp)
        return false;
    auto task  = ticket->TakeTaskPermit();
    auto model = ticket->TakeModelPermit();
    if (!task || !model)
        return false;
    std::shared_ptr<NativeHelmetTicket> lease(std::move(ticket));
    const auto selected = Select(input->targets, input->bHaveArea, frame->frame_index);
    if (!selected || selected->rois.empty())
        return false;
    struct MapMarker final : helmet_candidate::MappingOwner {};
    auto pending           = std::make_unique<helmet_candidate::PendingResources>();
    pending->frame         = frame;
    pending->frame_lease   = lease;
    pending->owner_thread  = std::this_thread::get_id();
    pending->task_permit   = task;
    pending->model_permit  = model;
    pending->mapping_owner = std::make_shared<MapMarker>();
    pending->sdk           = std::make_shared<helmet_candidate::HelmetSystemSdk>(
        [counters = execution_counters_](AX_ENGINE_HANDLE h, AX_ENGINE_IO_T* io) {
            AX_S32 rc = -1;
            counters->RunForward([&]() {
                rc = AX_ENGINE_RunSync(h, io);
                return rc == 0;
            });
            return rc;
        });
    auto result =
        helmet_candidate::ExecuteFaceTask(std::move(pending), native_face_feature_models_, selected->rois);
    if (result.result == helmet_candidate::GuardResult::CompletionUnknown ||
        result.result == helmet_candidate::GuardResult::Poisoned)
        throw std::runtime_error("native face feature device completion unknown");
    if (result.result != helmet_candidate::GuardResult::Complete || !result.task_permit ||
        !result.model_permit)
        return false;
    auto prepared = Materialize(input->targets, *selected, result);
    std::vector<AiDetectRstEl> sink;
    if (!mask_candidate::CommitPrepared(result.task_permit->permit, result.model_permit->permit, sink,
                                        prepared))
        return false;
    auto output = AlgDataCopy(data);
    if (!output)
        return false;
    output->dataType      = AlgDataType::TaskDataLandmark;
    output->bHaveLandmark = true;
    auto landmarks        = std::make_shared<DataDetTrackClassify>(*input);
    landmarks->targets.swap(sink);
    output->native_face_feature_proof = std::unique_ptr<NativeFaceFeatureProof>(
        new NativeFaceFeatureProof(native_face_feature_task_, data->channelId, landmarks));
    output->SetTaskResult(AlgDataType::TaskDataLandmark, std::move(landmarks));
    distributor->DistributorData(output);
    return true;
}
#endif
#ifdef COSMO_AXERA_FACEQUALITY_ROUTE_CANDIDATE
bool AiClassifier::HandNativeFaceQuality(AlgDataPtr data) {
    using namespace facequality_candidate;
    if (!data || !native_facequality_enabled_ || !native_helmet_model_ || !data->native_helmet_ticket ||
        data->dataType != AlgDataType::TaskDataTrack || !data->native_helmet_ticket->ReadyForClassifier())
        return false;
    auto input = data->GetTaskResult(AlgDataType::TaskDataTrack);
    if (!input)
        return false;
    auto ticket = std::move(data->native_helmet_ticket);
    if (ticket->TaskId() != native_facequality_task_ || ticket->Channel() != data->channelId)
        return false;
    const auto identity = ticket->RouteIdentity();
    auto frame          = ticket->Claim();
    if (!frame || !frame->frame_witness || !frame->frame_witness->Matches(*frame) || input->frameIndex < 0 ||
        frame->frame_index != static_cast<std::uint64_t>(input->frameIndex) ||
        frame->source_timestamp != input->timestamp)
        return false;
    auto task  = ticket->TakeTaskPermit();
    auto model = ticket->TakeModelPermit();
    if (!task || !model)
        return false;
    std::shared_ptr<NativeHelmetTicket> lease(std::move(ticket));
    const auto& hash = cosmo::nn::axera::route_identity::Digest("face_quality.sha256");
    if (hash.empty())
        return false;
    const auto* spec = facequality_scene_candidate::Lookup(native_facequality_scene_);
    if (!spec)
        return false;
    const Admission binding{native_facequality_scene_,
                            "1000012",
                            "1000001",
                            spec->output_label,
                            spec->repository_scene_label_id,
                            spec->reference_config_label_id,
                            spec->tensor_channel,
                            6,
                            hash,
                            hash};
    const InferenceKey inference{native_facequality_task_, identity.task_generation,
                                 identity.model_generation, identity.config_generation, hash};
    const FrameKey key{data->channelId, frame->stream_generation, frame->decoder_token, frame->frame_index,
                       frame->source_timestamp};
    std::vector<BoundTarget> targets;
    std::vector<Roi> rois;
    targets.reserve(input->targets.size());
    rois.reserve(input->targets.size());
    for (const auto& target : input->targets) {
        targets.push_back({target, key, inference});
        if (target.classId != 1 || ClassifierEligibility(target, input->bHaveArea) == Eligibility::Skip)
            continue;
        if (target.confidence.source != AiAnnotationSource::Detection ||
            target.confidence.atomic_code != "1000001" || target.confidence.label != "face")
            return false;
        auto roi = BoxToRoi(target.relatedEl.bActive ? target.relatedEl.box : target.box, 1920, 1080);
        if (!roi || rois.size() >= 64)
            return false;
        rois.push_back(*roi);
    }
    std::vector<std::array<float, 6>> scores;
    if (!rois.empty()) {
        struct IoMarker final : helmet_candidate::IoOwner {};
        struct MapMarker final : helmet_candidate::MappingOwner {};
        auto pending           = std::make_unique<helmet_candidate::PendingResources>();
        pending->frame         = frame;
        pending->frame_lease   = lease;
        pending->model         = native_helmet_model_;
        pending->handle        = native_helmet_model_->HandleForOwnerThread();
        pending->owner_thread  = std::this_thread::get_id();
        pending->task_permit   = task;
        pending->model_permit  = model;
        pending->io_owner      = std::make_shared<IoMarker>();
        pending->mapping_owner = std::make_shared<MapMarker>();
        pending->sdk           = std::make_shared<helmet_candidate::HelmetSystemSdk>(
            [counters = execution_counters_](AX_ENGINE_HANDLE h, AX_ENGINE_IO_T* io) {
                AX_S32 rc = -1;
                counters->RunForward([&]() {
                    rc = AX_ENGINE_RunSync(h, io);
                    return rc == 0;
                });
                return rc;
            });
        auto result = helmet_candidate::ExecuteFaceQualitySdkBatch(std::move(pending), rois);
        if (result.result == helmet_candidate::GuardResult::CompletionUnknown ||
            result.result == helmet_candidate::GuardResult::Poisoned)
            throw std::runtime_error("native facequality device completion unknown");
        if (result.result != helmet_candidate::GuardResult::Complete || result.scores.size() != rois.size() ||
            !result.task_permit || !result.model_permit)
            return false;
        task   = std::move(result.task_permit);
        model  = std::move(result.model_permit);
        scores = std::move(result.scores);
    } else if (!task->permit.TryBegin() || !model->permit.TryBegin())
        return false;
    const auto eligible = [&](const AiDetectRstEl& t) { return ClassifierEligibility(t, input->bHaveArea); };
    const auto current  = [&]() { return task->permit.StillCurrent() && model->permit.StillCurrent(); };
    std::size_t cursor  = 0;
    const auto fetch    = [&](const AiDetectRstEl&, const Roi& roi, std::array<float, 6>& out) {
        if (cursor >= scores.size() || roi != rois[cursor])
            return RunStatus::KnownFailure;
        out = scores[cursor++];
        return RunStatus::Complete;
    };
    const auto isolate = []() noexcept {};
    const Thresholds thresholds{native_facequality_scene_, native_facequality_threshold_};
    auto batch = Stage(targets, key, binding, inference, 64, thresholds, current, eligible, fetch, isolate);
    if (batch.status != BatchStatus::Complete || cursor != scores.size())
        return false;
    const auto next_id = []() { return util::GenerateUUID(); };
    auto prepared      = Materialize(targets, key, batch, binding, inference, thresholds, eligible, next_id);
    if (!prepared)
        return false;
    TaskOutput sink;
    if (!facequality_candidate::CommitPrepared(task->permit, model->permit, sink, prepared))
        return false;
    auto output = AlgDataCopy(data);
    if (!output)
        return false;
    output->dataType      = AlgDataType::TaskDataClassify;
    output->bHaveClassify = true;
    auto classify         = std::make_shared<DataDetTrackClassify>(*input);
    classify->targets.swap(sink);
    output->SetTaskResult(AlgDataType::TaskDataClassify, std::move(classify));
    RecordHistory(output);
    distributor->DistributorData(output);
    return true;
}
#endif
#ifdef COSMO_AXERA_MASK_ROUTE_CANDIDATE
bool AiClassifier::HandNativeMask(AlgDataPtr data) {
    using namespace mask_candidate;
    if (!data || !native_mask_enabled_ || !native_helmet_model_ || !data->native_helmet_ticket ||
        data->dataType != AlgDataType::TaskDataTrack || !data->native_helmet_ticket->ReadyForClassifier())
        return false;
    auto input = data->GetTaskResult(AlgDataType::TaskDataTrack);
    if (!input)
        return false;
    auto ticket = std::move(data->native_helmet_ticket);
    if (ticket->TaskId() != native_mask_task_ || ticket->Channel() != data->channelId)
        return false;
    const auto identity = ticket->RouteIdentity();
    auto frame          = ticket->Claim();
    if (!frame || !frame->frame_witness || !frame->frame_witness->Matches(*frame) || input->frameIndex < 0 ||
        frame->frame_index != static_cast<std::uint64_t>(input->frameIndex) ||
        frame->source_timestamp != input->timestamp)
        return false;
    auto task  = ticket->TakeTaskPermit();
    auto model = ticket->TakeModelPermit();
    if (!task || !model)
        return false;
    std::shared_ptr<NativeHelmetTicket> lease(std::move(ticket));
    const auto& hash = cosmo::nn::axera::route_identity::Digest("mask.sha256");
    if (hash.empty())
        return false;
    const auto* spec = mask_scene_candidate::Lookup(native_mask_scene_);
    if (!spec)
        return false;
    const Admission binding{native_mask_scene_,
                            "1000010",
                            "1000001",
                            spec->output_label,
                            spec->repository_scene_label_id,
                            spec->reference_config_label_id,
                            spec->tensor_channel,
                            2,
                            hash,
                            hash};
    const InferenceKey inference{native_mask_task_, identity.task_generation, identity.model_generation,
                                 identity.config_generation, hash};
    const FrameKey key{data->channelId, frame->stream_generation, frame->decoder_token, frame->frame_index,
                       frame->source_timestamp};
    std::vector<BoundTarget> targets;
    std::vector<Roi> rois;
    targets.reserve(input->targets.size());
    rois.reserve(input->targets.size());
    for (const auto& target : input->targets) {
        targets.push_back({target, key, inference});
        if (target.classId != 1 || ClassifierEligibility(target, input->bHaveArea) == Eligibility::Skip)
            continue;
        if (target.confidence.source != AiAnnotationSource::Detection ||
            target.confidence.atomic_code != "1000001" || target.confidence.label != "face")
            return false;
        auto roi = BoxToRoi(target.relatedEl.bActive ? target.relatedEl.box : target.box, 1920, 1080);
        if (!roi || rois.size() >= 64)
            return false;
        rois.push_back(*roi);
    }
    std::vector<std::array<float, 2>> scores;
    if (!rois.empty()) {
        struct IoMarker final : helmet_candidate::IoOwner {};
        struct MapMarker final : helmet_candidate::MappingOwner {};
        auto pending           = std::make_unique<helmet_candidate::PendingResources>();
        pending->frame         = frame;
        pending->frame_lease   = lease;
        pending->model         = native_helmet_model_;
        pending->handle        = native_helmet_model_->HandleForOwnerThread();
        pending->owner_thread  = std::this_thread::get_id();
        pending->task_permit   = task;
        pending->model_permit  = model;
        pending->io_owner      = std::make_shared<IoMarker>();
        pending->mapping_owner = std::make_shared<MapMarker>();
        pending->sdk           = std::make_shared<helmet_candidate::HelmetSystemSdk>(
            [counters = execution_counters_](AX_ENGINE_HANDLE h, AX_ENGINE_IO_T* io) {
                AX_S32 rc = -1;
                counters->RunForward([&]() {
                    rc = AX_ENGINE_RunSync(h, io);
                    return rc == 0;
                });
                return rc;
            });
        auto result = helmet_candidate::ExecuteMaskSdkBatch(std::move(pending), rois);
        if (result.result == helmet_candidate::GuardResult::CompletionUnknown ||
            result.result == helmet_candidate::GuardResult::Poisoned)
            throw std::runtime_error("native mask device completion unknown");
        if (result.result != helmet_candidate::GuardResult::Complete || result.scores.size() != rois.size() ||
            !result.task_permit || !result.model_permit)
            return false;
        task   = std::move(result.task_permit);
        model  = std::move(result.model_permit);
        scores = std::move(result.scores);
    } else if (!task->permit.TryBegin() || !model->permit.TryBegin())
        return false;
    const auto eligible = [&](const AiDetectRstEl& t) { return ClassifierEligibility(t, input->bHaveArea); };
    const auto current  = [&]() { return task->permit.StillCurrent() && model->permit.StillCurrent(); };
    std::size_t cursor  = 0;
    const auto fetch    = [&](const AiDetectRstEl&, const Roi& roi, std::array<float, 2>& out) {
        if (cursor >= scores.size() || roi != rois[cursor])
            return RunStatus::KnownFailure;
        out = scores[cursor++];
        return RunStatus::Complete;
    };
    const auto isolate = []() noexcept {};
    const Thresholds thresholds{native_mask_scene_, native_mask_threshold_};
    auto batch = Stage(targets, key, binding, inference, 64, thresholds, current, eligible, fetch, isolate);
    if (batch.status != BatchStatus::Complete || cursor != scores.size())
        return false;
    const auto next_id = []() { return util::GenerateUUID(); };
    auto prepared      = Materialize(targets, key, batch, binding, inference, thresholds, eligible, next_id);
    if (!prepared)
        return false;
    TaskOutput sink;
    if (!mask_candidate::CommitPrepared(task->permit, model->permit, sink, prepared))
        return false;
    auto output = AlgDataCopy(data);
    if (!output)
        return false;
    output->dataType      = AlgDataType::TaskDataClassify;
    output->bHaveClassify = true;
    auto classify         = std::make_shared<DataDetTrackClassify>(*input);
    classify->targets.swap(sink);
    output->SetTaskResult(AlgDataType::TaskDataClassify, std::move(classify));
    RecordHistory(output);
    distributor->DistributorData(output);
    return true;
}
#endif
#ifdef COSMO_AXERA_HSHELMET_ROUTE_CANDIDATE
bool AiClassifier::HandNativeHsHelmet(AlgDataPtr data) {
    using namespace hshelmet_candidate;
    if (!data || !native_hshelmet_enabled_ || !native_helmet_model_ || !data->native_helmet_ticket ||
        data->dataType != AlgDataType::TaskDataTrack || !data->native_helmet_ticket->ReadyForClassifier())
        return false;
    auto input = data->GetTaskResult(AlgDataType::TaskDataTrack);
    if (!input)
        return false;
    auto ticket = std::move(data->native_helmet_ticket);
    if (ticket->TaskId() != native_hshelmet_task_ || ticket->Channel() != data->channelId)
        return false;
    const auto identity = ticket->RouteIdentity();
    auto frame          = ticket->Claim();
    if (!frame || !frame->frame_witness || !frame->frame_witness->Matches(*frame) || input->frameIndex < 0 ||
        frame->frame_index != static_cast<std::uint64_t>(input->frameIndex) ||
        frame->source_timestamp != input->timestamp)
        return false;
    auto task  = ticket->TakeTaskPermit();
    auto model = ticket->TakeModelPermit();
    if (!task || !model)
        return false;
    std::shared_ptr<NativeHelmetTicket> lease(std::move(ticket));
    const auto& hash = cosmo::nn::axera::route_identity::Digest("head_shoulder_helmet.sha256");
    if (hash.empty())
        return false;
    const auto* spec = hshelmet_scene_candidate::Lookup(native_hshelmet_scene_);
    if (!spec)
        return false;
    const Admission binding{native_hshelmet_scene_,
                            "2001002",
                            "1001001",
                            spec->output_label,
                            spec->repository_scene_label_id,
                            spec->reference_config_label_id,
                            spec->tensor_channel,
                            5,
                            hash,
                            hash};
    const InferenceKey inference{native_hshelmet_task_, identity.task_generation, identity.model_generation,
                                 identity.config_generation, hash};
    const FrameKey key{data->channelId, frame->stream_generation, frame->decoder_token, frame->frame_index,
                       frame->source_timestamp};
    std::vector<BoundTarget> targets;
    std::vector<Roi> rois;
    targets.reserve(input->targets.size());
    rois.reserve(input->targets.size());
    for (const auto& target : input->targets) {
        targets.push_back({target, key, inference});
        if (target.classId != 1 || ClassifierEligibility(target, input->bHaveArea) == Eligibility::Skip)
            continue;
        if (target.confidence.source != AiAnnotationSource::Detection ||
            target.confidence.atomic_code != "1001001" || target.confidence.label != "headShoulder")
            return false;
        auto roi = BoxToRoi(target.relatedEl.bActive ? target.relatedEl.box : target.box, 1920, 1080);
        if (!roi || rois.size() >= 64)
            return false;
        rois.push_back(*roi);
    }
    std::vector<std::array<float, 5>> scores;
    if (!rois.empty()) {
        struct IoMarker final : helmet_candidate::IoOwner {};
        struct MapMarker final : helmet_candidate::MappingOwner {};
        auto pending           = std::make_unique<helmet_candidate::PendingResources>();
        pending->frame         = frame;
        pending->frame_lease   = lease;
        pending->model         = native_helmet_model_;
        pending->handle        = native_helmet_model_->HandleForOwnerThread();
        pending->owner_thread  = std::this_thread::get_id();
        pending->task_permit   = task;
        pending->model_permit  = model;
        pending->io_owner      = std::make_shared<IoMarker>();
        pending->mapping_owner = std::make_shared<MapMarker>();
        pending->sdk           = std::make_shared<helmet_candidate::HelmetSystemSdk>(
            [counters = execution_counters_](AX_ENGINE_HANDLE h, AX_ENGINE_IO_T* io) {
                AX_S32 rc = -1;
                counters->RunForward([&]() {
                    rc = AX_ENGINE_RunSync(h, io);
                    return rc == 0;
                });
                return rc;
            });
        auto result = helmet_candidate::ExecuteHsHelmetSdkBatch(std::move(pending), rois);
        if (result.result == helmet_candidate::GuardResult::CompletionUnknown ||
            result.result == helmet_candidate::GuardResult::Poisoned)
            throw std::runtime_error("native mask device completion unknown");
        if (result.result != helmet_candidate::GuardResult::Complete || result.scores.size() != rois.size() ||
            !result.task_permit || !result.model_permit)
            return false;
        task   = std::move(result.task_permit);
        model  = std::move(result.model_permit);
        scores = std::move(result.scores);
    } else if (!task->permit.TryBegin() || !model->permit.TryBegin())
        return false;
    const auto eligible = [&](const AiDetectRstEl& t) { return ClassifierEligibility(t, input->bHaveArea); };
    const auto current  = [&]() { return task->permit.StillCurrent() && model->permit.StillCurrent(); };
    std::size_t cursor  = 0;
    const auto fetch    = [&](const AiDetectRstEl&, const Roi& roi, std::array<float, 5>& out) {
        if (cursor >= scores.size() || roi != rois[cursor])
            return RunStatus::KnownFailure;
        out = scores[cursor++];
        return RunStatus::Complete;
    };
    const auto isolate = []() noexcept {};
    const Thresholds thresholds{native_hshelmet_scene_, native_hshelmet_threshold_};
    auto batch = Stage(targets, key, binding, inference, 64, thresholds, current, eligible, fetch, isolate);
    if (batch.status != BatchStatus::Complete || cursor != scores.size())
        return false;
    const auto next_id = []() { return util::GenerateUUID(); };
    auto prepared      = Materialize(targets, key, batch, binding, inference, thresholds, eligible, next_id);
    if (!prepared)
        return false;
    TaskOutput sink;
    if (!hshelmet_candidate::CommitPrepared(task->permit, model->permit, sink, prepared))
        return false;
    auto output = AlgDataCopy(data);
    if (!output)
        return false;
    output->dataType      = AlgDataType::TaskDataClassify;
    output->bHaveClassify = true;
    auto classify         = std::make_shared<DataDetTrackClassify>(*input);
    classify->targets.swap(sink);
    output->SetTaskResult(AlgDataType::TaskDataClassify, std::move(classify));
    RecordHistory(output);
    distributor->DistributorData(output);
    return true;
}
#endif
#ifdef COSMO_AXERA_PHONE_ROUTE_CANDIDATE
bool AiClassifier::HandNativePhone(AlgDataPtr data) {
    using namespace phone_candidate;
    if (!data || !native_phone_enabled_ || !native_helmet_model_ || !data->native_helmet_ticket ||
        data->dataType != AlgDataType::TaskDataTrack || !data->native_helmet_ticket->ReadyForClassifier())
        return false;
    auto input = data->GetTaskResult(AlgDataType::TaskDataTrack);
    if (!input)
        return false;
    auto ticket = std::move(data->native_helmet_ticket);
    if (ticket->TaskId() != native_phone_task_ || ticket->Channel() != data->channelId)
        return false;
    const auto identity = ticket->RouteIdentity();
    auto frame          = ticket->Claim();
    if (!frame || !frame->frame_witness || !frame->frame_witness->Matches(*frame) || input->frameIndex < 0 ||
        frame->frame_index != static_cast<std::uint64_t>(input->frameIndex) ||
        frame->source_timestamp != input->timestamp)
        return false;
    auto task  = ticket->TakeTaskPermit();
    auto model = ticket->TakeModelPermit();
    if (!task || !model)
        return false;
    std::shared_ptr<NativeHelmetTicket> lease(std::move(ticket));
    const auto& hash = cosmo::nn::axera::route_identity::Digest("garment.sha256");
    if (hash.empty())
        return false;
    const auto* spec = phone_scene_candidate::Lookup(native_phone_scene_);
    if (!spec)
        return false;
    const Admission binding{native_phone_scene_,
                            "2002011",
                            "1001003",
                            spec->output_label,
                            spec->repository_scene_label_id,
                            spec->reference_config_label_id,
                            spec->tensor_channel,
                            7,
                            hash,
                            hash};
    const InferenceKey inference{native_phone_task_, identity.task_generation, identity.model_generation,
                                 identity.config_generation, hash};
    const FrameKey key{data->channelId, frame->stream_generation, frame->decoder_token, frame->frame_index,
                       frame->source_timestamp};
    std::vector<BoundTarget> targets;
    std::vector<Roi> rois;
    targets.reserve(input->targets.size());
    rois.reserve(input->targets.size());
    for (const auto& target : input->targets) {
        targets.push_back({target, key, inference});
        if (target.classId != 1 || ClassifierEligibility(target, input->bHaveArea) == Eligibility::Skip)
            continue;
        if (target.confidence.source != AiAnnotationSource::Detection ||
            target.confidence.atomic_code != "1001003" || target.confidence.label != "pedestrian")
            return false;
        auto roi = BoxToRoi(target.relatedEl.bActive ? target.relatedEl.box : target.box, 1920, 1080);
        if (!roi || rois.size() >= 64)
            return false;
        rois.push_back(*roi);
    }
    std::vector<std::array<float, 7>> scores;
    if (!rois.empty()) {
        struct IoMarker final : helmet_candidate::IoOwner {};
        struct MapMarker final : helmet_candidate::MappingOwner {};
        auto pending           = std::make_unique<helmet_candidate::PendingResources>();
        pending->frame         = frame;
        pending->frame_lease   = lease;
        pending->model         = native_helmet_model_;
        pending->handle        = native_helmet_model_->HandleForOwnerThread();
        pending->owner_thread  = std::this_thread::get_id();
        pending->task_permit   = task;
        pending->model_permit  = model;
        pending->io_owner      = std::make_shared<IoMarker>();
        pending->mapping_owner = std::make_shared<MapMarker>();
        pending->sdk           = std::make_shared<helmet_candidate::HelmetSystemSdk>(
            [counters = execution_counters_](AX_ENGINE_HANDLE h, AX_ENGINE_IO_T* io) {
                AX_S32 rc = -1;
                counters->RunForward([&]() {
                    rc = AX_ENGINE_RunSync(h, io);
                    return rc == 0;
                });
                return rc;
            });
        auto result = helmet_candidate::ExecutePhoneSdkBatch(std::move(pending), rois);
        if (result.result == helmet_candidate::GuardResult::CompletionUnknown ||
            result.result == helmet_candidate::GuardResult::Poisoned)
            throw std::runtime_error("native phone device completion unknown");
        if (result.result != helmet_candidate::GuardResult::Complete || result.scores.size() != rois.size() ||
            !result.task_permit || !result.model_permit)
            return false;
        task   = std::move(result.task_permit);
        model  = std::move(result.model_permit);
        scores = std::move(result.scores);
    } else if (!task->permit.TryBegin() || !model->permit.TryBegin())
        return false;
    const auto eligible = [&](const AiDetectRstEl& t) { return ClassifierEligibility(t, input->bHaveArea); };
    const auto current  = [&]() { return task->permit.StillCurrent() && model->permit.StillCurrent(); };
    std::size_t cursor  = 0;
    const auto fetch    = [&](const AiDetectRstEl&, const Roi& roi, std::array<float, 7>& out) {
        if (cursor >= scores.size() || roi != rois[cursor])
            return RunStatus::KnownFailure;
        out = scores[cursor++];
        return RunStatus::Complete;
    };
    const auto isolate = []() noexcept {};
    const Thresholds thresholds{native_phone_scene_, native_phone_threshold_};
    auto batch = Stage(targets, key, binding, inference, 64, thresholds, current, eligible, fetch, isolate);
    if (batch.status != BatchStatus::Complete || cursor != scores.size())
        return false;
    const auto next_id = []() { return util::GenerateUUID(); };
    auto prepared      = Materialize(targets, key, batch, binding, inference, thresholds, eligible, next_id);
    if (!prepared)
        return false;
    TaskOutput sink;
    if (!phone_candidate::CommitPrepared(task->permit, model->permit, sink, prepared))
        return false;
    auto output = AlgDataCopy(data);
    if (!output)
        return false;
    output->dataType      = AlgDataType::TaskDataClassify;
    output->bHaveClassify = true;
    auto classify         = std::make_shared<DataDetTrackClassify>(*input);
    classify->targets.swap(sink);
    output->SetTaskResult(AlgDataType::TaskDataClassify, std::move(classify));
    RecordHistory(output);
    distributor->DistributorData(output);
    return true;
}
#endif
#ifdef COSMO_AXERA_GARMENT_ROUTE_CANDIDATE
bool AiClassifier::HandNativeGarment(AlgDataPtr data) {
    using namespace garment_candidate;
    if (!data || !native_garment_enabled_ || !native_helmet_model_ || !data->native_helmet_ticket ||
        data->dataType != AlgDataType::TaskDataTrack || !data->native_helmet_ticket->ReadyForClassifier())
        return false;
    auto input = data->GetTaskResult(AlgDataType::TaskDataTrack);
    if (!input)
        return false;
    auto ticket = std::move(data->native_helmet_ticket);
    if (ticket->TaskId() != native_garment_task_ || ticket->Channel() != data->channelId)
        return false;
    const auto identity = ticket->RouteIdentity();
    auto frame          = ticket->Claim();
    if (!frame || !frame->frame_witness || !frame->frame_witness->Matches(*frame) || input->frameIndex < 0 ||
        frame->frame_index != static_cast<std::uint64_t>(input->frameIndex) ||
        frame->source_timestamp != input->timestamp)
        return false;
    auto task  = ticket->TakeTaskPermit();
    auto model = ticket->TakeModelPermit();
    if (!task || !model)
        return false;
    std::shared_ptr<NativeHelmetTicket> lease(std::move(ticket));
    const auto& hash = cosmo::nn::axera::route_identity::Digest("phone.sha256");
    if (hash.empty())
        return false;
    const auto* spec = garment_scene_candidate::Lookup(native_garment_scene_);
    if (!spec)
        return false;
    const Admission binding{native_garment_scene_,
                            "1001005",
                            "1001003",
                            spec->output_label,
                            spec->repository_scene_label_id,
                            spec->reference_config_label_id,
                            spec->tensor_channel,
                            66,
                            hash,
                            hash};
    const InferenceKey inference{native_garment_task_, identity.task_generation, identity.model_generation,
                                 identity.config_generation, hash};
    const FrameKey key{data->channelId, frame->stream_generation, frame->decoder_token, frame->frame_index,
                       frame->source_timestamp};
    std::vector<BoundTarget> targets;
    std::vector<Roi> rois;
    targets.reserve(input->targets.size());
    rois.reserve(input->targets.size());
    for (const auto& target : input->targets) {
        targets.push_back({target, key, inference});
        if (target.classId != 1 || ClassifierEligibility(target, input->bHaveArea) == Eligibility::Skip)
            continue;
        if (target.confidence.source != AiAnnotationSource::Detection ||
            target.confidence.atomic_code != "1001003" || target.confidence.label != "pedestrian")
            return false;
        auto roi = BoxToRoi(target.relatedEl.bActive ? target.relatedEl.box : target.box, 1920, 1080);
        if (!roi || rois.size() >= 64)
            return false;
        rois.push_back(*roi);
    }
    std::vector<std::array<float, 66>> scores;
    if (!rois.empty()) {
        struct IoMarker final : helmet_candidate::IoOwner {};
        struct MapMarker final : helmet_candidate::MappingOwner {};
        auto pending           = std::make_unique<helmet_candidate::PendingResources>();
        pending->frame         = frame;
        pending->frame_lease   = lease;
        pending->model         = native_helmet_model_;
        pending->handle        = native_helmet_model_->HandleForOwnerThread();
        pending->owner_thread  = std::this_thread::get_id();
        pending->task_permit   = task;
        pending->model_permit  = model;
        pending->io_owner      = std::make_shared<IoMarker>();
        pending->mapping_owner = std::make_shared<MapMarker>();
        pending->sdk           = std::make_shared<helmet_candidate::HelmetSystemSdk>(
            [counters = execution_counters_](AX_ENGINE_HANDLE h, AX_ENGINE_IO_T* io) {
                AX_S32 rc = -1;
                counters->RunForward([&]() {
                    rc = AX_ENGINE_RunSync(h, io);
                    return rc == 0;
                });
                return rc;
            });
        auto result = helmet_candidate::ExecuteGarmentSdkBatch(std::move(pending), rois);
        if (result.result == helmet_candidate::GuardResult::CompletionUnknown ||
            result.result == helmet_candidate::GuardResult::Poisoned)
            throw std::runtime_error("native phone device completion unknown");
        if (result.result != helmet_candidate::GuardResult::Complete || result.scores.size() != rois.size() ||
            !result.task_permit || !result.model_permit)
            return false;
        task   = std::move(result.task_permit);
        model  = std::move(result.model_permit);
        scores = std::move(result.scores);
    } else if (!task->permit.TryBegin() || !model->permit.TryBegin())
        return false;
    const auto eligible = [&](const AiDetectRstEl& t) { return ClassifierEligibility(t, input->bHaveArea); };
    const auto current  = [&]() { return task->permit.StillCurrent() && model->permit.StillCurrent(); };
    std::size_t cursor  = 0;
    const auto fetch    = [&](const AiDetectRstEl&, const Roi& roi, std::array<float, 66>& out) {
        if (cursor >= scores.size() || roi != rois[cursor])
            return RunStatus::KnownFailure;
        out = scores[cursor++];
        return RunStatus::Complete;
    };
    const auto isolate = []() noexcept {};
    const Thresholds thresholds{native_garment_scene_, native_garment_threshold_};
    auto batch = Stage(targets, key, binding, inference, 64, thresholds, current, eligible, fetch, isolate);
    if (batch.status != BatchStatus::Complete || cursor != scores.size())
        return false;
    const auto next_id = []() { return util::GenerateUUID(); };
    auto prepared      = Materialize(targets, key, batch, binding, inference, thresholds, eligible, next_id);
    if (!prepared)
        return false;
    TaskOutput sink;
    if (!garment_candidate::CommitPrepared(task->permit, model->permit, sink, prepared))
        return false;
    auto output = AlgDataCopy(data);
    if (!output)
        return false;
    output->dataType      = AlgDataType::TaskDataClassify;
    output->bHaveClassify = true;
    auto classify         = std::make_shared<DataDetTrackClassify>(*input);
    classify->targets.swap(sink);
    output->SetTaskResult(AlgDataType::TaskDataClassify, std::move(classify));
    RecordHistory(output);
    distributor->DistributorData(output);
    return true;
}
#endif

void AiClassifier::HandFramesEx(std::vector<AlgDataPtr> alg_datas) {
#ifdef COSMO_AXERA_HELMET_ROUTE_CANDIDATE
    if (native_helmet_enabled_) {
        bool handled = false;
        if (alg_datas.size() == 1 && alg_datas[0] && alg_datas[0]->native_helmet_ticket) {
#ifdef COSMO_AXERA_SCENARIOBENCH_CANDIDATE
            if (native_bench_helmet_enabled_)
                handled = HandBenchmarkHelmet(std::move(alg_datas[0]));
            else
#endif
#ifdef COSMO_AXERA_VEHICLECOLOR_ROUTE_CANDIDATE
                if (native_vehiclecolor_enabled_)
                handled = HandNativeVehicleColor(std::move(alg_datas[0]));
            else
#endif
#ifdef COSMO_AXERA_ELECTRIC_ROUTE_CANDIDATE
                if (native_electric_enabled_)
                handled = HandNativeElectric(std::move(alg_datas[0]));
            else
#endif
#ifdef COSMO_AXERA_HSHELMET_ROUTE_CANDIDATE
                if (native_hshelmet_enabled_)
                handled = HandNativeHsHelmet(std::move(alg_datas[0]));
            else
#endif
#ifdef COSMO_AXERA_REID_ROUTE_CANDIDATE
                if (native_reid_enabled_)
                handled = HandNativeReid(std::move(alg_datas[0]));
            else
#endif
#ifdef COSMO_AXERA_FACE_FEATURE_ROUTE_CANDIDATE
                if (native_face_feature_enabled_)
                handled = HandNativeFaceFeature(std::move(alg_datas[0]));
            else
#endif
#ifdef COSMO_AXERA_FACEQUALITY_ROUTE_CANDIDATE
                if (native_facequality_enabled_)
                handled = HandNativeFaceQuality(std::move(alg_datas[0]));
            else
#endif
#ifdef COSMO_AXERA_MASK_ROUTE_CANDIDATE
                if (native_mask_enabled_)
                handled = HandNativeMask(std::move(alg_datas[0]));
            else
#endif
#ifdef COSMO_AXERA_GARMENT_ROUTE_CANDIDATE
                if (native_garment_enabled_)
                handled = HandNativeGarment(std::move(alg_datas[0]));
            else
#endif
#ifdef COSMO_AXERA_PHONE_ROUTE_CANDIDATE
                if (native_phone_enabled_)
                handled = HandNativePhone(std::move(alg_datas[0]));
            else
#endif
#ifdef COSMO_AXERA_SLEEP_ROUTE_CANDIDATE
                if (IsNativeSleep())
                handled = HandNativeSleep(std::move(alg_datas[0]));
            else
#endif
#ifdef COSMO_AXERA_FALL_ROUTE_CANDIDATE
                if (native_fall_enabled_)
                handled = HandNativeFall(std::move(alg_datas[0]));
            else
#endif
                handled = HandNativeHelmet(std::move(alg_datas[0]));
        }
        if (!handled)
            action_status = util::ErrorEnum::AI_FORWARD_FAILED;
        else
            action_status = util::ErrorEnum::Success;
        return;
    }
    for (const auto& data : alg_datas)
        if (data && data->native_helmet_ticket) {
            action_status = util::ErrorEnum::AI_FORWARD_FAILED;
            return;
        }
#endif
    std::vector<AlgDataPtr> alg_out_datas;
    std::vector<VideoFramePtr> in_images;
    std::vector<media::NativeVideoBufferPtr> in_native_buffers;
    std::vector<std::vector<AiDetectRstEl>> io_puts;
    std::vector<int> img_record;
    size_t image_num              = alg_datas.size();
    bool target_have_mult_related = false;
    for (size_t i = 0; i < image_num; i++) {
        auto alg_data               = alg_datas[i];
        AlgDataPtr alg_out_data     = AlgDataCopy(alg_data);
        alg_out_data->dataType      = AlgDataType::TaskDataClassify;
        alg_out_data->bHaveClassify = true;
        auto classify_result        = std::make_shared<DataDetTrackClassify>();
        alg_out_data->SetTaskResult(AlgDataType::TaskDataClassify, classify_result);
        if (alg_data->chanDataDetect.detRet) {
            classify_result->frameIndex        = alg_data->chanDataDetect.detRet->frameIndex;
            classify_result->streamIndex       = alg_data->chanDataDetect.detRet->streamIndex;
            classify_result->timestamp         = alg_data->chanDataDetect.detRet->timestamp;
            classify_result->picWidth          = alg_data->chanDataDetect.detRet->picWidth;
            classify_result->picHeight         = alg_data->chanDataDetect.detRet->picHeight;
            classify_result->bHaveArea         = alg_data->chanDataDetect.detRet->bHaveArea;
            classify_result->bHaveShieldedArea = alg_data->chanDataDetect.detRet->bHaveShieldedArea;
        } else if (alg_data->chanDataDec.frame) {
            classify_result->frameIndex  = alg_data->chanDataDec.frame->GetFrameIndex();
            classify_result->streamIndex = alg_data->chanDataDec.frame->GetStreamIndex();
            classify_result->timestamp   = alg_data->chanDataDec.frame->GetTimestamp();
            classify_result->picWidth    = alg_data->chanDataDec.frame->GetWidth();
            classify_result->picHeight   = alg_data->chanDataDec.frame->GetHeight();
        }

        auto input = alg_data->GetTaskResult(AlgDataType::TaskDataTrack);
        if (AlgDataType::ChannelDataDetect == alg_data->dataType) {
            input = alg_data->chanDataDetect.detRet;
        } else if (AlgDataType::TaskDataClassify == alg_data->dataType) {
            input = alg_data->GetTaskResult(AlgDataType::TaskDataClassify);
        } else if (AlgDataType::TaskDataLandmark == alg_data->dataType) {
            input = alg_data->GetTaskResult(AlgDataType::TaskDataLandmark);
        }

        if (input) {
            classify_result->bHaveArea         = input->bHaveArea;
            classify_result->bHaveShieldedArea = input->bHaveShieldedArea;
        }
        alg_out_datas.push_back(alg_out_data);

        bool has_upstream_target_source = FlowHasUpstreamTargetSource(action_alg, action_node.flowActionId);
        if (!input && has_upstream_target_source) {
            continue;
        }

        std::vector<AiDetectRstEl> io_puts_el;
        std::vector<AiDetectRstEl> targets = input ? input->targets : std::vector<AiDetectRstEl>{};
        if (targets.empty() && !has_upstream_target_source) {
            std::vector<MsgTaskArea> areas;
            {
                std::shared_lock<std::shared_mutex> lock(mtx);
                areas = task_area_.areas;
            }
            targets = BuildAreaFallbackTargets(areas, classify_result->picWidth, classify_result->picHeight);
            classify_result->bHaveArea = !targets.empty();
        }
        size_t box_nums          = targets.size();
        target_have_mult_related = input ? input->targetHaveMultRelated : false;
        for (size_t j = 0; j < box_nums; j++) {
            if ((!targets[j].bForceClassify) &&
                ((targets[j].bFilter)                                   // Skip filtered targets
                 || (input && input->bHaveArea                          // When areas are defined
                     && ((!targets[j].areaSign.shielded_areas.empty())  // Skip targets in shielded areas
                         || (targets[j].areaSign.areas.empty())))))     // Skip targets outside all areas
            {
                classify_result->targets.push_back(targets[j]);
                continue;
            }
            io_puts_el.push_back(targets[j]);
        }

        if (io_puts_el.size() < 1) {
            continue;
        }

        io_puts.push_back(io_puts_el);
        in_images.push_back(alg_data->chanDataDec.frame);
        in_native_buffers.push_back(alg_data->chanDataDec.native_buffer);
        img_record.push_back(i);
    }

    action_status =
        classifier_->ClassifyMultSub(in_images, io_puts, target_have_mult_related, true, in_native_buffers);
    if (action_status != util::ErrorEnum::Success) {
        LOG_WARN("{}[{}] Classification failed; dropping this batch", log_tag_, GetTaskId());
        return;
    }
    for (size_t out = 0; out < io_puts.size(); out++) {
        auto classify_result = alg_out_datas[img_record[out]]->GetTaskResult(AlgDataType::TaskDataClassify);
        if (classify_result) {
            for (const auto& el : io_puts[out]) {
                classify_result->targets.push_back(el);
            }
        }
    }

    for (size_t i = 0; i < alg_out_datas.size(); i++) {
        RecordHistory(alg_out_datas[i]);
        distributor->DistributorData(alg_out_datas[i]);
    }
}

}  // namespace cosmo
