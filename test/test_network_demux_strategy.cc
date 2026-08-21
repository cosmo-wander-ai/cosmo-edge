#include "catch_amalgamated.hpp"
#include "media/NetworkDemuxStrategy.h"

TEST_CASE("NetworkDemuxStrategy models RTMP as a packetized live source", "[media][rtmp]") {
    cosmo::media::NetworkDemuxStrategy strategy;

    CHECK(strategy.IsLive());
    CHECK_FALSE(strategy.SupportsRepeat());
    CHECK_FALSE(strategy.NeedsFpsControl());
    CHECK(strategy.ShouldUpdateFpsFromPts());
    CHECK(strategy.NeedsBsf(cosmo::media::VideoCodecType::kH264));
    CHECK(strategy.NeedsBsf(cosmo::media::VideoCodecType::kH265));
    CHECK_FALSE(strategy.NeedsBsf(cosmo::media::VideoCodecType::kMjpeg));
}
