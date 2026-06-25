#pragma once
// clang-format off
#include "catch_amalgamated.hpp"
#include "catch2/trompeloeil.hpp"
// clang-format on
#include "flow/common/AlgDataUnit.h"
#include "service/network/IClientMessageService.h"
#include "trompeloeil.hpp"
#include "util/PathUtil.h"

namespace cosmo::test {

class MockClientMessageService : public cosmo::service::IClientMessageService {
public:
    MAKE_MOCK2(FetchAlgorithmConfig,
               bool(cosmo::CMsgAlgorithmProcessConfigNGReq&, cosmo::CMsgAlgorithmProcessConfigNGRsp&),
               override);
    MAKE_MOCK2(FetchAtomicCodeList, bool(cosmo::CMsgGetAtomicCodeListReq&, cosmo::CMsgGetAtomicCodeListRsp&),
               override);
    MAKE_MOCK2(FetchVideoPlayUrl, bool(cosmo::CMsgGetVideoPlayReq&, cosmo::CMsgGetVideoPlayRsp&), override);
    MAKE_MOCK1(NodeOperatorEventPush, void(cosmo::MsgOperateNodeRecv&), override);
};

}  // namespace cosmo::test
