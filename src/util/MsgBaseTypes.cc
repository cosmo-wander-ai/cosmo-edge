// MsgBaseTypes — Base message types — MsgRecvHead, MsgSendHead, geometry types, enums.

#include "MsgBaseTypes.h"

#include <nlohmann/json.hpp>

#include "util/LimitedTypeJson.h"

// Auto-generated JSON serialization
namespace cosmo {
void to_json(nlohmann::json& j, const MsgRecvHead& /*h*/) {
    j = nlohmann::json::object();
}

void from_json(const nlohmann::json& /*j*/, MsgRecvHead& /*h*/) {}

void to_json(nlohmann::json& j, const MsgSendHead& h) {
    if (h.msgSendType == MsgSendType::CWAI) {
        j["resCode"] = h.resCode;
        j["resMsg"]  = h.resMsg;
    }
    if (h.msgSendType == MsgSendType::ChinaMobile) {
        j["resultCode"] = h.resultCode;
        j["resultMsg"]  = h.resultMsg;
    }
}

void from_json(const nlohmann::json& j, MsgSendHead& h) {
    if (j.contains("resCode") && !j["resCode"].is_null()) {
        h.msgSendType = MsgSendType::CWAI;
        j.at("resCode").get_to(h.resCode);
        if (j.contains("resMsg") && !j["resMsg"].is_null())
            j.at("resMsg").get_to(h.resMsg);
    }
    if (j.contains("resultCode") && !j["resultCode"].is_null()) {
        h.msgSendType = MsgSendType::ChinaMobile;
        j.at("resultCode").get_to(h.resultCode);
        if (j.contains("resultMsg") && !j["resultMsg"].is_null())
            j.at("resultMsg").get_to(h.resultMsg);
    }
}

void to_json(nlohmann::json& j, const MsgPoint& p) {
    j = nlohmann::json{{"xRatio", p.x}, {"yRatio", p.y}};
}

void from_json(const nlohmann::json& j, MsgPoint& p) {
    if (j.contains("xRatio") && !j["xRatio"].is_null())
        j.at("xRatio").get_to(p.x);
    if (j.contains("yRatio") && !j["yRatio"].is_null())
        j.at("yRatio").get_to(p.y);
}

void to_json(nlohmann::json& j, const MsgRect& r) {
    j = nlohmann::json{{"xRatio", r.x}, {"yRatio", r.y}, {"wRatio", r.width}, {"hRatio", r.height}};
}

void from_json(const nlohmann::json& j, MsgRect& r) {
    if (j.contains("xRatio") && !j["xRatio"].is_null())
        j.at("xRatio").get_to(r.x);
    if (j.contains("yRatio") && !j["yRatio"].is_null())
        j.at("yRatio").get_to(r.y);
    if (j.contains("wRatio") && !j["wRatio"].is_null())
        j.at("wRatio").get_to(r.width);
    if (j.contains("hRatio") && !j["hRatio"].is_null())
        j.at("hRatio").get_to(r.height);
}

void to_json(nlohmann::json& j, const MsgRectReal& r) {
    j = nlohmann::json{{"x", r.x}, {"y", r.y}, {"width", r.width}, {"height", r.height}};
}

void from_json(const nlohmann::json& j, MsgRectReal& r) {
    j.at("x").get_to(r.x);
    j.at("y").get_to(r.y);
    j.at("width").get_to(r.width);
    j.at("height").get_to(r.height);
}

void from_json(const nlohmann::json& j, MsgResBase& v) {
    if (j.contains("msgCode") && !j["msgCode"].is_null())
        j.at("msgCode").get_to(v.msgCode);
    if (j.contains("msgText") && !j["msgText"].is_null())
        j.at("msgText").get_to(v.msgText);
}

void to_json(nlohmann::json& j, const MsgResBase& v) {
    j["msgCode"] = v.msgCode;
    j["msgText"] = v.msgText;
}

}  // namespace cosmo
