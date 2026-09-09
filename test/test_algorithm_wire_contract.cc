#include "AlgorithmPayloadContract.h"
#include "catch_amalgamated.hpp"
#include "service/algorithm/dto/AlgorithmDto.h"

using namespace cosmo;

TEST_CASE("Algorithm wire payload compatibility", "[algorithm-contract]") {
    test::CheckAlgorithmPayloads<Algorithm::MsgLayoutDetailSend::ResData, Algorithm::MsgLayoutDetailVersion,
                                 Algorithm::MsgLayoutListSend::ResData, Algorithm::MsgLayoutListItem,
                                 Algorithm::MsgAtomicActionListSend::ResData, Algorithm::MsgAtomicAction>();
}

TEST_CASE("Algorithm response envelopes retain protocol keys", "[algorithm-contract]") {
    const auto check = [](auto response) {
        response.resCode = 1;
        nlohmann::json expected{{"resCode", 1},
                                {"resMsg", nlohmann::json::array()},
                                {"resData", nlohmann::json::parse(R"({"list":[]})")}};
        CHECK(nlohmann::json(response) == expected);
        response.msgSendType = MsgSendType::ChinaMobile;
        response.resultCode  = "1";
        response.resultMsg   = "完成";
        expected             = {
            {"resultCode", "1"}, {"resultMsg", "完成"}, {"resData", nlohmann::json::parse(R"({"list":[]})")}};
        CHECK(nlohmann::json(response) == expected);
        auto decoded        = decltype(response){};
        decoded.msgSendType = MsgSendType::ChinaMobile;
        expected.get_to(decoded);
        CHECK(nlohmann::json(decoded) == expected);
    };
    check(Algorithm::MsgLayoutListSend{});
    check(Algorithm::MsgAtomicActionListSend{});
    Algorithm::MsgLayoutDetailSend detail;
    nlohmann::json expected{
        {"resCode", 0}, {"resMsg", nlohmann::json::array()}, {"resData", test::EmptyDetailContract()}};
    CHECK(nlohmann::json(detail) == expected);
    detail.msgSendType = MsgSendType::ChinaMobile;
    expected           = {{"resultCode", ""}, {"resultMsg", ""}, {"resData", test::EmptyDetailContract()}};
    CHECK(nlohmann::json(detail) == expected);
}
