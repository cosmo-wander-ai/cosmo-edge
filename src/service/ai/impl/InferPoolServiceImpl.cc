// InferPoolServiceImpl — Unified inference pool service implementation — merges the

#include "service/ai/impl/InferPoolServiceImpl.h"

// Include concrete Unify headers — required for template instantiation of
// InstancePool<T,PTR>::CreateTask / GetInst (they call make_shared<T>).
#include "infer/AiDetectorUnify.h"
#include "infer/AiLandmarkerUnify.h"
#include "infer/AiRecognizerUnify.h"

namespace cosmo::service {

cosmo::RecognizerPoolPtr InferPoolServiceImpl::GetRecognizerPool(const std::string& alg_code) {
    std::lock_guard<std::mutex> lock(recognizer_mtx_);
    auto& pool = recognizer_pools_[alg_code];
    if (!pool) {
        pool = std::make_shared<cosmo::RecognizerPool>(alg_code);
    }
    return pool;
}

cosmo::LandmarkPoolPtr InferPoolServiceImpl::GetLandmarkPool(const std::string& alg_code) {
    std::lock_guard<std::mutex> lock(landmark_mtx_);
    auto& pool = landmark_pools_[alg_code];
    if (!pool) {
        pool = std::make_shared<cosmo::LandmarkPool>(alg_code);
    }
    return pool;
}

cosmo::DetectorPoolPtr InferPoolServiceImpl::GetDetectPool(const std::string& alg_code) {
    std::lock_guard<std::mutex> lock(detect_mtx_);
    auto& pool = detect_pools_[alg_code];
    if (!pool) {
        pool = std::make_shared<cosmo::DetectorPool>(alg_code);
    }
    return pool;
}

}  // namespace cosmo::service
