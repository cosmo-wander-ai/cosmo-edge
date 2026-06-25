#include "catch_amalgamated.hpp"
// Unit tests for BodyLibServiceImpl (DEBT-C06).
// Validates cache behavior, TTL, invalidation, and match logic.

#include "mock/MockPersonRecogDaoService.h"
#include "mock/MockServiceRegistry.h"
#include "service/face/impl/BodyLibServiceImpl.h"

namespace cosmo::test {

namespace {

    // Helper to create a simple compare function that returns a fixed score
    service::CompareFeatureFunc MakeFixedCompare(float score) {
        return [score](const AiFeature&, const AiFeature&) -> float { return score; };
    }

    // Helper to create a compare function that returns different scores per call
    service::CompareFeatureFunc MakeSequentialCompare(std::vector<float> scores) {
        auto idx = std::make_shared<size_t>(0);
        return [scores = std::move(scores), idx](const AiFeature&, const AiFeature&) -> float {
            if (*idx < scores.size()) {
                return scores[(*idx)++];
            }
            return -1.0f;
        };
    }

}  // namespace

TEST_CASE("BodyLibService: empty lib_ids returns false", "[body-lib]") {
    MockServiceRegistry mocks;
    service::BodyLibServiceImpl sut;

    AiFeature runtime_feature;
    runtime_feature.feature = {1.0f, 2.0f, 3.0f};
    AiDetectMatchHighScoreInfo match_info;

    bool result = sut.BodyCompare({}, runtime_feature, match_info, -1.0f, MakeFixedCompare(90.0f));
    REQUIRE_FALSE(result);
}

TEST_CASE("BodyLibService: SetCacheTtlMs updates TTL", "[body-lib]") {
    MockServiceRegistry mocks;
    service::BodyLibServiceImpl sut;

    // Should not throw
    sut.SetCacheTtlMs(10000);
}

TEST_CASE("BodyLibService: InvalidateCache removes specific entry", "[body-lib]") {
    MockServiceRegistry mocks;
    service::BodyLibServiceImpl sut;

    // Should not throw on non-existent key
    sut.InvalidateCache("non-existent-lib");
}

TEST_CASE("BodyLibService: InvalidateAll clears all entries", "[body-lib]") {
    MockServiceRegistry mocks;
    service::BodyLibServiceImpl sut;

    // Should not throw
    sut.InvalidateAll();
}

TEST_CASE("BodyLibService: BodyCompare with match above threshold returns true", "[body-lib]") {
    MockServiceRegistry mocks;
    service::BodyLibServiceImpl sut;
    sut.SetCacheTtlMs(60000);  // Long TTL to avoid re-fetch

    // Set up mock to return a person with feature data
    db::PersonRecogQueryResult personResult;
    personResult.total_count = 1;
    db::PersonRecogRecord person;
    person.id              = "person-001";
    person.picture_name    = "Worker A";
    person.person_lib.id   = "lib-001";
    person.person_lib.name = "Body Lib";
    person.feature         = {1.0f, 2.0f, 3.0f};
    personResult.person_list.push_back(person);

    db::PersonRecogLibQueryResult libResult;
    libResult.person_lib_count = 1;
    db::PersonRecogLibRecord libRecord;
    libRecord.id        = "lib-001";
    libRecord.name      = "Body Lib";
    libRecord.threshold = 70.0;
    libResult.person_lib_list.push_back(libRecord);

    ALLOW_CALL(mocks.personRecogDaoSvc, QueryPersons(trompeloeil::_)).RETURN(personResult);
    ALLOW_CALL(mocks.personRecogDaoSvc, QueryPersonLib(trompeloeil::_)).RETURN(libResult);

    AiFeature runtime_feature;
    runtime_feature.feature = {1.0f, 2.0f, 3.0f};
    AiDetectMatchHighScoreInfo match_info;

    // Compare function returns score 85 — above lib threshold 70
    bool result = sut.BodyCompare({"lib-001"}, runtime_feature, match_info, -1.0f, MakeFixedCompare(85.0f));
    REQUIRE(result);
    REQUIRE(match_info.matched);
    REQUIRE(match_info.match_degree == Catch::Approx(85.0f));
    REQUIRE(match_info.person_id == "person-001");
    REQUIRE(match_info.name == "Worker A");
    REQUIRE(match_info.group_id == "lib-001");
}

TEST_CASE("BodyLibService: BodyCompare with match below threshold returns false", "[body-lib]") {
    MockServiceRegistry mocks;
    service::BodyLibServiceImpl sut;
    sut.SetCacheTtlMs(60000);

    db::PersonRecogQueryResult personResult;
    personResult.total_count = 1;
    db::PersonRecogRecord person;
    person.id           = "person-002";
    person.picture_name = "Worker B";
    person.feature      = {1.0f, 2.0f, 3.0f};
    personResult.person_list.push_back(person);

    db::PersonRecogLibQueryResult libResult;
    libResult.person_lib_count = 1;
    db::PersonRecogLibRecord libRecord;
    libRecord.id        = "lib-002";
    libRecord.threshold = 80.0;
    libResult.person_lib_list.push_back(libRecord);

    ALLOW_CALL(mocks.personRecogDaoSvc, QueryPersons(trompeloeil::_)).RETURN(personResult);
    ALLOW_CALL(mocks.personRecogDaoSvc, QueryPersonLib(trompeloeil::_)).RETURN(libResult);

    AiFeature runtime_feature;
    runtime_feature.feature = {4.0f, 5.0f, 6.0f};
    AiDetectMatchHighScoreInfo match_info;

    // Compare returns 50 — below lib threshold 80
    bool result = sut.BodyCompare({"lib-002"}, runtime_feature, match_info, -1.0f, MakeFixedCompare(50.0f));
    REQUIRE_FALSE(result);
    REQUIRE_FALSE(match_info.matched);
}

TEST_CASE("BodyLibService: limit_score overrides lib threshold", "[body-lib]") {
    MockServiceRegistry mocks;
    service::BodyLibServiceImpl sut;
    sut.SetCacheTtlMs(60000);

    db::PersonRecogQueryResult personResult;
    personResult.total_count = 1;
    db::PersonRecogRecord person;
    person.id           = "person-003";
    person.picture_name = "Worker C";
    person.feature      = {1.0f};
    personResult.person_list.push_back(person);

    db::PersonRecogLibQueryResult libResult;
    libResult.person_lib_count = 1;
    db::PersonRecogLibRecord libRecord;
    libRecord.id        = "lib-003";
    libRecord.threshold = 50.0;  // Lib threshold is 50
    libResult.person_lib_list.push_back(libRecord);

    ALLOW_CALL(mocks.personRecogDaoSvc, QueryPersons(trompeloeil::_)).RETURN(personResult);
    ALLOW_CALL(mocks.personRecogDaoSvc, QueryPersonLib(trompeloeil::_)).RETURN(libResult);

    AiFeature runtime_feature;
    runtime_feature.feature = {2.0f};
    AiDetectMatchHighScoreInfo match_info;

    // Compare returns 60, limit_score is 70 — should NOT match despite lib threshold 50
    bool result = sut.BodyCompare({"lib-003"}, runtime_feature, match_info, 70.0f, MakeFixedCompare(60.0f));
    REQUIRE_FALSE(result);

    // Compare returns 75, limit_score is 70 — should match
    sut.InvalidateAll();  // Force cache refresh
    result = sut.BodyCompare({"lib-003"}, runtime_feature, match_info, 70.0f, MakeFixedCompare(75.0f));
    REQUIRE(result);
}

TEST_CASE("BodyLibService: cache hit avoids redundant DB queries", "[body-lib]") {
    MockServiceRegistry mocks;
    service::BodyLibServiceImpl sut;
    sut.SetCacheTtlMs(60000);  // Long TTL

    db::PersonRecogQueryResult personResult;
    personResult.total_count = 1;
    db::PersonRecogRecord person;
    person.id      = "p-cached";
    person.feature = {1.0f};
    personResult.person_list.push_back(person);

    db::PersonRecogLibQueryResult libResult;
    libResult.person_lib_count = 1;
    db::PersonRecogLibRecord libRecord;
    libRecord.id        = "lib-cached";
    libRecord.threshold = 50.0;
    libResult.person_lib_list.push_back(libRecord);

    // Expect exactly 1 call — second call should use cache
    REQUIRE_CALL(mocks.personRecogDaoSvc, QueryPersons(trompeloeil::_)).RETURN(personResult);
    REQUIRE_CALL(mocks.personRecogDaoSvc, QueryPersonLib(trompeloeil::_)).RETURN(libResult);

    AiFeature rf;
    rf.feature = {1.0f};
    AiDetectMatchHighScoreInfo mi;

    // First call — cache miss, hits DB
    sut.BodyCompare({"lib-cached"}, rf, mi, -1.0f, MakeFixedCompare(80.0f));
    // Second call — cache hit, does NOT hit DB
    sut.BodyCompare({"lib-cached"}, rf, mi, -1.0f, MakeFixedCompare(80.0f));
}

TEST_CASE("BodyLibService: InvalidateCache forces cache refresh", "[body-lib]") {
    MockServiceRegistry mocks;
    service::BodyLibServiceImpl sut;
    sut.SetCacheTtlMs(60000);

    db::PersonRecogQueryResult personResult;
    personResult.total_count = 1;
    db::PersonRecogRecord person;
    person.id      = "p-inv";
    person.feature = {1.0f};
    personResult.person_list.push_back(person);

    db::PersonRecogLibQueryResult libResult;
    libResult.person_lib_count = 1;
    db::PersonRecogLibRecord libRecord;
    libRecord.id        = "lib-inv";
    libRecord.threshold = 50.0;
    libResult.person_lib_list.push_back(libRecord);

    // Expect exactly 2 calls because we invalidate after the first call
    REQUIRE_CALL(mocks.personRecogDaoSvc, QueryPersons(trompeloeil::_)).RETURN(personResult).TIMES(2);
    REQUIRE_CALL(mocks.personRecogDaoSvc, QueryPersonLib(trompeloeil::_)).RETURN(libResult).TIMES(2);

    AiFeature rf;
    rf.feature = {1.0f};
    AiDetectMatchHighScoreInfo mi;

    sut.BodyCompare({"lib-inv"}, rf, mi, -1.0f, MakeFixedCompare(80.0f));
    sut.InvalidateCache("lib-inv");
    sut.BodyCompare({"lib-inv"}, rf, mi, -1.0f, MakeFixedCompare(80.0f));
}

}  // namespace cosmo::test
