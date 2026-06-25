/// @file AlarmExport.h
/// @brief Alarm record CSV export utilities and service-layer export orchestration.
#pragma once

#include <ostream>
#include <string>
#include <vector>

namespace cosmo {

struct MsgEventUnit;

namespace service {
    class IAlgorithmQuery;
}  // namespace service

enum class ExportType { Recognize = 1, Behavior, Snapshot, MotorObject, CrowdDensity, LeavePost, WalkDog };

/// Convert a category string ("1"-"7") to ExportType enum.
/// Returns ExportType::Behavior if the category is unknown or empty.
ExportType CategoryToExportType(const std::string& category);

std::string AlarmTarget(int target);
std::ostream& WriteExportCsvHeader(std::ostream& os, ExportType type, bool is_en = false, bool is_cap_orig_id = false);

/// Write a single Recognize-type CSV row.
void WriteCsvRowRecognize(std::ostream& os, const MsgEventUnit& event, const std::string& http_dir,
                          size_t index, service::IAlgorithmQuery& alg_query, bool is_en = false);

/// Write a single Behavior-type CSV row.
void WriteCsvRowBehavior(std::ostream& os, const MsgEventUnit& event, const std::string& http_dir,
                         size_t index, service::IAlgorithmQuery& alg_query, bool is_en = false);

/// Export alarm records to a CSV file and return the web-accessible URL.
/// @param records   Pre-queried event records to export.
/// @param export_type CSV format variant (Recognize, Behavior, etc.).
/// @param host_ip   Device IP address for constructing image URLs.
/// @param alg_query Algorithm query service for resolving algorithm names.
/// @return Web-accessible file URL on success, empty string on failure.
std::string ExportAlarmRecordsToCsv(const std::vector<MsgEventUnit>& records, ExportType export_type,
                                    const std::string& host_ip, service::IAlgorithmQuery& alg_query, const std::string& language = "");

}  // namespace cosmo
