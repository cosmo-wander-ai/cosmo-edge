#include "nn/utils/video_quality_utils.h"

#include "nn/utils/abstract_video_quality_analyser.h"

namespace cosmo::nn {

VideoQualityUtils::VideoQualityUtils(DeviceType device_type) {
    analyser = VideoQualityAnalyserManager::GetAnalyser(device_type);
}

VideoQualityUtils::~VideoQualityUtils() {
    analyser.reset();
}

size_t VideoQualityUtils::GetBlurAnalysisExtraMemorySize() {
    return analyser->GetBlurAnalysisExtraMemorySize();
}

void VideoQualityUtils::SetBlurAnalysisExtraMemory(void* ptr) {
    analyser->SetBlurAnalysisExtraMemory(ptr);
}

Status VideoQualityUtils::AllocBlurAnalysisExtraMemory() {
    return analyser->AllocBlurAnalysisExtraMemory();
}

Status VideoQualityUtils::BlurAnalysis(std::shared_ptr<Blob> blob, float* ret) {
    return analyser->BlurAnalysis(blob, ret);
}

size_t VideoQualityUtils::GetSnowAnalysisExtraMemorySize() {
    return analyser->GetSnowAnalysisExtraMemorySize();
}

void VideoQualityUtils::SetSnowAnalysisExtraMemory(void* ptr) {
    analyser->SetSnowAnalysisExtraMemory(ptr);
}

Status VideoQualityUtils::AllocSnowAnalysisExtraMemory() {
    return analyser->AllocSnowAnalysisExtraMemory();
}

Status VideoQualityUtils::SnowAnalysis(std::shared_ptr<Blob> blob, float* ret) {
    return analyser->SnowAnalysis(blob, ret);
}

size_t VideoQualityUtils::GetStripeAnalysisExtraMemorySize() {
    return analyser->GetStripeAnalysisExtraMemorySize();
}

void VideoQualityUtils::SetStripeAnalysisExtraMemory(void* ptr) {
    analyser->SetStripeAnalysisExtraMemory(ptr);
}

Status VideoQualityUtils::AllocStripeAnalysisExtraMemory() {
    return analyser->AllocStripeAnalysisExtraMemory();
}

Status VideoQualityUtils::StripeAnalysis(std::shared_ptr<Blob> blob, float* ret) {
    return analyser->StripeAnalysis(blob, ret);
}

size_t VideoQualityUtils::GetBrightnessAnalysisExtraMemorySize() {
    return analyser->GetBrightnessAnalysisExtraMemorySize();
}

void VideoQualityUtils::SetBrightnessAnalysisExtraMemory(void* ptr) {
    analyser->SetBrightnessAnalysisExtraMemory(ptr);
}

Status VideoQualityUtils::AllocBrightnessAnalysisExtraMemory() {
    return analyser->AllocBrightnessAnalysisExtraMemory();
}

Status VideoQualityUtils::BrightnessAnalysis(std::shared_ptr<Blob> blob, float* ret) {
    return analyser->BrightnessAnalysis(blob, ret);
}

size_t VideoQualityUtils::GetOcclusionAnalysisExtraMemorySize() {
    return analyser->GetOcclusionAnalysisExtraMemorySize();
}

void VideoQualityUtils::SetOcclusionAnalysisExtraMemory(void* ptr) {
    analyser->SetOcclusionAnalysisExtraMemory(ptr);
}

Status VideoQualityUtils::AllocOcclusionAnalysisExtraMemory() {
    return analyser->AllocOcclusionAnalysisExtraMemory();
}

Status VideoQualityUtils::OcclusionAnalysis(std::shared_ptr<Blob> blob, float* ret) {
    return analyser->OcclusionAnalysis(blob, ret);
}

size_t VideoQualityUtils::GetContrastAnalysisExtraMemorySize() {
    return analyser->GetContrastAnalysisExtraMemorySize();
}

void VideoQualityUtils::SetContrastAnalysisExtraMemory(void* ptr) {
    analyser->SetContrastAnalysisExtraMemory(ptr);
}

Status VideoQualityUtils::AllocContrastAnalysisExtraMemory() {
    return analyser->AllocContrastAnalysisExtraMemory();
}

Status VideoQualityUtils::ContrastAnalysis(std::shared_ptr<Blob> blob, float* ret) {
    return analyser->ContrastAnalysis(blob, ret);
}

size_t VideoQualityUtils::GetDeviationAnalysisExtraMemorySize() {
    return analyser->GetDeviationAnalysisExtraMemorySize();
}

void VideoQualityUtils::SetDeviationAnalysisExtraMemory(void* ptr) {
    analyser->SetDeviationAnalysisExtraMemory(ptr);
}

Status VideoQualityUtils::AllocDeviationAnalysisExtraMemory() {
    return analyser->AllocDeviationAnalysisExtraMemory();
}

Status VideoQualityUtils::DeviationAnalysis(std::shared_ptr<Blob> blob, float* ret) {
    return analyser->DeviationAnalysis(blob, ret);
}

Status VideoQualityUtils::IRCheck(std::shared_ptr<Blob> blob, int* lumination, int point_num) {
    return analyser->IRCheck(blob, lumination, point_num);
}

}  // namespace cosmo::nn