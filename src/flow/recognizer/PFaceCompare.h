#pragma once

#include "infer/AiCommon.h"
#include "service/face/IFaceFeature.h"
#include "service/face/IFaceLibRepo.h"

namespace cosmo {
// Match already extracted faces; no inference or task ownership is performed here.
util::ErrorEnum ComparePictureFaces(std::vector<AiDetectRstEl>& targets,
                                    const std::vector<std::string>& libraries, float threshold,
                                    service::IFaceLibRepo& repo, service::IFaceFeature& matcher);
}  // namespace cosmo
