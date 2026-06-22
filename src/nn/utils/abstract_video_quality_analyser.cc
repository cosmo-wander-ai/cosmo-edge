#include "nn/utils/abstract_video_quality_analyser.h"

#include <mutex>

namespace cosmo::nn {

AbstractVideoQualityAnalyser::~AbstractVideoQualityAnalyser() {}

size_t AbstractVideoQualityAnalyser::GetBlurAnalysisExtraMemorySize() {
    return 0;
}

void AbstractVideoQualityAnalyser::SetBlurAnalysisExtraMemory(void*) {}

Status AbstractVideoQualityAnalyser::AllocBlurAnalysisExtraMemory() {
    return Status(COSMO_NN_ERR_NOT_IMPLEMENTED, "AbstractVideoQualityAnalyser::AllocBlurAnalysisExtraMemory");
}

Status AbstractVideoQualityAnalyser::BlurAnalysis(std::shared_ptr<Blob> blob, float* ret) {
    return Status(COSMO_NN_ERR_NOT_IMPLEMENTED, "AbstractVideoQualityAnalyser::BlurAnalysis");
}

size_t AbstractVideoQualityAnalyser::GetSnowAnalysisExtraMemorySize() {
    return 0;
}

void AbstractVideoQualityAnalyser::SetSnowAnalysisExtraMemory(void*) {}

Status AbstractVideoQualityAnalyser::AllocSnowAnalysisExtraMemory() {
    return Status(COSMO_NN_ERR_NOT_IMPLEMENTED, "AbstractVideoQualityAnalyser::AllocSnowAnalysisExtraMemory");
}

Status AbstractVideoQualityAnalyser::SnowAnalysis(std::shared_ptr<Blob> blob, float* ret) {
    return Status(COSMO_NN_ERR_NOT_IMPLEMENTED, "AbstractVideoQualityAnalyser::SnowAnalysis");
}

size_t AbstractVideoQualityAnalyser::GetStripeAnalysisExtraMemorySize() {
    return 0;
}

void AbstractVideoQualityAnalyser::SetStripeAnalysisExtraMemory(void*) {}

Status AbstractVideoQualityAnalyser::AllocStripeAnalysisExtraMemory() {
    return Status(COSMO_NN_ERR_NOT_IMPLEMENTED,
                  "AbstractVideoQualityAnalyser::AllocStripeAnalysisExtraMemory");
}

Status AbstractVideoQualityAnalyser::StripeAnalysis(std::shared_ptr<Blob> blob, float* ret) {
    return Status(COSMO_NN_ERR_NOT_IMPLEMENTED, "AbstractVideoQualityAnalyser::StripeAnalysis");
}

size_t AbstractVideoQualityAnalyser::GetBrightnessAnalysisExtraMemorySize() {
    return 0;
}

void AbstractVideoQualityAnalyser::SetBrightnessAnalysisExtraMemory(void*) {}

Status AbstractVideoQualityAnalyser::AllocBrightnessAnalysisExtraMemory() {
    return Status(COSMO_NN_ERR_NOT_IMPLEMENTED,
                  "AbstractVideoQualityAnalyser::AllocBrightnessAnalysisExtraMemory");
}

Status AbstractVideoQualityAnalyser::BrightnessAnalysis(std::shared_ptr<Blob> blob, float* ret) {
    return Status(COSMO_NN_ERR_NOT_IMPLEMENTED, "AbstractVideoQualityAnalyser::BrightnessAnalysis");
}

size_t AbstractVideoQualityAnalyser::GetOcclusionAnalysisExtraMemorySize() {
    return 0;
}

void AbstractVideoQualityAnalyser::SetOcclusionAnalysisExtraMemory(void*) {}

Status AbstractVideoQualityAnalyser::AllocOcclusionAnalysisExtraMemory() {
    return Status(COSMO_NN_ERR_NOT_IMPLEMENTED, "AbstractVideoQualityAnalyser::OcclusionAnalysis");
}

Status AbstractVideoQualityAnalyser::OcclusionAnalysis(std::shared_ptr<Blob> blob, float* ret) {
    return Status(COSMO_NN_ERR_NOT_IMPLEMENTED, "AbstractVideoQualityAnalyser::OcclusionAnalysis");
}

size_t AbstractVideoQualityAnalyser::GetContrastAnalysisExtraMemorySize() {
    return 0;
}

void AbstractVideoQualityAnalyser::SetContrastAnalysisExtraMemory(void*) {}

Status AbstractVideoQualityAnalyser::AllocContrastAnalysisExtraMemory() {
    return Status(COSMO_NN_ERR_NOT_IMPLEMENTED,
                  "AbstractVideoQualityAnalyser::AllocContrastAnalysisExtraMemory");
}

Status AbstractVideoQualityAnalyser::ContrastAnalysis(std::shared_ptr<Blob> blob, float* ret) {
    return Status(COSMO_NN_ERR_NOT_IMPLEMENTED, "AbstractVideoQualityAnalyser::ContrastAnalysis");
}

size_t AbstractVideoQualityAnalyser::GetDeviationAnalysisExtraMemorySize() {
    return 0;
}

void AbstractVideoQualityAnalyser::SetDeviationAnalysisExtraMemory(void*) {}

Status AbstractVideoQualityAnalyser::AllocDeviationAnalysisExtraMemory() {
    return Status(COSMO_NN_ERR_NOT_IMPLEMENTED,
                  "AbstractVideoQualityAnalyser::AllocDeviationAnalysisExtraMemory");
}

Status AbstractVideoQualityAnalyser::DeviationAnalysis(std::shared_ptr<Blob> blob, float* ret) {
    return Status(COSMO_NN_ERR_NOT_IMPLEMENTED, "AbstractVideoQualityAnalyser::DeviationAnalysis");
}

Status AbstractVideoQualityAnalyser::IRCheck(std::shared_ptr<Blob> blob, int* lumination, int point_num) {
    return Status(COSMO_NN_ERR_NOT_IMPLEMENTED, "AbstractVideoQualityAnalyser::IRCheck");
}

//////////////////////////////////////////////////////////////////
//  VideoQualityAnalyserManager
//////////////////////////////////////////////////////////////////
std::map<DeviceType, std::shared_ptr<AbstractVideoQualityAnalyserFactory>>&
VideoQualityAnalyserManager::GetGlobVideoQualityAnalyserFactoryMap() {
    static std::map<DeviceType, std::shared_ptr<AbstractVideoQualityAnalyserFactory>> g_analyser_map;
    return g_analyser_map;
}

void VideoQualityAnalyserManager::RegisterVideoQualityAnalyserFactory(
    DeviceType type, AbstractVideoQualityAnalyserFactory* factory) {
    if (factory) {
        auto& map = GetGlobVideoQualityAnalyserFactoryMap();
        map[type] = std::shared_ptr<AbstractVideoQualityAnalyserFactory>(factory);
    }
}

std::shared_ptr<AbstractVideoQualityAnalyser> VideoQualityAnalyserManager::GetAnalyser(DeviceType type) {
    auto& map = GetGlobVideoQualityAnalyserFactoryMap();
    auto iter = map.find(type);
    if (iter != map.end()) {
        return iter->second->CreateAnalyser();
    }
    return nullptr;
}

}  // namespace cosmo::nn