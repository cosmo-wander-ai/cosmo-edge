/// @file AlarmExport.cc
/// @brief Alarm record CSV export utilities and service-layer export orchestration.
#include "service/event/AlarmExport.h"

#include <filesystem>
#include <fstream>
#include <map>
#include <ostream>

#include "service/algorithm/IAlgorithmQuery.h"
#include "util/DateTimeFormat.h"
#include "util/JsonStructUtil.h"
#include "util/Log.h"
#include "util/PathUtil.h"
#include "util/TimeUtil.h"
#include "util/UuidUtil.h"
#include "util/dto/ClientMsgEvent.h"
#include "util/dto/EventMsgTypes.h"

namespace cosmo {

// ── Category conversion ─────────────────────────────────────────────

ExportType CategoryToExportType(const std::string& category) {
    static const std::map<std::string, ExportType> kMapping = {
        {"1", ExportType::Recognize},   {"2", ExportType::Behavior},     {"3", ExportType::Snapshot},
        {"4", ExportType::MotorObject}, {"5", ExportType::CrowdDensity}, {"6", ExportType::LeavePost},
        {"7", ExportType::WalkDog}};
    auto it = kMapping.find(category);
    if (it != kMapping.end()) {
        return it->second;
    }
    LOG_WARN("Unknown category: {}, using default ExportType::Behavior", category);
    return ExportType::Behavior;
}

// ── CSV header ──────────────────────────────────────────────────────

std::ostream& WriteExportCsvHeader(std::ostream& os, ExportType type, bool is_en, bool /*isCapOrigId*/) {
    unsigned char utf8_bom[] = {0xEF, 0xBB, 0xBF, 0};
    os << utf8_bom;

    switch (type) {
        case ExportType::Behavior:
            if (is_en) {
                os << "\"No.\",";
                os << "\"Full Image\",\"Alarm Type\",\"Channel Name\",\"Area Name\",\"Alarm Time\",\"Status\"\n";
            } else {
                os << "\"序号\",";
                os << "\"全景照\",\"告警类型\",\"通道名称\",\"区域名称\",\"告警时间\",\"状态\"\n";
            }
            break;
        case ExportType::Recognize:
            if (is_en) {
                os << "\"No.\",\"Event Type\",\"Face Capture\",\"Face Base Image\",\"Full Image\",\"Face Library\",\"Similarity\","
                      "\"Name\",\"ID\",\"Channel Name\",\"Capture Time\",\"Status\"\n";
            } else {
                os << "\"序号\",\"事件类型\",\"人脸抓拍照\",\"人脸底库照\",\"全景照\",\"所属脸库\",\"相似度\","
                      "\"人员姓名\",\"人员编号\",\"通道名称\",\"抓拍时间\",\"状态\"\n";
            }
            break;
        default:
            break;
    }

    return os;
}

// ── AlarmTarget ─────────────────────────────────────────────────────

std::string AlarmTarget(int target) {
    std::string target_str;
    switch (target) {
        case 1:
            target_str = "行人";
            break;
        case 2:
            target_str = "非机动车";
            break;
        case 3:
            target_str = "机动车";
            break;
        default:
            break;
    }

    return target_str;
}

// ── CSV row writers ─────────────────────────────────────────────────

namespace {

    constexpr const char* kReportedText   = "已上传";
    constexpr const char* kUnreportedText = "未上传";

    const char* ReportStatusText(int status, bool is_en) {
        if (is_en) {
            return status ? "Uploaded" : "Not Uploaded";
        }
        return status ? kReportedText : kUnreportedText;
    }

    std::string FormatTimestamp(int64_t timestamp) {
        auto dt = util::DateTime(timestamp / 1000);
        return dt.Date().ToYMD() + " " + dt.Time().ToHMS();
    }

}  // namespace

void WriteCsvRowRecognize(std::ostream& os, const MsgEventUnit& el, const std::string& http_dir, size_t index,
                          service::IAlgorithmQuery& alg_query, bool is_en) {
    CMsgOnEventsProperty para;
    para.type = OnEventsPropertyType::Face;
    if (!el.property.empty()) {
        if (!util::DecodeJson(el.property, para)) {
            LOG_WARN("Failed to decode property JSON for event {}", el.id);
            return;
        }
    }

    auto time_string = FormatTimestamp(el.timestamp);
    auto alg_name    = alg_query.GetAlgorithmName(el.algorithmCode);
    auto detected    = el.detectedPicture.empty() ? "" : (http_dir + el.detectedPicture);
    auto lib_image   = para.recognition.LibImage.empty() ? "" : (http_dir + para.recognition.LibImage);
    auto full_pic    = el.fullPicture.empty() ? "" : (http_dir + el.fullPicture);

    os << "\"" << index << "\t\",\"" << alg_name << "\t\",\"" << detected << "\t\",\"" << lib_image
       << "\t\",\"" << full_pic << "\t\",\"" << para.recognition.matchLibName << "\t\",\"";

    if (para.recognition.matchDegree != -1.0f) {
        os << para.recognition.matchDegree;
    }

    os << "\t\",\"" << para.recognition.matchName << "\t\",\"" << para.recognition.personCode << "\t\",\""
       << el.channelName << "\t\",\"" << time_string << "\t\",\"" << ReportStatusText(el.reportStatus, is_en)
       << "\t\",\""
       << "\t\"\n";
}

void WriteCsvRowBehavior(std::ostream& os, const MsgEventUnit& el, const std::string& http_dir, size_t index,
                         service::IAlgorithmQuery& alg_query, bool is_en) {
    auto time_string = FormatTimestamp(el.timestamp);
    auto full_pic    = el.fullPicture.empty() ? "" : (http_dir + el.fullPicture);
    auto alg_name    = alg_query.GetAlgorithmName(el.algorithmCode);

    os << "\"" << index << "\t\",\"" << full_pic << "\t\",\"" << alg_name << "\t\",\"" << el.channelName
       << "\t\",\"" << el.areaName << "\t\",\"" << time_string << "\t\",\""
       << ReportStatusText(el.reportStatus, is_en) << "\t\",\""
       << "\t\"\n";
}

// ── Full CSV export orchestration ───────────────────────────────────

std::string ExportAlarmRecordsToCsv(const std::vector<MsgEventUnit>& records, ExportType export_type,
                                    const std::string& host_ip, service::IAlgorithmQuery& alg_query, const std::string& language) {
    auto date = util::DateTime(util::GetMilliseconds() / 1000);
    auto csv_name =
        date.Date().ToYMD() + "_" + date.Time().ToHMS() + "_" + util::GenerateUUID().substr(0, 4) + ".csv";
    auto timestamp       = util::GetMilliseconds();
    auto local_dir       = cosmo::path::GetWebLocalPath(timestamp);
    auto full_path       = (std::filesystem::path(local_dir) / csv_name).string();
    auto web_access_dir  = cosmo::path::GetWebAcessPath(timestamp);
    auto web_access_path = (std::filesystem::path(web_access_dir) / csv_name).string();

    std::ofstream out_file(full_path);
    if (!out_file.is_open()) {
        LOG_ERRO("{} open failed.", full_path);
        return {};
    }

    bool is_en = (language == "en-US" || language == "en_US");
    WriteExportCsvHeader(out_file, export_type, is_en);
    std::string http_dir = "http://" + host_ip;

    if (records.empty()) {
        LOG_WARN("{}", "The Export Alarm Number Is Zero");
    }

    size_t index = 0;
    for (const auto& el : records) {
        ++index;
        if (ExportType::Recognize == export_type) {
            WriteCsvRowRecognize(out_file, el, http_dir, index, alg_query, is_en);
        } else if (ExportType::Behavior == export_type) {
            WriteCsvRowBehavior(out_file, el, http_dir, index, alg_query, is_en);
        }
    }

    return web_access_path;
}

}  // namespace cosmo