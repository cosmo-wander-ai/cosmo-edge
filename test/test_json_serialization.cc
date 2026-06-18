// JSON serialization consistency tests.
// Captures current x2struct behavior as golden baselines for migration to nlohmann/json.
// Covers all 5 XTOSTRUCT patterns: O(), M(), I(), A(), CC().

#include <memory>
#include <string>

#include "catch_amalgamated.hpp"
#include "platform/NetCardOp.h"
#include "serialization/x2struct.hpp"
#include "util/JsonStructUtil.h"
#include "util/LicenseManager.h"
#include "util/MsgBaseTypes.h"
#include "util/MsgDynamicElement.h"
#include "util/dto/ServerMsgTypes.h"
#include "util/dto/TaskCreateTypes.h"

// ---------------------------------------------------------------------------
// Helper: parse JSON string into rapidjson Document for field-level assertions
// ---------------------------------------------------------------------------
namespace {
rapidjson::Document ParseJson(const std::string& json_str) {
    rapidjson::Document doc;
    doc.Parse(json_str.c_str());
    REQUIRE_FALSE(doc.HasParseError());
    return doc;
}
}  // namespace

// ===========================================================================
// Pattern 1: O() — Optional fields (most common, ~600 uses)
// ===========================================================================
TEST_CASE("x2struct O(): optional fields round-trip", "[json][baseline]") {
    SECTION("MsgResBase — simple O(msgCode, msgText)") {
        cosmo::MsgResBase original;
        original.msgCode = "200";
        original.msgText = "success";

        std::string json;
        REQUIRE(cosmo::util::EncodeJson(original, json));

        auto doc = ParseJson(json);
        REQUIRE(doc.HasMember("msgCode"));
        REQUIRE(doc.HasMember("msgText"));
        CHECK(std::string(doc["msgCode"].GetString()) == "200");
        CHECK(std::string(doc["msgText"].GetString()) == "success");

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
TEST_CASE("x2struct M(): mandatory fields", "[json][baseline]") {
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
TEST_CASE("x2struct I(): inheritance serialization", "[json][baseline]") {
    SECTION("MsgInfoRecv — I(MsgRecvHead) + M(devId)") {
        cosmo::MsgInfoRecv original;
        original.devId = "device-001";

        std::string json;
        REQUIRE(cosmo::util::EncodeJson(original, json));

        auto doc = ParseJson(json);
        REQUIRE(doc.HasMember("devId"));
        CHECK(std::string(doc["devId"].GetString()) == "device-001");

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
        REQUIRE(doc.HasMember("devId"));
        REQUIRE(doc.HasMember("runtimeDuration"));
        CHECK(std::string(doc["devId"].GetString()) == "device-001");

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
TEST_CASE("x2struct A(): alias field names", "[json][baseline]") {
    SECTION("MsgPoint — A(x, xRatio), A(y, yRatio)") {
        cosmo::MsgPoint original;
        original.x = 0.5;
        original.y = 0.8;

        std::string json;
        REQUIRE(cosmo::util::EncodeJson(original, json));

        auto doc = ParseJson(json);
        // JSON key must be the alias, not the member name
        REQUIRE(doc.HasMember("xRatio"));
        REQUIRE(doc.HasMember("yRatio"));
        CHECK_FALSE(doc.HasMember("x"));
        CHECK_FALSE(doc.HasMember("y"));
        CHECK(doc["xRatio"].GetDouble() == Catch::Approx(0.5));
        CHECK(doc["yRatio"].GetDouble() == Catch::Approx(0.8));

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
        REQUIRE(doc.HasMember("xRatio"));
        REQUIRE(doc.HasMember("yRatio"));
        REQUIRE(doc.HasMember("wRatio"));
        REQUIRE(doc.HasMember("hRatio"));

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
        REQUIRE(doc.HasMember("dhcp"));
        REQUIRE(doc.HasMember("ethName"));
        REQUIRE(doc.HasMember("ipAddr"));
        REQUIRE(doc.HasMember("netMask"));
        REQUIRE(doc.HasMember("gateway"));
        // Member names must NOT appear in JSON
        CHECK_FALSE(doc.HasMember("dhcp_"));
        CHECK_FALSE(doc.HasMember("eth_name_"));
        CHECK_FALSE(doc.HasMember("ip_addr_"));

        cosmo::platform::NetCardInfo restored;
        REQUIRE(cosmo::util::DecodeJson(json, restored));
        CHECK(restored.dhcp == 1);
        CHECK(restored.eth_name == "eth0");
        CHECK(restored.ip_addr == "192.168.1.100");
        CHECK(restored.net_mask == "255.255.255.0");
        CHECK(restored.gateway == "192.168.1.1");
    }

    SECTION("ServiceDetail — all A() alias fields") {
        cosmo::util::ServiceDetail original;
        original.auth_count_            = 10;
        original.auth_day_              = 365;
        original.service_category_type_ = 1;
        original.sub_service_type_      = 10000;
        original.sub_service_name_      = "detection";

        std::string json;
        REQUIRE(cosmo::util::EncodeJson(original, json));

        auto doc = ParseJson(json);
        REQUIRE(doc.HasMember("authCount"));
        REQUIRE(doc.HasMember("authDay"));
        REQUIRE(doc.HasMember("serviceCategoryType"));
        REQUIRE(doc.HasMember("subServiceType"));
        REQUIRE(doc.HasMember("subServiceName"));

        cosmo::util::ServiceDetail restored;
        REQUIRE(cosmo::util::DecodeJson(json, restored));
        CHECK(restored.auth_count_ == 10);
        CHECK(restored.auth_day_ == 365);
        CHECK(restored.sub_service_type_ == 10000);
        CHECK(restored.sub_service_name_ == "detection");
    }

    SECTION("LicenseContent — A() with nested vector of aliased structs") {
        cosmo::util::LicenseContent original;
        original.auth_date_ = "2026-01-01";
        original.device_sn_ = "SN-12345";

        cosmo::util::ServiceDetail svc;
        svc.auth_count_       = 5;
        svc.auth_day_         = 90;
        svc.sub_service_type_ = 10000;
        svc.sub_service_name_ = "face_detect";
        original.service_detail_.push_back(svc);

        std::string json;
        REQUIRE(cosmo::util::EncodeJson(original, json));

        auto doc = ParseJson(json);
        REQUIRE(doc.HasMember("authDate"));
        REQUIRE(doc.HasMember("deviceSn"));
        REQUIRE(doc.HasMember("serviceDetail"));
        REQUIRE(doc["serviceDetail"].IsArray());
        REQUIRE(doc["serviceDetail"].Size() == 1);
        CHECK(doc["serviceDetail"][0].HasMember("authCount"));

        cosmo::util::LicenseContent restored;
        REQUIRE(cosmo::util::DecodeJson(json, restored));
        CHECK(restored.auth_date_ == "2026-01-01");
        CHECK(restored.device_sn_ == "SN-12345");
        REQUIRE(restored.service_detail_.size() == 1);
        CHECK(restored.service_detail_[0].auth_count_ == 5);
    }
}

// ===========================================================================
// Pattern 5: CC() — Conditional serialization (2 core uses + MsgDynamicElement)
// ===========================================================================
TEST_CASE("x2struct CC(): conditional serialization", "[json][baseline]") {
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
        REQUIRE(doc.HasMember("resCode"));
        REQUIRE(doc.HasMember("resMsg"));
        CHECK(doc["resCode"].GetInt() == 1);
        // ChinaMobile fields should NOT be present
        CHECK_FALSE(doc.HasMember("resultCode"));
        CHECK_FALSE(doc.HasMember("resultMsg"));
    }

    SECTION("MsgSendHead — CC(ChinaMobile): resultCode/resultMsg present") {
        cosmo::MsgSendHead original;
        original.msgSendType = cosmo::MsgSendType::ChinaMobile;
        original.resultCode  = "200";
        original.resultMsg   = "success";

        std::string json;
        REQUIRE(cosmo::util::EncodeJson(original, json));

        auto doc = ParseJson(json);
        REQUIRE(doc.HasMember("resultCode"));
        REQUIRE(doc.HasMember("resultMsg"));
        CHECK(std::string(doc["resultCode"].GetString()) == "200");
        // CWAI fields should NOT be present
        CHECK_FALSE(doc.HasMember("resCode"));
        CHECK_FALSE(doc.HasMember("resMsg"));
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
        REQUIRE(doc.HasMember("box"));

        // CC(bHaveLogicResult, bLogicResult) — false, so bLogicResult absent
        CHECK_FALSE(doc.HasMember("bLogicResult"));

        // CC(!confidence.empty(), confidence) — empty, so absent
        CHECK_FALSE(doc.HasMember("confidence"));

        // CC(!groupEls.empty(), groupEls) — empty, so absent
        CHECK_FALSE(doc.HasMember("groupEls"));

        // CC(bHaveMatchInfo, matchInfo) — true, so present
        REQUIRE(doc.HasMember("matchInfo"));
        CHECK(doc["matchInfo"].HasMember("matchId"));

        // Now test with conditions enabled
        original.bHaveLogicResult = true;
        original.confidence.push_back({"person", 0.9f});
        original.groupEls.push_back(1);
        original.groupEls.push_back(2);

        std::string json2;
        REQUIRE(cosmo::util::EncodeJson(original, json2));

        auto doc2 = ParseJson(json2);
        REQUIRE(doc2.HasMember("bLogicResult"));
        REQUIRE(doc2.HasMember("confidence"));
        REQUIRE(doc2.HasMember("groupEls"));
        CHECK(doc2["bLogicResult"].GetBool() == true);
        CHECK(doc2["confidence"].IsArray());
        CHECK(doc2["confidence"].Size() == 1);
        CHECK(doc2["groupEls"].IsArray());
        CHECK(doc2["groupEls"].Size() == 2);
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
        REQUIRE(doc_slider.HasMember("min"));
        REQUIRE(doc_slider.HasMember("max"));
        CHECK_FALSE(doc_slider.HasMember("options"));
        CHECK_FALSE(doc_slider.HasMember("regexpr"));

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
        REQUIRE(doc_radio.HasMember("options"));
        CHECK_FALSE(doc_radio.HasMember("min"));
        CHECK_FALSE(doc_radio.HasMember("max"));

        // type == "text" → regexpr, failedTip present
        cosmo::MsgDynamicElement text;
        text.key       = "name";
        text.type      = "text";
        text.regexpr   = "^[a-zA-Z]+$";
        text.failedTip = "Letters only";

        std::string json_text;
        REQUIRE(cosmo::util::EncodeJson(text, json_text));

        auto doc_text = ParseJson(json_text);
        REQUIRE(doc_text.HasMember("regexpr"));
        REQUIRE(doc_text.HasMember("failedTip"));
        CHECK_FALSE(doc_text.HasMember("min"));
        CHECK_FALSE(doc_text.HasMember("options"));
    }
}

// ===========================================================================
// Pattern 6: E() — Empty base marker
// ===========================================================================
TEST_CASE("x2struct E(): empty struct marker", "[json][baseline]") {
    SECTION("MsgRecvHead — E() produces empty JSON") {
        cosmo::MsgRecvHead original;
        std::string json;
        REQUIRE(cosmo::util::EncodeJson(original, json));
        // E() should produce a valid but empty JSON object
        auto doc = ParseJson(json);
        CHECK(doc.IsObject());
    }
}

// ===========================================================================
// Complex integration: nested structs with multiple patterns
// ===========================================================================
TEST_CASE("x2struct complex: nested struct round-trip", "[json][baseline]") {
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
        REQUIRE(json_points.IsArray());
        REQUIRE(json_points.Size() == 3);
        CHECK(json_points[0].HasMember("xRatio"));
        CHECK(json_points[0].HasMember("yRatio"));
        CHECK_FALSE(json_points[0].HasMember("x"));
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
        CHECK(doc.IsObject());
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
