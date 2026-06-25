#pragma once
// clang-format off
#include "catch_amalgamated.hpp"
#include "catch2/trompeloeil.hpp"
// clang-format on
#include "flow/common/AlgDataUnit.h"
#include "service/media/IVideoFrameCodec.h"
#include "trompeloeil.hpp"
#include "util/PathUtil.h"

namespace cosmo::test {

class MockVideoFrameCodec : public cosmo::service::IVideoFrameCodec {
public:
    MAKE_MOCK1(EncodeJpeg, std::vector<u_char>(const VideoFramePtr), override);
    MAKE_MOCK1(DecodeJpeg, VideoFramePtr(const std::vector<u_int8_t>&), override);
};

}  // namespace cosmo::test
