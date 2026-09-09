#include "catch_amalgamated.hpp"
#include "mock/MockModelService.h"
#include "service/algorithm/impl/AlgorithmValidator.h"
#include "service/system/impl/AppInfoServiceImpl.h"
#include "support/ScopedServiceOverride.h"
#include "util/Keys.h"

using namespace cosmo::service;

TEST_CASE("AlgorithmValidator uses only model query registration", "[model-query]") {
    cosmo::test::MockModelService mock;
    cosmo::test::ScopedServiceOverride<IModelQuery> registration(mock);
    REQUIRE_FALSE(ServiceRegistry::Instance().Has<IModelService>());
    algorithm::AlgorithmPacketInfo packet;

    SECTION("Workflow models preserve names and overall invalid state") {
        packet.processdata = std::make_shared<cosmo::ActionAlg>();
        cosmo::ActionNode action;
        cosmo::MsgDynamicKeyValue valid;
        valid.key                  = std::string(cosmo::key::ATOM_CODE);
        valid.value                = "valid";
        auto missing               = valid;
        missing.value              = "missing";
        action.configObject.params = {valid, missing};
        packet.processdata->workFlow.push_back(action);
        REQUIRE_CALL(mock, ModelValid("valid", trompeloeil::_)).SIDE_EFFECT(_2 = "Valid model").RETURN(true);
        REQUIRE_CALL(mock, ModelValid("missing", trompeloeil::_)).RETURN(false);
        detail::AlgorithmValidator::ValidateModels(packet);
        REQUIRE(packet.modelInfo.models.size() == 2);
        REQUIRE(packet.modelInfo.models[0].modelCode == "valid");
        REQUIRE(packet.modelInfo.models[0].modelName == "Valid model");
        REQUIRE(packet.modelInfo.models[0].bActive);
        REQUIRE(packet.modelInfo.models[1].modelCode == "missing");
        REQUIRE_FALSE(packet.modelInfo.models[1].bActive);
        REQUIRE_FALSE(packet.modelInfo.bActive);
    }
    SECTION("Local model names and active state are refreshed") {
        packet.modelInfo.models.push_back({"valid", "old", false});
        REQUIRE_CALL(mock, ModelValid("valid", trompeloeil::_)).SIDE_EFFECT(_2 = "New name").RETURN(true);
        detail::AlgorithmValidator::ValidateLocalModels(packet);
        REQUIRE(packet.modelInfo.bActive);
        REQUIRE(packet.modelInfo.models[0].bActive);
        REQUIRE(packet.modelInfo.models[0].modelName == "New name");
    }
    SECTION("Empty model lists are active without querying a model") {
        detail::AlgorithmValidator::ValidateModels(packet);
        REQUIRE(packet.modelInfo.bActive);
        REQUIRE(packet.modelInfo.models.empty());
        packet.modelInfo.bActive = false;
        detail::AlgorithmValidator::ValidateLocalModels(packet);
        REQUIRE(packet.modelInfo.bActive);
    }
}

TEST_CASE("AppInfoService delegates model paths through the narrow interface", "[model-path]") {
    cosmo::test::MockModelService mock;
    cosmo::test::ScopedServiceOverride<IModelPathMapping> registration(mock);
    REQUIRE_FALSE(ServiceRegistry::Instance().Has<IModelService>());
    AppInfoServiceImpl app;
    REQUIRE_CALL(mock, SetModelPathMapping("algorithm", "/models/custom"));
    app.SetModelPath("algorithm", "/models/custom");
}
