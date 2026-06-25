#include "catch_amalgamated.hpp"
/*
 * test_client_message_service_impl.cc — ClientMessageServiceImpl unit tests (DEBT-T01)
 *
 * Strategy: ClientMessageServiceImpl is a stub implementation that logs warnings
 * and returns false/noop. Tests verify this contract.
 */
#include "mock/MockServiceRegistry.h"
#include "service/network/impl/ClientMessageServiceImpl.h"

using namespace cosmo::service;

TEST_CASE("ClientMessageServiceImpl: construction does not crash", "[ClientMessageService]") {
    REQUIRE_NOTHROW([]() { ClientMessageServiceImpl sut; }());
}

TEST_CASE("ClientMessageServiceImpl: FetchAlgorithmConfig returns false (stub)", "[ClientMessageService]") {
    ClientMessageServiceImpl sut;
    cosmo::CMsgAlgorithmProcessConfigNGReq req;
    cosmo::CMsgAlgorithmProcessConfigNGRsp rsp;
    REQUIRE(sut.FetchAlgorithmConfig(req, rsp) == false);
}

TEST_CASE("ClientMessageServiceImpl: FetchAtomicCodeList returns false (stub)", "[ClientMessageService]") {
    ClientMessageServiceImpl sut;
    cosmo::CMsgGetAtomicCodeListReq req;
    cosmo::CMsgGetAtomicCodeListRsp rsp;
    REQUIRE(sut.FetchAtomicCodeList(req, rsp) == false);
}

TEST_CASE("ClientMessageServiceImpl: FetchVideoPlayUrl returns false (stub)", "[ClientMessageService]") {
    ClientMessageServiceImpl sut;
    cosmo::CMsgGetVideoPlayReq req;
    cosmo::CMsgGetVideoPlayRsp rsp;
    REQUIRE(sut.FetchVideoPlayUrl(req, rsp) == false);
}

TEST_CASE("ClientMessageServiceImpl: NodeOperatorEventPush does not crash (stub)", "[ClientMessageService]") {
    ClientMessageServiceImpl sut;
    cosmo::MsgOperateNodeRecv data;
    data.operateType = "test_operation";
    REQUIRE_NOTHROW(sut.NodeOperatorEventPush(data));
}
