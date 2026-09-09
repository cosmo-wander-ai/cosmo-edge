#pragma once

#include <nlohmann/json.hpp>
#include <string>

#include "catch_amalgamated.hpp"

namespace cosmo::test {

inline nlohmann::json DetailContract() {
    return nlohmann::json::parse(R"({
        "algorithmCode":"code", "algorithmName":"算法", "algorithmCategory":"2",
        "algorithmUsage":"1", "supplier":"supplier", "remark":"remark", "confVersionId":"v2",
        "algorithmMetadata":"{\"z\":2, \"a\":1}", "algorithmProcessdata":"[]", "atomicList":"[]",
        "configVersionList":[
            {"id":"v2", "name":"second", "algorithmCode":"code", "algorithmMetadata":"{}",
             "algorithmProcessdata":"", "atomicList":"", "algorithmUpdateTime":4294967301},
            {"id":"v1", "name":"first", "algorithmCode":"old", "algorithmMetadata":"",
             "algorithmProcessdata":"[]", "atomicList":"[]", "algorithmUpdateTime":12}
        ]})");
}

inline nlohmann::json EmptyDetailContract() {
    return nlohmann::json::parse(R"({"algorithmCode":"", "algorithmName":"", "algorithmCategory":"",
        "algorithmUsage":"", "supplier":"", "remark":"", "confVersionId":"", "algorithmMetadata":"",
        "algorithmProcessdata":"", "atomicList":"", "configVersionList":[]})");
}

inline nlohmann::json LayoutListContract() {
    return nlohmann::json::parse(R"({"list":[
        {"algorithmCode":"b", "algorithmName":"乙", "supplier":"s2", "algorithmUsage":"2", "description":"d2"},
        {"algorithmCode":"a", "algorithmName":"甲", "supplier":"s1", "algorithmUsage":"1", "description":"d1"}]})");
}

inline nlohmann::json ActionListContract() {
    return nlohmann::json::parse(R"({"list":[
        {"id":"b", "name":"乙", "actionName":"second", "inputParamConfig":"{\"b\": 2, \"a\":1}", "actionUsage":2, "actionType":3},
        {"id":"a", "name":"甲", "actionName":"first", "inputParamConfig":"[]", "actionUsage":1, "actionType":4}]})");
}

template <typename T>
void CheckPayloadContract(const nlohmann::json& expected) {
    auto value = expected.get<T>();
    CHECK(nlohmann::json(value) == expected);
    nlohmann::json::object().get_to(value);
    CHECK(nlohmann::json(value) == expected);
    auto nulls = expected;
    for (auto& item : nulls.items()) {
        item.value() = nullptr;
    }
    nulls.get_to(value);
    CHECK(nlohmann::json(value) == expected);
    for (const auto& item : expected.items()) {
        CAPTURE(item.key());
        auto wrong        = nlohmann::json::object();
        wrong[item.key()] = nlohmann::json::object();
        CHECK_THROWS_AS(wrong.get_to(value), nlohmann::json::type_error);
    }
}

template <typename Detail, typename Version, typename LayoutList, typename LayoutItem, typename ActionList,
          typename Action>
void CheckAlgorithmPayloads() {
    CheckPayloadContract<Detail>(DetailContract());
    CheckPayloadContract<Version>(DetailContract()["configVersionList"][0]);
    CheckPayloadContract<LayoutList>(LayoutListContract());
    CheckPayloadContract<LayoutItem>(LayoutListContract()["list"][0]);
    CheckPayloadContract<ActionList>(ActionListContract());
    CheckPayloadContract<Action>(ActionListContract()["list"][0]);
    CHECK(nlohmann::json(Detail{}) == EmptyDetailContract());
    CHECK(nlohmann::json(LayoutList{}) == nlohmann::json::parse(R"({"list":[]})"));
    CHECK(nlohmann::json(ActionList{}) == nlohmann::json::parse(R"({"list":[]})"));
    CHECK(
        nlohmann::json(Version{}) ==
        nlohmann::json::parse(
            R"({"id":"","name":"","algorithmCode":"","algorithmMetadata":"","algorithmProcessdata":"","atomicList":"","algorithmUpdateTime":0})"));
    CHECK(
        nlohmann::json(LayoutItem{}) ==
        nlohmann::json::parse(
            R"({"algorithmCode":"","algorithmName":"","supplier":"","algorithmUsage":"","description":""})"));
    CHECK(nlohmann::json(Action{}) ==
          nlohmann::json::parse(
              R"({"id":"","name":"","actionName":"","inputParamConfig":"","actionUsage":0,"actionType":0})"));
    const std::string escaped   = "中文\"quoted\"\\path\nnext";
    auto detail                 = DetailContract().get<Detail>();
    detail.algorithmMetadata    = escaped;
    detail.algorithmProcessdata = escaped;
    detail.atomicList           = escaped;
    const auto round_trip       = nlohmann::json::parse(nlohmann::json(detail).dump()).template get<Detail>();
    CHECK(round_trip.algorithmMetadata == escaped);
    CHECK(round_trip.algorithmProcessdata == escaped);
    CHECK(round_trip.atomicList == escaped);
    auto action             = ActionListContract()["list"][0].get<Action>();
    action.inputParamConfig = escaped;
    CHECK(nlohmann::json::parse(nlohmann::json(action).dump()).template get<Action>().inputParamConfig ==
          escaped);
}

}  // namespace cosmo::test
