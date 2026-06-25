#pragma once
// clang-format off
#include "catch_amalgamated.hpp"
#include "catch2/trompeloeil.hpp"
// clang-format on
#include "flow/common/AlgDataUnit.h"
#include "service/face/IBodyLibService.h"
#include "trompeloeil.hpp"
#include "util/PathUtil.h"

namespace cosmo::test {

class MockBodyLibService : public cosmo::service::IBodyLibService {
public:
    MAKE_MOCK5(BodyCompare,
               bool(const std::vector<std::string>&, const cosmo::AiFeature&,
                    cosmo::AiDetectMatchHighScoreInfo&, float, cosmo::service::CompareFeatureFunc),
               override);
    MAKE_MOCK1(InvalidateCache, void(const std::string&), override);
    MAKE_MOCK0(InvalidateAll, void(), override);
    MAKE_MOCK1(SetCacheTtlMs, void(int64_t), override);
    MAKE_MOCK1(ExtractBodyFeature, std::vector<float>(const VideoFramePtr&), override);
};

}  // namespace cosmo::test
