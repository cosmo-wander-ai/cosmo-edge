#include <SQLiteCpp/SQLiteCpp.h>

#include <algorithm>
#include <limits>
#include <sstream>

#include "attribute_test_support.h"
#include "catch_amalgamated.hpp"
#include "db/TaskEventDao.h"
#include "flow/sensitivity/AttributeAccumulator.h"
#include "service/event/AlarmExport.h"
#include "util/dto/EventMsgTypes.h"

using namespace cosmo;

namespace {
AiDetectRstEl Observation(const std::string& hat, const std::string& bag) {
    AiDetectRstEl target;
    TargetAreaUnit area;
    area.area_id = "area";
    target.areaSign.areas.push_back(area);
    for (const auto& pair : {std::make_pair("hat-node", hat), std::make_pair("bag-node", bag)}) {
        auto& observation     = target.classificationObservations[pair.first];
        observation.modelCode = "shared-model";
        if (!pair.second.empty()) {
            AiConfidence value;
            value.label      = pair.second;
            value.confidence = 0.9f;
            observation.results.push_back(value);
        }
    }
    return target;
}
AttributeRecord Record(const std::string& id, bool known = true) {
    AttributeRecord record;
    record.schema    = TestAttributeSchema();
    record.schemaId  = AttributeSchemaId(record.schema);
    record.recordId  = id;
    record.trackId   = "track";
    record.firstSeen = 1000;
    record.lastSeen  = 4000;
    AttributeAccumulator accumulator;
    for (int64_t time : {1000, 2000, 3000})
        accumulator.Observe(record.schema, Observation(known ? "yes" : "", "no"), time, 200);
    record.attributes = accumulator.Finish(record.schema, 3);
    record.status     = known ? "complete" : "partial";
    return record;
}
AttributeSchema BinarySchema() {
    auto schema = TestAttributeSchema();
    schema.attributes.resize(1);
    auto& definition   = schema.attributes.front();
    definition.type    = "binary";
    definition.options = {{"yes", "yes", "Present"}, {"", "no", "Absent"}};
    return schema;
}
AiDetectRstEl BinaryObservation(float score) {
    auto target                                                              = Observation("yes", "");
    target.classificationObservations["hat-node"].results.front().confidence = score;
    return target;
}
db::TaskEventData Event(const AttributeRecord& record, const std::string& channel) {
    db::TaskEventData event;
    event.id             = record.recordId;
    event.category       = "12";
    event.algorithm_code = "scene";
    event.camera_id      = channel;
    event.timestamp      = 5000;
    event.property       = nlohmann::json{{"attributes", record}}.dump();
    return event;
}
}  // namespace

TEST_CASE("Binary attributes count below-threshold scores as negative evidence", "[attributes][binary]") {
    const auto schema = BinarySchema();
    const auto target = BinaryObservation(0.2f);
    AttributeAccumulator accumulator;
    for (int64_t time : {1000, 2000, 3000})
        accumulator.Observe(schema, target, time, 200);
    const auto value = accumulator.Finish(schema, 3).front();
    REQUIRE(value.status == "valid");
    REQUIRE(value.values == std::vector<std::string>{"no"});
    REQUIRE(value.samples == 3);
    REQUIRE(value.confidence == Catch::Approx(0.8));
    REQUIRE(ValidateAttributeSchema(schema));
}

TEST_CASE("Binary votes use all valid scores and require stable independent observations",
          "[attributes][binary]") {
    const auto schema = BinarySchema();
    AttributeAccumulator accumulator;
    accumulator.Observe(schema, BinaryObservation(0.9f), 1000, 200);
    accumulator.Observe(schema, BinaryObservation(0.9f), 2000, 200);
    accumulator.Observe(schema, BinaryObservation(0.1f), 3000, 200);
    accumulator.Observe(schema, BinaryObservation(0.1f), 4000, 200);
    REQUIRE(accumulator.Finish(schema, 3).front().status == "unknown");
    accumulator.Observe(schema, BinaryObservation(0.1f), 5000, 200);
    const auto value = accumulator.Finish(schema, 3).front();
    REQUIRE(value.values == std::vector<std::string>{"no"});
    REQUIRE(value.samples == 5);
    REQUIRE(value.confidence == Catch::Approx(0.9));
    SECTION("sampling limit applies to negative votes") {
        accumulator.Observe(schema, BinaryObservation(0.9f), 5000, 200);
        accumulator.Observe(schema, BinaryObservation(0.9f), 5100, 200);
        REQUIRE(accumulator.Finish(schema, 3).front().samples == 5);
    }
    SECTION("positive votes also use the full denominator") {
        accumulator.Observe(schema, BinaryObservation(0.9f), 6000, 200);
        REQUIRE(accumulator.Finish(schema, 3).front().status == "unknown");
        accumulator.Observe(schema, BinaryObservation(0.9f), 7000, 200);
        accumulator.Observe(schema, BinaryObservation(0.9f), 8000, 200);
        REQUIRE(accumulator.Finish(schema, 3).front().values == std::vector<std::string>{"yes"});
    }
}

TEST_CASE("Binary threshold includes equality but does not turn unavailable evidence into no",
          "[attributes][binary]") {
    const auto schema    = BinarySchema();
    auto target          = BinaryObservation(0.5f);
    std::string expected = "valid";
    SECTION("score exactly at threshold is positive") {}
    SECTION("missing observation") {
        target.classificationObservations.clear();
        expected = "unknown";
    }
    SECTION("empty result") {
        target.classificationObservations["hat-node"].results.clear();
        expected = "unknown";
    }
    SECTION("wrong source model") {
        target.classificationObservations["hat-node"].modelCode = "other";
        expected                                                = "unknown";
    }
    SECTION("wrong label") {
        target.classificationObservations["hat-node"].results.front().label = "other";
        expected                                                            = "unknown";
    }
    SECTION("inference failure") {
        target.classificationObservations["hat-node"].failed = true;
        expected                                             = "failed";
    }
    SECTION("filtered target") {
        target.bFilter = true;
        expected       = "unknown";
    }
    SECTION("not in any area") {
        target.areaSign.areas.clear();
        expected = "unknown";
    }
    SECTION("NaN") {
        target   = BinaryObservation(std::numeric_limits<float>::quiet_NaN());
        expected = "unknown";
    }
    SECTION("infinity") {
        target   = BinaryObservation(std::numeric_limits<float>::infinity());
        expected = "unknown";
    }
    SECTION("out of range below zero") {
        target   = BinaryObservation(-0.1f);
        expected = "unknown";
    }
    SECTION("out of range above one") {
        target   = BinaryObservation(1.1f);
        expected = "unknown";
    }
    AttributeAccumulator accumulator;
    for (int64_t time : {1000, 2000, 3000})
        accumulator.Observe(schema, target, time, 200);
    const auto value = accumulator.Finish(schema, 3).front();
    REQUIRE(value.status == expected);
    REQUIRE(value.samples == (expected == "valid" ? 3 : 0));
    REQUIRE(value.values ==
            (expected == "valid" ? std::vector<std::string>{"yes"} : std::vector<std::string>{}));
}

TEST_CASE("Binary negatives require enough evidence and categorical attributes keep their semantics",
          "[attributes][binary]") {
    auto schema = BinarySchema();
    AttributeAccumulator accumulator;
    accumulator.Observe(schema, BinaryObservation(0.2f), 1000, 200);
    accumulator.Observe(schema, BinaryObservation(0.2f), 2000, 200);
    REQUIRE(accumulator.Finish(schema, 3).front().status == "unknown");
    schema.attributes.front().type = "single";
    schema.attributes.front().options.resize(1);
    AttributeAccumulator categorical;
    for (int64_t time : {1000, 2000, 3000})
        categorical.Observe(schema, BinaryObservation(0.2f), time, 200);
    REQUIRE(categorical.Finish(schema, 3).front().status == "unknown");
    REQUIRE(categorical.Finish(schema, 3).front().samples == 0);
}

TEST_CASE("Binary schemas require an explicit positive label and a distinct negative value",
          "[attributes][binary]") {
    auto schema = BinarySchema();
    REQUIRE(ValidateAttributeSchema(schema));
    const auto originalId = AttributeSchemaId(schema);
    SECTION("no negative value") {
        schema.attributes.front().options.pop_back();
    }
    SECTION("negative must not bind a second model label") {
        schema.attributes.front().options.back().label = "other";
    }
    SECTION("positive needs a label") {
        schema.attributes.front().options.front().label.clear();
    }
    SECTION("duplicate values") {
        schema.attributes.front().options.back().value = "yes";
    }
    SECTION("more than two values") {
        schema.attributes.front().options.push_back({"other", "other", "Other"});
    }
    REQUIRE_FALSE(ValidateAttributeSchema(schema));
    REQUIRE(AttributeSchemaId(schema) != originalId);
}

TEST_CASE("Negative attributes persist filter count and export like other valid values",
          "[attributes][binary][db]") {
    auto record     = Record("binary-negative");
    record.schema   = BinarySchema();
    record.schemaId = AttributeSchemaId(record.schema);
    AttributeAccumulator accumulator;
    for (int64_t time : {1000, 2000, 3000})
        accumulator.Observe(record.schema, BinaryObservation(0.2f), time, 200);
    record.attributes = accumulator.Finish(record.schema, 3);
    REQUIRE(ValidateAttributeRecord(record));
    auto invalid                      = record;
    invalid.attributes.front().values = {"yes", "no"};
    REQUIRE_FALSE(ValidateAttributeRecord(invalid));
    SQLite::Database database(":memory:", SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
    db::TaskEventDao dao(database);
    dao.CreateTable();
    REQUIRE(dao.Insert(Event(record, "a")));
    db::QueryTaskEventCondition query;
    query.categories          = {"12"};
    query.algorithm_codes     = {"scene"};
    query.time_begin          = 0;
    query.time_end            = 6000;
    query.attribute_schema_id = record.schemaId;
    query.attribute_filters   = {{"hat", "no", "valid"}};
    query.page_num            = 1;
    query.page_size           = 20;
    REQUIRE(dao.Query(query).total_count == 1);
    const auto stats = dao.QueryAttributeSummary(query).statistics;
    REQUIRE(stats.size() == 1);
    REQUIRE(stats.front().value == "no");
    REQUIRE(stats.front().count == 1);
    REQUIRE(stats.front().validCount == 1);
    query.attribute_filters = {{"hat", "", "unknown"}};
    REQUIRE(dao.Query(query).total_count == 0);
    MsgEventUnit event;
    event.property = nlohmann::json{{"attributes", record}}.dump();
    std::ostringstream output;
    WriteAttributeCsv(output, {event}, true);
    REQUIRE(output.str().find("Absent") != std::string::npos);
}

TEST_CASE("Attribute votes isolate source nodes and require stable independent samples", "[attributes]") {
    const auto schema = TestAttributeSchema();
    AttributeAccumulator accumulator;
    accumulator.Observe(schema, Observation("yes", "no"), 1000, 200);
    accumulator.Observe(schema, Observation("no", "yes"), 1000, 200);  // duplicate
    accumulator.Observe(schema, Observation("no", "yes"), 1100, 200);  // throttled
    accumulator.Observe(schema, Observation("yes", "no"), 2000, 200);
    REQUIRE(accumulator.Finish(schema, 3).front().status == "unknown");
    accumulator.Observe(schema, Observation("no", "no"), 3000, 200);
    const auto result = accumulator.Finish(schema, 3);
    REQUIRE(result[0].values == std::vector<std::string>{"yes"});
    REQUIRE(result[0].samples == 3);
    REQUIRE(result[1].values == std::vector<std::string>{"no"});
}

TEST_CASE("Missing failed and ambiguous classifications never become negative attributes", "[attributes]") {
    const auto schema = TestAttributeSchema();
    AttributeAccumulator accumulator;
    auto target                                          = Observation("", "");
    target.classificationObservations["hat-node"].failed = true;
    accumulator.Observe(schema, target, 1000, 200);
    const auto result = accumulator.Finish(schema, 3);
    REQUIRE(result[0].status == "failed");
    REQUIRE(result[1].status == "unknown");
    REQUIRE(result[0].values.empty());
    target     = Observation("yes", "no");
    auto tied  = target.classificationObservations["hat-node"].results.front();
    tied.label = "no";
    target.classificationObservations["hat-node"].results.push_back(tied);
    for (int64_t time : {2000, 3000, 4000})
        accumulator.Observe(schema, target, time, 200);
    REQUIRE(accumulator.Finish(schema, 3)[0].status != "valid");
}

TEST_CASE("Attribute records validate mappings and preserve immutable schema identity", "[attributes]") {
    auto record = Record("record");
    REQUIRE(ValidateAttributeRecord(record));
    REQUIRE(nlohmann::json(record).get<AttributeRecord>().schemaId == record.schemaId);
    SECTION("renaming produces a new version") {
        record.schema.attributes[0].name = "New title";
        REQUIRE(AttributeSchemaId(record.schema) != record.schemaId);
    }
    SECTION("duplicate keys rejected") {
        record.schema.attributes.push_back(record.schema.attributes[0]);
    }
    SECTION("unmapped value rejected") {
        record.attributes[0].values = {"unexpected"};
    }
    SECTION("missing attribute rejected") {
        record.attributes.pop_back();
    }
    REQUIRE_FALSE(ValidateAttributeRecord(record));
}

TEST_CASE("Attribute query and full-result statistics share channel schema and combined filters",
          "[attributes][db]") {
    SQLite::Database database(":memory:", SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
    db::TaskEventDao dao(database);
    dao.CreateTable();
    const auto first = Record("one");
    REQUIRE(dao.Insert(Event(first, "a")));
    REQUIRE(dao.Insert(Event(first, "a")));  // idempotent
    REQUIRE(dao.Insert(Event(Record("two"), "b")));
    REQUIRE(dao.Insert(Event(Record("three", false), "b")));
    auto historical                      = Record("history");
    historical.schema.attributes[0].name = "Old hat name";
    historical.schemaId                  = AttributeSchemaId(historical.schema);
    REQUIRE(dao.Insert(Event(historical, "a")));
    db::QueryTaskEventCondition query;
    query.categories          = {"12"};
    query.algorithm_codes     = {"scene"};
    query.channel_ids         = {"a", "b"};
    query.time_begin          = 0;
    query.time_end            = 6000;
    query.attribute_schema_id = first.schemaId;
    query.page_num            = 1;
    query.page_size           = 1;
    REQUIRE(dao.Query(query).total_count == 3);
    REQUIRE(dao.Query(query).behavior_list.size() == 1);
    auto summary = dao.QueryAttributeSummary(query);
    REQUIRE(summary.schemas.size() == 2);
    const auto hat = std::find_if(summary.statistics.begin(), summary.statistics.end(),
                                  [](const auto& s) { return s.key == "hat" && s.status == "valid"; });
    REQUIRE(hat != summary.statistics.end());
    REQUIRE(hat->count == 2);
    REQUIRE(hat->validCount == 2);
    query.attribute_filters = {{"hat", "yes", "valid"}, {"bag", "no", "valid"}};
    REQUIRE(dao.Query(query).total_count == 2);
    query.channel_ids = {"b"};
    REQUIRE(dao.Query(query).total_count == 1);
    query.attribute_filters = {{"hat", "", "unknown"}};
    REQUIRE(dao.Query(query).total_count == 1);
    query.attribute_filters = {{"hat' OR 1=1 --", "yes", "valid"}};
    REQUIRE(dao.Query(query).total_count == 0);
    dao.RemoveItems({"one", "two", "three", "history"});
    query.attribute_filters.clear();
    query.channel_ids.clear();
    query.attribute_schema_id.clear();
    REQUIRE(dao.QueryAttributeSummary(query).statistics.empty());
    REQUIRE(dao.QueryAttributeSummary(query).schemas.empty());
    REQUIRE_FALSE(dao.Insert(Event(AttributeRecord{}, "a")));
}

TEST_CASE("Attribute CSV uses historic labels and quotes formula-like values", "[attributes][export]") {
    auto record                                 = Record("record");
    record.schema.attributes[0].options[0].name = "=formula,\"quoted\"";
    record.schemaId                             = AttributeSchemaId(record.schema);
    MsgEventUnit event;
    event.property    = nlohmann::json{{"attributes", record}}.dump();
    event.channelName = "A, B";
    std::ostringstream output;
    WriteAttributeCsv(output, {event}, true);
    REQUIRE(output.str().find("\"A, B\"") != std::string::npos);
    REQUIRE(output.str().find("\"'=formula,\"\"quoted\"\"\"") != std::string::npos);
    REQUIRE(CategoryToExportType("12") == ExportType::Attributes);
}

TEST_CASE("Attribute scenes require serial source classifiers after tracking and a reporter",
          "[attributes][schema]") {
    auto node = [](const std::string& action, const std::string& id, const std::string& parent,
                   nlohmann::json params = nlohmann::json::array()) {
        return nlohmann::json{{"actionId", action},
                              {"flowActionId", id},
                              {"preFlowActionId", parent},
                              {"configObject", {{"params", params}}}};
    };
    const auto schema          = TestAttributeSchema();
    const nlohmann::json model = nlohmann::json::array({{{"key", "atomicCode"}, {"value", "shared-model"}}});
    nlohmann::json flow        = nlohmann::json::array(
        {node("AA_00003", "track", "-1"), node("AA_00002", "hat-node", "track", model),
                node("AA_00002", "bag-node", "hat-node", model),
                node(
             "BA_20003", "accumulate", "bag-node",
             nlohmann::json::array({{{"key", "inputMode"}, {"value", "attributes"}},
                                           {{"key", "attributeSchema"}, {"value", nlohmann::json(schema).dump()}}})),
                node("BA_00004", "report", "accumulate")});
    REQUIRE(ValidateAttributeFlow(flow.dump(), true));
    SECTION("untracked") {
        flow[0]["actionId"] = "AA_00001";
    }
    SECTION("unbound model") {
        flow[1]["configObject"]["params"][0]["value"] = "other";
    }
    SECTION("unmerged branches") {
        flow[3]["preFlowActionId"] = "hat-node,bag-node";
    }
    SECTION("missing reporter") {
        flow.erase(4);
    }
    SECTION("multiple accumulators") {
        flow.push_back(flow[3]);
    }
    REQUIRE_FALSE(ValidateAttributeFlow(flow.dump(), true));
    REQUIRE(ValidateAttributeFlow("[]"));
    REQUIRE_FALSE(ValidateAttributeFlow("[]", true));
}
