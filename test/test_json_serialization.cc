// JSON serialization consistency tests.
// Captures current nlohmann/json serialization behavior as golden baselines.
// Covers legacy JSON patterns: O(), M(), I(), A(), CC().

#include <memory>
#include <string>

#include "catch_amalgamated.hpp"
#include "nlohmann/json.hpp"
#include "platform/NetCardOp.h"
#include "util/JsonStructUtil.h"
#include "util/MsgBaseTypes.h"
#include "util/MsgDynamicElement.h"
#include "util/dto/ServerMsgTypes.h"
#include "util/dto/TaskCreateTypes.h"

// ---------------------------------------------------------------------------
// Helper: parse JSON string into nlohmann::json for field-level assertions
// ---------------------------------------------------------------------------
namespace {
nlohmann::json ParseJson(const std::string& json_str) {
    auto doc = nlohmann::json::parse(json_str, nullptr, false);
    REQUIRE_FALSE(doc.is_discarded());
    return doc;
}
}  // namespace

// ===========================================================================
// Pattern 1: O() — Optional fields (most common, ~600 uses)
// ===========================================================================
TEST_CASE("legacy JSON O(): optional fields round-trip", "[json][baseline]") {
    SECTION("MsgResBase — simple O(msgCode, msgText)") {
        cosmo::MsgResBase original;
        original.msgCode = "200";
        original.msgText = "success";

        std::string json;
        REQUIRE(cosmo::util::EncodeJson(original, json));

        auto doc = ParseJson(json);
        REQUIRE(doc.contains("msgCode"));
        REQUIRE(doc.contains("msgText"));
        CHECK(doc["msgCode"].get<std::string>() == "200");
        CHECK(doc["msgText"].get<std::string>() == "success");

        cosmo::MsgResBase restored;
        REQUIRE(cosmo::util::DecodeJson(json, restored));
        CHECK(restored.msgCode == original.msgCode);
        CHECK(restored.msgText == original.msgText);
    }

    SECTION("MsgAiConfidence — O() with float") {
        cosmo::MsgAiConfidence original;
        original.label      = "person";
        original.confidence = 0.95f;

        std::string json;
        REQUIRE(cosmo::util::EncodeJson(original, json));

        cosmo::MsgAiConfidence restored;
        REQUIRE(cosmo::util::DecodeJson(json, restored));
        CHECK(restored.label == "person");
        CHECK(restored.confidence == Catch::Approx(0.95f).margin(0.01f));
    }

    SECTION("MsgTaskConfig — O() with nested vectors") {
        cosmo::MsgTaskConfig original;
        cosmo::MsgDynamicKeyValue kv;
        kv.key   = "threshold";
        kv.value = "0.5";
        original.params.push_back(kv);

        std::string json;
        REQUIRE(cosmo::util::EncodeJson(original, json));

        cosmo::MsgTaskConfig restored;
        REQUIRE(cosmo::util::DecodeJson(json, restored));
        REQUIRE(restored.params.size() == 1);
        CHECK(restored.params[0].key == "threshold");
        CHECK(restored.params[0].value == "0.5");
    }

    SECTION("O() missing field uses default") {
        // Deserialize JSON with missing optional field
        std::string json = R"({"label":"cat"})";
        cosmo::MsgAiConfidence restored;
        REQUIRE(cosmo::util::DecodeJson(json, restored));
        CHECK(restored.label == "cat");
        CHECK(restored.confidence == 0.0f);  // default
    }
}

// ===========================================================================
// Pattern 2: M() — Mandatory fields (~80 uses)
// ===========================================================================
TEST_CASE("legacy JSON M(): mandatory fields", "[json][baseline]") {
    SECTION("MsgRunTime — M(timeBegin, timeEnd) both present") {
        cosmo::MsgRunTime original;
        original.timeBegin = "08:00:00";
        original.timeEnd   = "20:00:00";

        std::string json;
        REQUIRE(cosmo::util::EncodeJson(original, json));

        cosmo::MsgRunTime restored;
        REQUIRE(cosmo::util::DecodeJson(json, restored));
        CHECK(restored.timeBegin == "08:00:00");
        CHECK(restored.timeEnd == "20:00:00");
    }

    SECTION("MsgRunTime — M() missing mandatory field throws") {
        std::string json = R"({"timeBegin":"08:00:00"})";  // missing timeEnd
        cosmo::MsgRunTime restored;
        CHECK_FALSE(cosmo::util::DecodeJson(json, restored));
    }

    SECTION("MsgDynamicKeyValue — M(key) + O(value)") {
        // Only mandatory key present, optional value missing
        std::string json = R"({"key":"threshold"})";
        cosmo::MsgDynamicKeyValue restored;
        REQUIRE(cosmo::util::DecodeJson(json, restored));
        CHECK(restored.key == "threshold");
        CHECK(restored.value == "");  // default empty
    }

    SECTION("MsgDynamicKeyValue — M(key) missing throws") {
        std::string json = R"({"value":"0.5"})";  // missing mandatory key
        cosmo::MsgDynamicKeyValue restored;
        CHECK_FALSE(cosmo::util::DecodeJson(json, restored));
    }

    SECTION("MsgTaskCreateRecv — M() + O() mix") {
        cosmo::MsgTaskCreateRecv original;
        original.taskId              = "task-001";
        original.videoChannelId      = "ch-01";
        original.algorithmCode       = "det_person";
        original.algorithmUpdateTime = "1716883200000";
        original.taskDesc            = "test task";

        std::string json;
        REQUIRE(cosmo::util::EncodeJson(original, json));

        cosmo::MsgTaskCreateRecv restored;
        REQUIRE(cosmo::util::DecodeJson(json, restored));
        CHECK(restored.taskId == "task-001");
        CHECK(restored.videoChannelId == "ch-01");
        CHECK(restored.algorithmCode == "det_person");
        CHECK(restored.algorithmUpdateTime == "1716883200000");
        CHECK(restored.taskDesc == "test task");
    }
}

// ===========================================================================
// Pattern 3: I() — Inheritance (~200 uses)
// ===========================================================================
TEST_CASE("legacy JSON I(): inheritance serialization", "[json][baseline]") {
    SECTION("MsgInfoRecv — I(MsgRecvHead) + M(devId)") {
        cosmo::MsgInfoRecv original;
        original.devId = "device-001";

        std::string json;
        REQUIRE(cosmo::util::EncodeJson(original, json));

        auto doc = ParseJson(json);
        REQUIRE(doc.contains("devId"));
        CHECK(doc["devId"].get<std::string>() == "device-001");

        cosmo::MsgInfoRecv restored;
        REQUIRE(cosmo::util::DecodeJson(json, restored));
        CHECK(restored.devId == "device-001");
    }

    SECTION("MsgInfoSend — I(MsgSendHead, CMsgHeartBeatReq) double inheritance") {
        cosmo::MsgInfoSend original;
        original.resCode         = 1;
        original.devId           = "device-001";
        original.runtimeDuration = 3600;
        original.cpuUsage        = 0.45f;

        std::string json;
        REQUIRE(cosmo::util::EncodeJson(original, json));

        auto doc = ParseJson(json);
        // MsgSendHead fields should be present via CC conditional
        // CMsgHeartBeatReq fields should be present
        REQUIRE(doc.contains("devId"));
        REQUIRE(doc.contains("runtimeDuration"));
        CHECK(doc["devId"].get<std::string>() == "device-001");

        cosmo::MsgInfoSend restored;
        REQUIRE(cosmo::util::DecodeJson(json, restored));
        CHECK(restored.devId == "device-001");
        CHECK(restored.runtimeDuration == 3600);
        CHECK(restored.cpuUsage == Catch::Approx(0.45f).margin(0.01f));
    }

    SECTION("MsgTaskCreateSend — I(MsgSendHead) inherits-only") {
        cosmo::MsgTaskCreateSend original;
        original.msgSendType = cosmo::MsgSendType::CWAI;
        original.resCode     = 1;
        cosmo::MsgResBase res;
        res.msgCode = "0";
        res.msgText = "OK";
        original.resMsg.push_back(res);

        std::string json;
        REQUIRE(cosmo::util::EncodeJson(original, json));

        cosmo::MsgTaskCreateSend restored;
        REQUIRE(cosmo::util::DecodeJson(json, restored));
        REQUIRE(restored.resMsg.size() == 1);
        CHECK(restored.resMsg[0].msgText == "OK");
    }

    SECTION("MsgDynamicElement — I(MsgDynamicKeyValue) + own O() fields") {
        cosmo::MsgDynamicElement original;
        original.key         = "sensitivity";
        original.value       = "80";
        original.name        = "Sensitivity";
        original.type        = "slider";
        original.description = "Detection sensitivity";
        original.min         = 0.0f;
        original.max         = 100.0f;

        std::string json;
        REQUIRE(cosmo::util::EncodeJson(original, json));

        cosmo::MsgDynamicElement restored;
        REQUIRE(cosmo::util::DecodeJson(json, restored));
        // Base class fields
        CHECK(restored.key == "sensitivity");
        CHECK(restored.value == "80");
        // Own fields
        CHECK(restored.name == "Sensitivity");
        CHECK(restored.type == "slider");
        // CC(type == "slider", min, max) — conditional fields present
        CHECK(restored.min == Catch::Approx(0.0f));
        CHECK(restored.max == Catch::Approx(100.0f));
    }
}

// ===========================================================================
// Pattern 4: A() — Alias mapping (~18 uses)
// ===========================================================================
TEST_CASE("legacy JSON A(): alias field names", "[json][baseline]") {
    SECTION("MsgPoint — A(x, xRatio), A(y, yRatio)") {
        cosmo::MsgPoint original;
        original.x = 0.5;
        original.y = 0.8;

        std::string json;
        REQUIRE(cosmo::util::EncodeJson(original, json));

        auto doc = ParseJson(json);
        // JSON key must be the alias, not the member name
        REQUIRE(doc.contains("xRatio"));
        REQUIRE(doc.contains("yRatio"));
        CHECK_FALSE(doc.contains("x"));
        CHECK_FALSE(doc.contains("y"));
        CHECK(doc["xRatio"].get<double>() == Catch::Approx(0.5));
        CHECK(doc["yRatio"].get<double>() == Catch::Approx(0.8));

        // Round-trip via alias keys
        cosmo::MsgPoint restored;
        REQUIRE(cosmo::util::DecodeJson(json, restored));
        CHECK(restored.x == Catch::Approx(0.5));
        CHECK(restored.y == Catch::Approx(0.8));
    }

    SECTION("MsgRect — A(x,xRatio), A(y,yRatio), A(width,wRatio), A(height,hRatio)") {
        cosmo::MsgRect original;
        original.x      = 0.1;
        original.y      = 0.2;
        original.width  = 0.3;
        original.height = 0.4;

        std::string json;
        REQUIRE(cosmo::util::EncodeJson(original, json));

        auto doc = ParseJson(json);
        REQUIRE(doc.contains("xRatio"));
        REQUIRE(doc.contains("yRatio"));
        REQUIRE(doc.contains("wRatio"));
        REQUIRE(doc.contains("hRatio"));

        cosmo::MsgRect restored;
        REQUIRE(cosmo::util::DecodeJson(json, restored));
        CHECK(restored.x == Catch::Approx(0.1));
        CHECK(restored.y == Catch::Approx(0.2));
        CHECK(restored.width == Catch::Approx(0.3));
        CHECK(restored.height == Catch::Approx(0.4));
    }

    SECTION("NetCardInfo — multiple A() aliases") {
        cosmo::platform::NetCardInfo original;
        original.dhcp     = 1;
        original.eth_name = "eth0";
        original.ip_addr  = "192.168.1.100";
        original.net_mask = "255.255.255.0";
        original.gateway  = "192.168.1.1";

        std::string json;
        REQUIRE(cosmo::util::EncodeJson(original, json));

        auto doc = ParseJson(json);
        REQUIRE(doc.contains("dhcp"));
        REQUIRE(doc.contains("ethName"));
        REQUIRE(doc.contains("ipAddr"));
        REQUIRE(doc.contains("netMask"));
        REQUIRE(doc.contains("gateway"));
        // Member names must NOT appear in JSON
        CHECK_FALSE(doc.contains("dhcp_"));
        CHECK_FALSE(doc.contains("eth_name_"));
        CHECK_FALSE(doc.contains("ip_addr_"));

        cosmo::platform::NetCardInfo restored;
        REQUIRE(cosmo::util::DecodeJson(json, restored));
        CHECK(restored.dhcp == 1);
        CHECK(restored.eth_name == "eth0");
        CHECK(restored.ip_addr == "192.168.1.100");
        CHECK(restored.net_mask == "255.255.255.0");
        CHECK(restored.gateway == "192.168.1.1");
    }
}

// ===========================================================================
// Pattern 5: CC() — Conditional serialization (2 core uses + MsgDynamicElement)
// ===========================================================================
TEST_CASE("legacy JSON CC(): conditional serialization", "[json][baseline]") {
    SECTION("MsgSendHead — CC(CWAI): resCode/resMsg present") {
        cosmo::MsgSendHead original;
        original.msgSendType = cosmo::MsgSendType::CWAI;
        original.resCode     = 1;
        cosmo::MsgResBase res;
        res.msgCode = "0";
        res.msgText = "OK";
        original.resMsg.push_back(res);

        std::string json;
        REQUIRE(cosmo::util::EncodeJson(original, json));

        auto doc = ParseJson(json);
        REQUIRE(doc.contains("resCode"));
        REQUIRE(doc.contains("resMsg"));
        CHECK(doc["resCode"].get<int>() == 1);
        // ChinaMobile fields should NOT be present
        CHECK_FALSE(doc.contains("resultCode"));
        CHECK_FALSE(doc.contains("resultMsg"));
    }

    SECTION("MsgSendHead — CC(ChinaMobile): resultCode/resultMsg present") {
        cosmo::MsgSendHead original;
        original.msgSendType = cosmo::MsgSendType::ChinaMobile;
        original.resultCode  = "200";
        original.resultMsg   = "success";

        std::string json;
        REQUIRE(cosmo::util::EncodeJson(original, json));

        auto doc = ParseJson(json);
        REQUIRE(doc.contains("resultCode"));
        REQUIRE(doc.contains("resultMsg"));
        CHECK(doc["resultCode"].get<std::string>() == "200");
        // CWAI fields should NOT be present
        CHECK_FALSE(doc.contains("resCode"));
        CHECK_FALSE(doc.contains("resMsg"));
    }

    SECTION("MsgSendHead — round-trip preserves mode") {
        // CWAI mode
        cosmo::MsgSendHead mv;
        mv.msgSendType = cosmo::MsgSendType::CWAI;
        mv.resCode     = 1;

        std::string json_mv;
        REQUIRE(cosmo::util::EncodeJson(mv, json_mv));

        cosmo::MsgSendHead restored_mv;
        restored_mv.msgSendType = cosmo::MsgSendType::CWAI;
        REQUIRE(cosmo::util::DecodeJson(json_mv, restored_mv));
        CHECK(restored_mv.resCode == 1);
    }

    SECTION("MsgPTaskTarget — CC with boolean and empty-check conditions") {
        cosmo::MsgPTaskTarget original;
        original.box.x      = 100;
        original.box.y      = 200;
        original.box.width  = 50;
        original.box.height = 60;

        // bHaveLogicResult = false → bLogicResult should NOT be serialized
        original.bHaveLogicResult = false;
        original.bLogicResult     = true;

        // confidence empty → should NOT be serialized
        // groupEls empty → should NOT be serialized

        // bHaveMatchInfo = true → matchInfo SHOULD be serialized
        original.bHaveMatchInfo      = true;
        original.matchInfo.matchId   = "match-001";
        original.matchInfo.matched   = true;
        original.matchInfo.groupId   = "group-1";
        original.matchInfo.groupName = "VIP";

        std::string json;
        REQUIRE(cosmo::util::EncodeJson(original, json));

        auto doc = ParseJson(json);
        // box is always O() — should be present
        REQUIRE(doc.contains("box"));

        // CC(bHaveLogicResult, bLogicResult) — false, so bLogicResult absent
        CHECK_FALSE(doc.contains("bLogicResult"));

        // CC(!confidence.empty(), confidence) — empty, so absent
        CHECK_FALSE(doc.contains("confidence"));

        // CC(!groupEls.empty(), groupEls) — empty, so absent
        CHECK_FALSE(doc.contains("groupEls"));

        // CC(bHaveMatchInfo, matchInfo) — true, so present
        REQUIRE(doc.contains("matchInfo"));
        CHECK(doc["matchInfo"].contains("matchId"));

        // Now test with conditions enabled
        original.bHaveLogicResult = true;
        original.confidence.push_back({"person", 0.9f});
        original.groupEls.push_back(1);
        original.groupEls.push_back(2);

        std::string json2;
        REQUIRE(cosmo::util::EncodeJson(original, json2));

        auto doc2 = ParseJson(json2);
        REQUIRE(doc2.contains("bLogicResult"));
        REQUIRE(doc2.contains("confidence"));
        REQUIRE(doc2.contains("groupEls"));
        CHECK(doc2["bLogicResult"].get<bool>() == true);
        CHECK(doc2["confidence"].is_array());
        CHECK(doc2["confidence"].size() == 1);
        CHECK(doc2["groupEls"].is_array());
        CHECK(doc2["groupEls"].size() == 2);
    }

    SECTION("MsgDynamicElement — CC with type-based conditionals") {
        // type == "slider" → min, max present; options absent
        cosmo::MsgDynamicElement slider;
        slider.key  = "thresh";
        slider.type = "slider";
        slider.min  = 0.0f;
        slider.max  = 100.0f;

        std::string json_slider;
        REQUIRE(cosmo::util::EncodeJson(slider, json_slider));

        auto doc_slider = ParseJson(json_slider);
        REQUIRE(doc_slider.contains("min"));
        REQUIRE(doc_slider.contains("max"));
        CHECK_FALSE(doc_slider.contains("options"));
        CHECK_FALSE(doc_slider.contains("regexpr"));

        // type == "radio" → options present; min, max absent
        cosmo::MsgDynamicElement radio;
        radio.key  = "mode";
        radio.type = "radio";
        cosmo::MsgDynamicElement::Option opt;
        opt.name  = "Fast";
        opt.value = "fast";
        radio.options.push_back(opt);

        std::string json_radio;
        REQUIRE(cosmo::util::EncodeJson(radio, json_radio));

        auto doc_radio = ParseJson(json_radio);
        REQUIRE(doc_radio.contains("options"));
        CHECK_FALSE(doc_radio.contains("min"));
        CHECK_FALSE(doc_radio.contains("max"));

        // type == "text" → regexpr, failedTip present
        cosmo::MsgDynamicElement text;
        text.key       = "name";
        text.type      = "text";
        text.regexpr   = "^[a-zA-Z]+$";
        text.failedTip = "Letters only";

        std::string json_text;
        REQUIRE(cosmo::util::EncodeJson(text, json_text));

        auto doc_text = ParseJson(json_text);
        REQUIRE(doc_text.contains("regexpr"));
        REQUIRE(doc_text.contains("failedTip"));
        CHECK_FALSE(doc_text.contains("min"));
        CHECK_FALSE(doc_text.contains("options"));
    }
}

// ===========================================================================
// Pattern 6: E() — Empty base marker
// ===========================================================================
TEST_CASE("legacy JSON E(): empty struct marker", "[json][baseline]") {
    SECTION("MsgRecvHead — E() produces empty JSON") {
        cosmo::MsgRecvHead original;
        std::string json;
        REQUIRE(cosmo::util::EncodeJson(original, json));
        // E() should produce a valid but empty JSON object
        auto doc = ParseJson(json);
        CHECK(doc.is_object());
    }
}

// ===========================================================================
// Complex integration: nested structs with multiple patterns
// ===========================================================================
TEST_CASE("legacy JSON complex: nested struct round-trip", "[json][baseline]") {
    SECTION("MsgTaskCreateRecv — I + M + O with nested MsgTaskConfig") {
        cosmo::MsgTaskCreateRecv original;
        original.taskId              = "task-integration-001";
        original.videoChannelId      = "ch-01";
        original.algorithmCode       = "det_person_v2";
        original.algorithmUpdateTime = "1716883200000";
        original.taskDesc            = "integration test";
        original.streamUrl           = "rtsp://192.168.1.10/stream1";

        // Nested MsgTaskConfig with areas containing MsgPoint (aliased)
        cosmo::MsgTaskArea area;
        area.areaId = "area-01";
        area.name   = "entrance";
        cosmo::MsgPoint p1, p2, p3;
        p1.x = 0.1;
        p1.y = 0.1;
        p2.x = 0.9;
        p2.y = 0.1;
        p3.x = 0.5;
        p3.y = 0.9;
        area.points.push_back(p1);
        area.points.push_back(p2);
        area.points.push_back(p3);
        original.taskConfig.areas.push_back(area);

        std::string json;
        REQUIRE(cosmo::util::EncodeJson(original, json));

        cosmo::MsgTaskCreateRecv restored;
        REQUIRE(cosmo::util::DecodeJson(json, restored));

        // Top-level mandatory fields
        CHECK(restored.taskId == "task-integration-001");
        CHECK(restored.algorithmCode == "det_person_v2");

        // Nested config
        REQUIRE(restored.taskConfig.areas.size() == 1);
        CHECK(restored.taskConfig.areas[0].areaId == "area-01");

        // Deeply nested aliased points
        REQUIRE(restored.taskConfig.areas[0].points.size() == 3);
        CHECK(restored.taskConfig.areas[0].points[0].x == Catch::Approx(0.1));
        CHECK(restored.taskConfig.areas[0].points[2].y == Catch::Approx(0.9));

        // Verify alias in nested JSON
        auto doc                = ParseJson(json);
        const auto& json_points = doc["taskConfig"]["areas"][0]["points"];
        REQUIRE(json_points.is_array());
        REQUIRE(json_points.size() == 3);
        CHECK(json_points[0].contains("xRatio"));
        CHECK(json_points[0].contains("yRatio"));
        CHECK_FALSE(json_points[0].contains("x"));
    }

    SECTION("MsgPTaskDetectPicSend — I + O with anonymous nested struct") {
        cosmo::MsgPTaskDetectPicSend original;
        original.msgSendType           = cosmo::MsgSendType::CWAI;
        original.resCode               = 1;
        original.resData.algorithmCode = "face_detect";
        original.resData.timestamp     = "1716883200000";

        cosmo::MsgPTaskArea area;
        area.areaId    = "a1";
        area.bDetected = true;
        cosmo::MsgPTaskTarget target;
        target.box.x      = 10;
        target.box.y      = 20;
        target.box.width  = 30;
        target.box.height = 40;
        area.targetList.push_back(target);
        original.resData.areaList.push_back(area);

        std::string json;
        REQUIRE(cosmo::util::EncodeJson(original, json));

        cosmo::MsgPTaskDetectPicSend restored;
        restored.msgSendType = cosmo::MsgSendType::CWAI;
        REQUIRE(cosmo::util::DecodeJson(json, restored));

        CHECK(restored.resData.algorithmCode == "face_detect");
        REQUIRE(restored.resData.areaList.size() == 1);
        CHECK(restored.resData.areaList[0].areaId == "a1");
        CHECK(restored.resData.areaList[0].bDetected == true);
        REQUIRE(restored.resData.areaList[0].targetList.size() == 1);
        CHECK(restored.resData.areaList[0].targetList[0].box.x == 10);
    }
}

// ===========================================================================
// JsonStructUtil wrapper functions
// ===========================================================================
TEST_CASE("JsonStructUtil: EncodeJson/DecodeJson wrappers", "[json][baseline]") {
    SECTION("EncodeJson produces valid JSON") {
        cosmo::MsgResBase msg;
        msg.msgCode = "100";
        msg.msgText = "test";
        std::string json;
        REQUIRE(cosmo::util::EncodeJson(msg, json));
        CHECK_FALSE(json.empty());

        auto doc = ParseJson(json);
        CHECK(doc.is_object());
    }

    SECTION("DecodeJson handles malformed JSON gracefully") {
        cosmo::MsgResBase restored;
        CHECK_FALSE(cosmo::util::DecodeJson("{invalid json}", restored));
    }

    SECTION("DecodeJson handles empty string gracefully") {
        cosmo::MsgResBase restored;
        CHECK_FALSE(cosmo::util::DecodeJson("", restored));
    }

    SECTION("EncodeJson with custom indent") {
        cosmo::MsgResBase msg;
        msg.msgCode = "0";
        std::string compact;
        REQUIRE(cosmo::util::EncodeJson(msg, compact, -1));  // no indent
        CHECK(compact.find('\n') == std::string::npos);

        std::string pretty;
        REQUIRE(cosmo::util::EncodeJson(msg, pretty, 2));  // 2-space indent
        CHECK(pretty.find('\n') != std::string::npos);
    }
}
