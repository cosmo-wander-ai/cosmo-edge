#pragma once

#include "db/DaoBase.h"

namespace SQLite {
class Database;
}

namespace cosmo::db {
struct PassengerFlowTimePoint {
    uint64_t hour{0};
    std::string time_string;
    std::string camera_id;
    std::string algorithm_code;
    int enter_number{0};
    int leave_number{0};
};

struct PassengerFlowResult {
    size_t total_count{0};
    std::vector<PassengerFlowTimePoint> number_list;
};

enum class PassengerFlowConditionType {
    None  = 0,  // No specific condition
    Hour  = 1,  // Hourly data
    Day   = 2,  // Daily data
    Week  = 3,  // Weekly data
    Month = 4   // Monthly data
};

// Flow record query condition
struct PassengerFlowCondition {
    std::string camera_id;
    std::string algorithm_code;
    PassengerFlowConditionType type{PassengerFlowConditionType::None};
    uint64_t start_hour{0};
    uint64_t end_hour{0};
    int reported{-1};
};

class PassengerFlowDao : public DaoBase {
public:
    explicit PassengerFlowDao(SQLite::Database& db);

    // Create table
    void CreateTable();

    // Query passenger flow
    [[nodiscard]] PassengerFlowResult Query(const PassengerFlowCondition& condition, int max_size = 0,
                                            int order = 1) const;
    // Query original passenger flow
    [[nodiscard]] PassengerFlowResult QueryOrigin(const PassengerFlowCondition& condition, int max_size = 0,
                                                  int order = 1) const;
    // Record passenger flow
    bool AddNumber(const std::string& camera_id, const std::string& alg_id, uint64_t hour, int enter_num,
                   int leave_num);
    // Mark passenger flow as reported
    bool Reported(const std::string& camera_id, const std::string& alg_id, uint64_t hour);
    // Clear passenger flow by date
    bool ResetDateNumber(const std::string& camera_id, const std::string& alg_id, uint64_t date);
};

}  // namespace cosmo::db
