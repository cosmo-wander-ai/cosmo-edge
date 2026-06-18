#pragma once

#include <memory>

#include "nn/core/blob.h"
#include "nn/core/common.h"
#include "nn/core/status.h"

namespace cosmo::nn {

class AbstractVideoQualityAnalyser {
public:
    virtual ~AbstractVideoQualityAnalyser();

    // blur analysis
    virtual size_t GetBlurAnalysisExtraMemorySize();
    virtual void SetBlurAnalysisExtraMemory(void*);
    virtual Status AllocBlurAnalysisExtraMemory();

    /**
     * @brief blur analysis
     * @param[in] blob input blob
     * @param[out] ret output result. if ret < 10, it means blur.
     * @return Status
     */
    virtual Status BlurAnalysis(std::shared_ptr<Blob> blob, float* ret);

    // snow analysis
    virtual size_t GetSnowAnalysisExtraMemorySize();
    virtual void SetSnowAnalysisExtraMemory(void*);
    virtual Status AllocSnowAnalysisExtraMemory();

    /**
     * @brief snow analysis
     * @param[in] blob input blob
     * @param[out] ret output result. if ret > 0.55, it means snow.
     * @return Status
     */
    virtual Status SnowAnalysis(std::shared_ptr<Blob> blob, float* ret);

    // stripe analysis
    virtual size_t GetStripeAnalysisExtraMemorySize();
    virtual void SetStripeAnalysisExtraMemory(void*);
    virtual Status AllocStripeAnalysisExtraMemory();

    /**
     * @brief stripe analysis
     * @param[in] blob input blob
     * @param[out] ret output result. if ret > 0.02, it means stripe.
     * @return Status
     */
    virtual Status StripeAnalysis(std::shared_ptr<Blob> blob, float* ret);

    // brightness analysis
    virtual size_t GetBrightnessAnalysisExtraMemorySize();
    virtual void SetBrightnessAnalysisExtraMemory(void*);
    virtual Status AllocBrightnessAnalysisExtraMemory();

    /**
     * @brief brightness analysis
     * @param[in] blob input blob
     * @param[out] ret output result.
     *  If ret < 50 OR ret > 200, it means that brightness is too low or too high.
     * @return Status
     */
    virtual Status BrightnessAnalysis(std::shared_ptr<Blob> blob, float* ret);

    // occlusion analysis
    virtual size_t GetOcclusionAnalysisExtraMemorySize();
    virtual void SetOcclusionAnalysisExtraMemory(void*);
    virtual Status AllocOcclusionAnalysisExtraMemory();

    /**
     * @brief  Occlusion analysis
     * @param[in]  blob: input blob
     * @param[out] ret:  output result. If ret > 0.4, it means that there is occlusion.
     * @return Status
     */
    virtual Status OcclusionAnalysis(std::shared_ptr<Blob> blob, float* ret);

    // contrast analysis
    virtual size_t GetContrastAnalysisExtraMemorySize();
    virtual void SetContrastAnalysisExtraMemory(void*);
    virtual Status AllocContrastAnalysisExtraMemory();

    /**
     * @brief contrast analysis
     * @param[in] blob input blob
     * @param[out] ret the result of analysis.
     *  If ret > 0.8, it means that there is contrast.
     * @return Status
     */
    virtual Status ContrastAnalysis(std::shared_ptr<Blob> blob, float* ret);

    // deviation analysis
    virtual size_t GetDeviationAnalysisExtraMemorySize();
    virtual void SetDeviationAnalysisExtraMemory(void*);
    virtual Status AllocDeviationAnalysisExtraMemory();

    /**
     * @brief deviation analysis
     * @param[in] blob input data
     * @param[out] ret the result of analysis.
     *  If ret > 2, it means that there is deviation.
     * @return Status
     */
    virtual Status DeviationAnalysis(std::shared_ptr<Blob> blob, float* ret);

    /**
     * @brief: check whether the image is ir
     * select the point_num points in the image, and calculate the lumination of the points
     *
     * @param: blob the image
     * @param: lumination the lumination of the points
     * @param: point_num the number of points
     */
    virtual Status IRCheck(std::shared_ptr<Blob> blob, int* lumination, int point_num = 5);

protected:
    void* blur_analysis_extra_memory          = nullptr;
    bool blur_analysis_extra_memory_allocated = false;

    void* snow_analysis_extra_memory          = nullptr;
    bool snow_analysis_extra_memory_allocated = false;

    void* stripe_analysis_extra_memory          = nullptr;
    bool stripe_analysis_extra_memory_allocated = false;

    void* brightness_analysis_extra_memory          = nullptr;
    bool brightness_analysis_extra_memory_allocated = false;

    void* occlusion_analysis_extra_memory          = nullptr;
    bool occlusion_analysis_extra_memory_allocated = false;

    void* contrast_analysis_extra_memory          = nullptr;
    bool contrast_analysis_extra_memory_allocated = false;

    void* deviation_analysis_extra_memory          = nullptr;
    bool deviation_analysis_extra_memory_allocated = false;

    int fft_width  = 320;
    int fft_height = 240;
    int fft_hw     = fft_width * fft_height;

    int gray_width  = 112;
    int gray_height = 112;
    int gray_hw     = gray_width * gray_height;

    int lab_width  = 112;
    int lab_height = 112;
    int lab_hw     = lab_width * lab_height;
};

class AbstractVideoQualityAnalyserFactory {
public:
    virtual ~AbstractVideoQualityAnalyserFactory() {}

    virtual std::shared_ptr<AbstractVideoQualityAnalyser> CreateAnalyser() = 0;
};

template <typename T>
class VideoQualityAnalyserFactory : public AbstractVideoQualityAnalyserFactory {
public:
    virtual std::shared_ptr<AbstractVideoQualityAnalyser> CreateAnalyser() {
        return std::make_shared<T>();
    }
};

class VideoQualityAnalyserManager {
public:
    static std::shared_ptr<AbstractVideoQualityAnalyser> GetAnalyser(DeviceType);
    static void RegisterVideoQualityAnalyserFactory(DeviceType type,
                                                    AbstractVideoQualityAnalyserFactory* factory);

private:
    static std::map<DeviceType, std::shared_ptr<AbstractVideoQualityAnalyserFactory>>&
    GetGlobVideoQualityAnalyserFactoryMap();
};

template <typename T>
class VideoQualityAnalyserFactoryRegister {
public:
    explicit VideoQualityAnalyserFactoryRegister(DeviceType type) {
        VideoQualityAnalyserManager::RegisterVideoQualityAnalyserFactory(type, new T());
    }
};

}  // namespace cosmo::nn
