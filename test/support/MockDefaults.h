#pragma once

#include <memory>
#include <vector>

#include "mock/MockAlgorithmService.h"
#include "mock/MockAppInfoService.h"
#include "mock/MockCameraService.h"
#include "mock/MockTaskService.h"

namespace cosmo::test {

using NamedExpectations = std::vector<std::unique_ptr<trompeloeil::expectation>>;

inline void AllowTaskMutationSuccess(MockTaskService& mock, NamedExpectations& expectations) {
    expectations.push_back(
        NAMED_ALLOW_CALL(mock, TaskCreate(trompeloeil::_, trompeloeil::_, trompeloeil::_, trompeloeil::_))
            .RETURN(cosmo::util::ErrorEnum::Success));
    expectations.push_back(NAMED_ALLOW_CALL(mock, TaskChannelSetUrl(trompeloeil::_, trompeloeil::_)));
    expectations.push_back(NAMED_ALLOW_CALL(mock, TaskStop(trompeloeil::_)).RETURN(true));
    expectations.push_back(
        NAMED_ALLOW_CALL(mock, TaskDelete(trompeloeil::_)).RETURN(cosmo::util::ErrorEnum::Success));
    expectations.push_back(
        NAMED_ALLOW_CALL(mock, SetTaskParam(trompeloeil::_, trompeloeil::_, trompeloeil::_)).RETURN(true));
    expectations.push_back(NAMED_ALLOW_CALL(mock, TaskIsStart(trompeloeil::_)).RETURN(false));
    expectations.push_back(NAMED_ALLOW_CALL(mock, TaskStart(trompeloeil::_, trompeloeil::_)).RETURN(true));
    expectations.push_back(NAMED_ALLOW_CALL(mock, RecordClearTaskData(trompeloeil::_)));
    expectations.push_back(NAMED_ALLOW_CALL(mock, RecordTaskAction(trompeloeil::_, trompeloeil::_)));
}

inline void AllowAlgorithmLookupDefaults(MockAlgorithmService& mock, NamedExpectations& expectations) {
    expectations.push_back(
        NAMED_ALLOW_CALL(mock, GetAlgorithm(trompeloeil::_)).RETURN(std::make_shared<cosmo::ActionAlg>()));
    expectations.push_back(NAMED_ALLOW_CALL(mock, GetMetaData(trompeloeil::_)).RETURN("{}"));
}

inline void AllowPreviewChannelDefaults(MockCameraService& mock, NamedExpectations& expectations) {
    expectations.push_back(NAMED_ALLOW_CALL(mock, AcquirePreviewChannel(trompeloeil::_))
                               .RETURN(cosmo::util::ErrorEnum::Success));
    expectations.push_back(NAMED_ALLOW_CALL(mock, ReleasePreviewChannel(trompeloeil::_)));
}

inline void AllowOverviewDisabled(MockAppInfoService& mock, NamedExpectations& expectations) {
    expectations.push_back(NAMED_ALLOW_CALL(mock, GetOverviewStructureRecord()).RETURN(false));
    expectations.push_back(NAMED_ALLOW_CALL(mock, GetOverviewStructureFile()).RETURN(false));
}

}  // namespace cosmo::test
