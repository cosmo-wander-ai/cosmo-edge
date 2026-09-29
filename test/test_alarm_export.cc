// clang-format off
#include "catch_amalgamated.hpp"
#include "catch2/trompeloeil.hpp"
// clang-format on

#include <sstream>

#include "mock/MockAlgorithmService.h"
#include "service/event/AlarmExport.h"
#include "util/DateTimeFormat.h"
#include "util/dto/EventMsgTypes.h"

using namespace cosmo;
using trompeloeil::_;

TEST_CASE("Alarm export: behavior CSV escapes quotes inside text fields", "[alarm-export]") {
    test::MockAlgorithmService algorithms;
    REQUIRE_CALL(algorithms, GetAlgorithmName(_)).RETURN("告警\"类型\"");

    MsgEventUnit event;
    event.timestamp    = util::DateTime("2026-09-28 12:00:00").ToTimeStamp() * 1000;
    event.fullPicture  = "/图片\"1\".jpg";
    event.channelName  = "大厅\"东门\",入口";
    event.areaName     = "区域\"A\"\n第二行";
    event.reportStatus = 0;

    std::ostringstream csv;
    WriteCsvRowBehavior(csv, event, "http://localhost", 1, algorithms);

    REQUIRE(csv.str() ==
            "\"1\t\",\"http://localhost/图片\"\"1\"\".jpg\t\",\"告警\"\"类型\"\"\t\","
            "\"大厅\"\"东门\"\",入口\t\",\"区域\"\"A\"\"\n第二行\t\","
            "\"2026-09-28 12:00:00\t\",\"未上传\t\",\"\t\"\n");
}

TEST_CASE("Alarm export: recognition CSV escapes quotes inside text fields", "[alarm-export]") {
    test::MockAlgorithmService algorithms;
    REQUIRE_CALL(algorithms, GetAlgorithmName(_)).RETURN("人脸\"识别\"");

    MsgEventUnit event;
    event.timestamp       = util::DateTime("2026-09-28 12:00:00").ToTimeStamp() * 1000;
    event.detectedPicture = "/抓拍\"1\".jpg";
    event.fullPicture     = "/全景\"2\".jpg";
    event.channelName     = "通道\"东\"";
    event.reportStatus    = 1;
    event.property        = R"json({"recognition":{
        "LibImage":"/底库\"3\".jpg", "matchLibName":"人员\"库\"",
        "matchDegree":0.5, "matchName":"张\"三\",四", "personCode":"ID\"001\""
    }})json";

    std::ostringstream csv;
    WriteCsvRowRecognize(csv, event, "http://localhost", 2, algorithms, true);

    REQUIRE(csv.str() ==
            "\"2\t\",\"人脸\"\"识别\"\"\t\",\"http://localhost/抓拍\"\"1\"\".jpg\t\","
            "\"http://localhost/底库\"\"3\"\".jpg\t\",\"http://localhost/全景\"\"2\"\".jpg\t\","
            "\"人员\"\"库\"\"\t\",\"0.5\t\",\"张\"\"三\"\",四\t\",\"ID\"\"001\"\"\t\","
            "\"通道\"\"东\"\"\t\",\"2026-09-28 12:00:00\t\",\"Uploaded\t\",\"\t\"\n");
}

TEST_CASE("Alarm export: plain and empty text fields retain their CSV representation", "[alarm-export]") {
    test::MockAlgorithmService algorithms;
    REQUIRE_CALL(algorithms, GetAlgorithmName(_)).RETURN("告警");

    MsgEventUnit event;
    event.timestamp    = util::DateTime("2026-09-28 12:00:00").ToTimeStamp() * 1000;
    event.channelName  = "大厅";
    event.reportStatus = 0;

    std::ostringstream csv;
    WriteCsvRowBehavior(csv, event, "http://localhost", 1, algorithms);

    REQUIRE(csv.str() ==
            "\"1\t\",\"\t\",\"告警\t\",\"大厅\t\",\"\t\","
            "\"2026-09-28 12:00:00\t\",\"未上传\t\",\"\t\"\n");
}
