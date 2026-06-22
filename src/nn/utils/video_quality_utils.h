#pragma once

#include <memory>

#include "nn/core/blob.h"
#include "nn/core/common.h"
#include "nn/core/macros.h"
#include "nn/core/status.h"

namespace cosmo::nn {

class AbstractVideoQualityAnalyser;
class PUBLIC VideoQualityUtils {
public:
    VideoQualityUtils(DeviceType);

    virtual ~VideoQualityUtils();

    // blur analysis
    size_t GetBlurAnalysisExtraMemorySize();
    void SetBlurAnalysisExtraMemory(void*);
    Status AllocBlurAnalysisExtraMemory();
    /**
     * @brief blur analysis
     * @param[in] blob input blob
     * @param[out] ret output result. if ret < 10, it means blur.
     * @return Status
     */
    Status BlurAnalysis(std::shared_ptr<Blob> blob, float* ret);

    // snow analysis
    size_t GetSnowAnalysisExtraMemorySize();
    void SetSnowAnalysisExtraMemory(void*);
    Status AllocSnowAnalysisExtraMemory();

    /**
     * @brief snow analysis
     * @param[in] blob input blob
     * @param[out] ret output result. if ret > 0.55, it means snow.
     * @return Status
     */
    Status SnowAnalysis(std::shared_ptr<Blob> blob, float* ret);

    // stripe analysis
    size_t GetStripeAnalysisExtraMemorySize();
    void SetStripeAnalysisExtraMemory(void*);
    Status AllocStripeAnalysisExtraMemory();

    /**
     * @brief stripe analysis
     * @param[in] blob input blob
     * @param[out] ret output result. if ret > 0.02, it means stripe.
     */
    Status StripeAnalysis(std::shared_ptr<Blob> blob, float* ret);

    // brightness analysis
    size_t GetBrightnessAnalysisExtraMemorySize();
    void SetBrightnessAnalysisExtraMemory(void*);
    Status AllocBrightnessAnalysisExtraMemory();

    /**
     * @brief brightness analysis
     * @param[in] blob input blob
     * @param[out] ret output result.
     *  If ret < 50 OR ret > 200, it means that brightness is too low or too high.
     * @return Status
     */
    Status BrightnessAnalysis(std::shared_ptr<Blob> blob, float* ret);

    // occlusion analysis
    size_t GetOcclusionAnalysisExtraMemorySize();
    void SetOcclusionAnalysisExtraMemory(void*);
    Status AllocOcclusionAnalysisExtraMemory();

    /**
     * @brief Occlusion analysis
     * @param[in] blob input blob
     * @param[out] ret the result of analysis. If ret > 0.4, it means that there is occlusion.
     * @return Status
     */
    Status OcclusionAnalysis(std::shared_ptr<Blob> blob, float* ret);

    // contrast analysis
    size_t GetContrastAnalysisExtraMemorySize();
    void SetContrastAnalysisExtraMemory(void*);
    Status AllocContrastAnalysisExtraMemory();

    /**
     * @brief contrast analysis
     * @param[in] blob input blob
     * @param[out] ret the result of analysis.
     *  If ret > 0.8, it means that there is contrast.
     * @return Status
     */
    Status ContrastAnalysis(std::shared_ptr<Blob> blob, float* ret);

    // deviation analysis
    size_t GetDeviationAnalysisExtraMemorySize();
    void SetDeviationAnalysisExtraMemory(void*);
    Status AllocDeviationAnalysisExtraMemory();
    /**
     * @brief deviation analysis
     * @param[in] blob input data
     * @param[out] ret the result of analysis.
     *  If ret > 2, it means that there is deviation.
     * @return Status
     */
    Status DeviationAnalysis(std::shared_ptr<Blob> blob, float* ret);

    Status IRCheck(std::shared_ptr<Blob> blob, int* lumination, int point_num = 5);

private:
    std::shared_ptr<AbstractVideoQualityAnalyser> analyser = nullptr;
};

}  // namespace cosmo::nn
