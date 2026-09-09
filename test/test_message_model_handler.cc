// Unit tests for MessageModelHandler

// clang-format off
#include "catch_amalgamated.hpp"
#include "catch2/trompeloeil.hpp"
// clang-format on

#include "api/MessageModelHandler.h"
#include "mock/MockModelService.h"
#include "util/ErrorCode.h"

using namespace cosmo;
using namespace cosmo::test;
using trompeloeil::_;

namespace {

struct ModelHandlerMocks {
    MockModelService modelSvc;
};

MessageModelHandler MakeHandler(ModelHandlerMocks& mocks) {
    return MessageModelHandler(mocks.modelSvc);
}

}  // namespace

TEST_CASE("ModelHandler: QueryModels pagination", "[model-handler]") {
    ModelHandlerMocks mocks;
    auto handler = MakeHandler(mocks);

    REQUIRE_CALL(mocks.modelSvc, QueryModels(_, _, _, _, _, _))
        .SIDE_EFFECT(_5 = 2)
        .SIDE_EFFECT(_6.push_back(Model::MsgModel{}));

    Model::MsgPageRecv data{};
    data.pageNum  = 1;
    data.pageSize = 10;
    std::error_condition errc;
    auto ret = handler.Handle(std::move(data), errc);
    REQUIRE(!errc);
}

TEST_CASE("ModelHandler: QueryModels with name filter", "[model-handler]") {
    ModelHandlerMocks mocks;
    auto handler = MakeHandler(mocks);

    REQUIRE_CALL(mocks.modelSvc, QueryModels(_, _, _, _, _, _)).SIDE_EFFECT(_5 = 0);

    Model::MsgPageRecv data{};
    data.pageNum   = 1;
    data.pageSize  = 10;
    data.modelName = "nonexistent";
    std::error_condition errc;
    auto ret = handler.Handle(std::move(data), errc);
    REQUIRE(!errc);
}

TEST_CASE("ModelHandler: ListModels", "[model-handler]") {
    ModelHandlerMocks mocks;
    auto handler = MakeHandler(mocks);

    REQUIRE_CALL(mocks.modelSvc, QueryAtomicModels(trompeloeil::_, trompeloeil::_, trompeloeil::_))
        .RETURN(std::vector<cosmo::Model::MsgAtomicModel>{});

    Model::MsgListRecv data{};
    std::error_condition errc;
    auto ret = handler.Handle(std::move(data), errc);
    REQUIRE(!errc);
}

TEST_CASE("ModelHandler: DeleteModel", "[model-handler]") {
    ModelHandlerMocks mocks;
    auto handler = MakeHandler(mocks);

    REQUIRE_CALL(mocks.modelSvc, DeleteModel("model-1")).RETURN(cosmo::util::ErrorEnum::Success);

    Model::MsgDeleteRecv data{};
    data.modelCode = "model-1";
    std::error_condition errc;
    auto ret = handler.Handle(std::move(data), errc);
    REQUIRE(!errc);
}

TEST_CASE("ModelHandler: GetModelConfig", "[model-handler]") {
    ModelHandlerMocks mocks;
    auto handler = MakeHandler(mocks);

    REQUIRE_CALL(mocks.modelSvc, GetModelConfig("model-1", _, _, _))
        .SIDE_EFFECT(_2 = "{\"key\":\"value\"}")
        .SIDE_EFFECT(_3 = true)
        .RETURN(cosmo::util::ErrorEnum::Success);

    Model::MsgGetConfigRecv data{};
    data.modelCode = "model-1";
    std::error_condition errc;
    auto ret = handler.Handle(std::move(data), errc);
    REQUIRE(!errc);
}

TEST_CASE("ModelHandler: SaveModelConfig", "[model-handler]") {
    ModelHandlerMocks mocks;
    auto handler = MakeHandler(mocks);

    REQUIRE_CALL(mocks.modelSvc, SaveModelConfig("model-1", _)).RETURN(cosmo::util::ErrorEnum::Success);

    Model::MsgSaveConfigRecv data{};
    data.modelCode  = "model-1";
    data.configJson = "{\"key\":\"new_value\"}";
    std::error_condition errc;
    auto ret = handler.Handle(std::move(data), errc);
    REQUIRE(!errc);
}

TEST_CASE("ModelHandler: GetModelComponents", "[model-handler]") {
    ModelHandlerMocks mocks;
    auto handler = MakeHandler(mocks);

    REQUIRE_CALL(mocks.modelSvc, GetModelComponents()).RETURN(std::vector<Model::MsgModelComponent>{});

    Model::MsgGetModelComponentsRecv data{};
    std::error_condition errc;
    auto ret = handler.Handle(std::move(data), errc);
    REQUIRE(!errc);
}
