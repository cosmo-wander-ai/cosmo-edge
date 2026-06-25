#pragma once
// clang-format off
#include "catch_amalgamated.hpp"
#include "catch2/trompeloeil.hpp"
// clang-format on
#include "flow/common/AlgDataUnit.h"
#include "service/media/ILiveStreamService.h"
#include "trompeloeil.hpp"
#include "util/PathUtil.h"

namespace cosmo::test {

class MockLiveStreamService : public cosmo::service::ILiveStreamService {
public:
    MAKE_MOCK3(ViewerCreate,
               cosmo::util::ErrorEnum(const std::string&, const std::string&,
                                      cosmo::LiveStream::LiveStreamInfo&),
               override);
    MAKE_MOCK2(ViewerDelete, bool(const std::string&, const std::string&), override);
    MAKE_MOCK2(ViewerHeartBeat, cosmo::util::ErrorEnum(const std::string&, const std::string&), override);
    MAKE_MOCK1(SetViewCounts, void(int), override);
};

}  // namespace cosmo::test
