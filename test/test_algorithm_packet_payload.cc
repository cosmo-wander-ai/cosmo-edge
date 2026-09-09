#include "AlgorithmPayloadContract.h"
#include "catch_amalgamated.hpp"
#include "util/dto/AlgorithmPacketDto.h"

TEST_CASE("Neutral algorithm payload header supports JSON independently",
          "[algorithm-payload][algorithm-contract]") {
    using namespace cosmo::service::algorithm;
    cosmo::test::CheckAlgorithmPayloads<LayoutDetailResult, LayoutDetailVersion, LayoutListResult,
                                        LayoutListItem, AtomicActionListResult, AtomicAction>();
}
