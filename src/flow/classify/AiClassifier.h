#ifdef COSMO_AXERA_REID_ROUTE_CANDIDATE
#include "flow/classify/NativePedQualityRanking.h"
#endif
// AI classifier action for per-target classification within the flow pipeline.

#pragma once

#include <array>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "flow/classify/AiClassifierBase.h"
#ifdef COSMO_AXERA_HSHELMET_ROUTE_CANDIDATE
#include "flow/classify/HsHelmetSceneContract.h"
#endif
#ifdef COSMO_AXERA_PHONE_ROUTE_CANDIDATE
#include "flow/classify/PhoneSceneContract.h"
#ifdef COSMO_AXERA_GARMENT_ROUTE_CANDIDATE
#include "flow/classify/GarmentSceneContract.h"
#endif
#ifdef COSMO_AXERA_ELECTRIC_ROUTE_CANDIDATE
#include "flow/classify/ElectricSceneContract.h"
#endif
#ifdef COSMO_AXERA_VEHICLECOLOR_ROUTE_CANDIDATE
#include "flow/classify/VehicleColorSceneContract.h"
#endif
#ifdef COSMO_AXERA_FACEQUALITY_ROUTE_CANDIDATE
#include "flow/classify/FaceQualitySceneContract.h"
#endif
#ifdef COSMO_AXERA_MASK_ROUTE_CANDIDATE
#include "flow/classify/MaskSceneContract.h"
#endif
#endif

namespace cosmo {
#ifdef COSMO_AXERA_HELMET_ROUTE_CANDIDATE
namespace helmet_candidate {
    class ModelOwner;
    struct FaceModelBundle;
    class ReidVerifiedBundle;
}  // namespace helmet_candidate
#endif
class AiClassifier : public AiClassifierBase {
public:
    AiClassifier(const std::string& taskId, ActionNode& action);
    ~AiClassifier() override;
#ifdef COSMO_AXERA_HELMET_ROUTE_CANDIDATE
    [[nodiscard]] bool Start() override;
    void Stop() override;
#endif

private:
#ifdef COSMO_AXERA_HELMET_ROUTE_CANDIDATE
    friend class TaskBase;
    bool ConfigureNativeHelmet(std::shared_ptr<const std::vector<std::uint8_t>> bytes);
#ifdef COSMO_AXERA_REID_ROUTE_CANDIDATE
    bool ConfigureNativeReid(std::array<std::shared_ptr<const std::vector<std::uint8_t>>, 2> bytes,
                             const std::string& task,
                             std::shared_ptr<const std::vector<std::uint8_t>> quality = {});
    bool HandNativeReid(AlgDataPtr data);
    bool native_reid_enabled_{false};
    std::string native_reid_task_;
    std::array<std::shared_ptr<const std::vector<std::uint8_t>>, 2> native_reid_bytes_;
    std::shared_ptr<helmet_candidate::ReidVerifiedBundle> native_reid_models_;
    std::shared_ptr<const std::vector<std::uint8_t>> native_reid_quality_bytes_;
    pedquality_candidate::CurrentBest native_reid_ranking_;
#endif
#ifdef COSMO_AXERA_FACE_FEATURE_ROUTE_CANDIDATE
    bool ConfigureNativeFaceFeature(std::array<std::shared_ptr<const std::vector<std::uint8_t>>, 5> bytes,
                                    const std::string& task_id);
    bool HandNativeFaceFeature(AlgDataPtr data);
    bool native_face_feature_enabled_{false};
    std::string native_face_feature_task_;
    std::array<std::shared_ptr<const std::vector<std::uint8_t>>, 5> native_face_feature_bytes_;
    std::shared_ptr<helmet_candidate::FaceModelBundle> native_face_feature_models_;
#endif

#ifdef COSMO_AXERA_FALL_ROUTE_CANDIDATE
    bool ConfigureNativeFall(std::shared_ptr<const std::vector<std::uint8_t>> bytes);
    bool HandNativeFall(AlgDataPtr data);
    bool native_fall_enabled_{false};
#endif
#ifdef COSMO_AXERA_ELECTRIC_ROUTE_CANDIDATE
    bool ConfigureNativeElectric(std::shared_ptr<const std::vector<std::uint8_t>> bytes,
                                 electric_scene_candidate::Scene scene, float threshold,
                                 const std::string& task_id);
    bool HandNativeElectric(AlgDataPtr data);
    bool native_electric_enabled_{false};
    electric_scene_candidate::Scene native_electric_scene_{};
    float native_electric_threshold_{0.f};
    std::string native_electric_task_;
#endif
#ifdef COSMO_AXERA_VEHICLECOLOR_ROUTE_CANDIDATE
    bool ConfigureNativeVehicleColor(std::shared_ptr<const std::vector<std::uint8_t>> bytes,
                                     vehiclecolor_scene_candidate::Scene scene, float threshold,
                                     const std::string& task_id);
    bool HandNativeVehicleColor(AlgDataPtr data);
    bool native_vehiclecolor_enabled_{false};
    vehiclecolor_scene_candidate::Scene native_vehiclecolor_scene_{};
    float native_vehiclecolor_threshold_{0.f};
    std::string native_vehiclecolor_task_;
#endif
#ifdef COSMO_AXERA_FACEQUALITY_ROUTE_CANDIDATE
    bool ConfigureNativeFaceQuality(std::shared_ptr<const std::vector<std::uint8_t>> bytes,
                                    facequality_scene_candidate::Scene scene, float threshold,
                                    const std::string& task_id);
    bool HandNativeFaceQuality(AlgDataPtr data);
    bool native_facequality_enabled_{false};
    facequality_scene_candidate::Scene native_facequality_scene_{};
    float native_facequality_threshold_{0.f};
    std::string native_facequality_task_;
#endif
#ifdef COSMO_AXERA_MASK_ROUTE_CANDIDATE
    bool ConfigureNativeMask(std::shared_ptr<const std::vector<std::uint8_t>> bytes,
                             mask_scene_candidate::Scene scene, float threshold, const std::string& task_id);
    bool HandNativeMask(AlgDataPtr data);
    bool native_mask_enabled_{false};
    mask_scene_candidate::Scene native_mask_scene_{};
    float native_mask_threshold_{0.f};
    std::string native_mask_task_;
#endif
#ifdef COSMO_AXERA_HSHELMET_ROUTE_CANDIDATE
    bool ConfigureNativeHsHelmet(std::shared_ptr<const std::vector<std::uint8_t>> bytes,
                                 hshelmet_scene_candidate::Scene scene, float threshold,
                                 const std::string& task_id);
    bool HandNativeHsHelmet(AlgDataPtr data);
    bool native_hshelmet_enabled_{false};
    hshelmet_scene_candidate::Scene native_hshelmet_scene_{};
    float native_hshelmet_threshold_{0.f};
    std::string native_hshelmet_task_;
#endif
#ifdef COSMO_AXERA_PHONE_ROUTE_CANDIDATE
    bool ConfigureNativePhone(std::shared_ptr<const std::vector<std::uint8_t>> bytes,
                              phone_scene_candidate::Scene scene, float threshold,
                              const std::string& task_id);
    bool HandNativePhone(AlgDataPtr data);
    bool native_phone_enabled_{false};
    phone_scene_candidate::Scene native_phone_scene_{};
    float native_phone_threshold_{0.f};
    std::string native_phone_task_;
#endif
#ifdef COSMO_AXERA_GARMENT_ROUTE_CANDIDATE
    bool ConfigureNativeGarment(std::shared_ptr<const std::vector<std::uint8_t>> bytes,
                                garment_scene_candidate::Scene scene, float threshold,
                                const std::string& task_id);
    bool HandNativeGarment(AlgDataPtr data);
    bool native_garment_enabled_{false};
    garment_scene_candidate::Scene native_garment_scene_{};
    float native_garment_threshold_{0.f};
    std::string native_garment_task_;
#endif
#ifdef COSMO_AXERA_SLEEP_ROUTE_CANDIDATE
    bool ConfigureNativeSleep(std::shared_ptr<const std::vector<std::uint8_t>> bytes);
    bool HandNativeSleep(AlgDataPtr data);
    bool IsNativeSleep() const noexcept {
        return native_helmet_enabled_ && alg_code_ == "1001010";
    }
#endif
    bool IsNativeHelmetCandidate() const noexcept override {
        return native_helmet_enabled_;
    }
    bool NativeHelmetWorkerStart() override;
    void NativeHelmetWorkerStop() noexcept override;
    bool HandNativeHelmet(AlgDataPtr data);
#ifdef COSMO_AXERA_SCENARIOBENCH_CANDIDATE
    bool ConfigureBenchmarkHelmet(std::shared_ptr<const std::vector<std::uint8_t>>, const std::string&);
    bool HandBenchmarkHelmet(AlgDataPtr data);
    bool native_bench_helmet_enabled_{false};
    std::string native_bench_helmet_task_;
#endif
    bool native_helmet_enabled_{false};
    std::mutex native_helmet_control_mutex_;
    std::mutex native_helmet_start_mutex_;
    std::condition_variable native_helmet_start_cv_;
    bool native_helmet_start_done_{false}, native_helmet_start_ok_{false};
    std::shared_ptr<const std::vector<std::uint8_t>> native_helmet_bytes_;
    std::shared_ptr<helmet_candidate::ModelOwner> native_helmet_model_;
#endif
    bool CheckDataAvailable(AlgDataPtr algData) override;
    void HandFramesEx(std::vector<AlgDataPtr> algDatas) override;
};
using AiClassifierPtr = std::shared_ptr<AiClassifier>;
}  // namespace cosmo
