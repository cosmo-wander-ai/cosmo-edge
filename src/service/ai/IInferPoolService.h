// Inference model pool service interface — provides access to
// shared NPU model interface pools for recognizer, landmark,
// and detection models.

#pragma once

#include <memory>
#include <string>

#include "infer/InferPoolTypes.h"
#include "service/detail/ServiceRegistry.h"

namespace cosmo::service {

/// Unified access to NPU inference model pools.
///
/// Replaces the former AiRecognizerInterfaceMng, AiLandmarkInterfaceMng,
/// and AiDetectReuseInterfaceMng singletons. Each pool lazily creates
/// reusable inference instances keyed by algorithm code.
class IInferPoolService {
public:
    virtual ~IInferPoolService() = default;

    /// Returns (or creates) the recognizer model pool for the given algorithm code.
    [[nodiscard]] virtual cosmo::RecognizerPoolPtr GetRecognizerPool(const std::string& alg_code) = 0;

    /// Returns (or creates) the landmark model pool for the given algorithm code.
    [[nodiscard]] virtual cosmo::LandmarkPoolPtr GetLandmarkPool(const std::string& alg_code) = 0;

    /// Returns (or creates) the detection model pool for the given algorithm code.
    [[nodiscard]] virtual cosmo::DetectorPoolPtr GetDetectPool(const std::string& alg_code) = 0;
};

}  // namespace cosmo::service
