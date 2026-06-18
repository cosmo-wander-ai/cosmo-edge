// Unified inference pool service implementation — merges the
// former AiRecognizerInterfaceMng, AiLandmarkInterfaceMng,
// and AiDetectReuseInterfaceMng singletons.

#pragma once

#include <map>
#include <mutex>
#include <string>

#include "service/ai/IInferPoolService.h"

namespace cosmo::service {

class InferPoolServiceImpl : public IInferPoolService {
public:
    InferPoolServiceImpl()           = default;
    ~InferPoolServiceImpl() override = default;

    InferPoolServiceImpl(const InferPoolServiceImpl&)            = delete;
    InferPoolServiceImpl& operator=(const InferPoolServiceImpl&) = delete;

    cosmo::RecognizerPoolPtr GetRecognizerPool(const std::string& alg_code) override;

    cosmo::LandmarkPoolPtr GetLandmarkPool(const std::string& alg_code) override;

    cosmo::DetectorPoolPtr GetDetectPool(const std::string& alg_code) override;

private:
    std::mutex recognizer_mtx_;
    std::map<std::string, cosmo::RecognizerPoolPtr> recognizer_pools_;

    std::mutex landmark_mtx_;
    std::map<std::string, cosmo::LandmarkPoolPtr> landmark_pools_;

    std::mutex detect_mtx_;
    std::map<std::string, cosmo::DetectorPoolPtr> detect_pools_;
};

}  // namespace cosmo::service
