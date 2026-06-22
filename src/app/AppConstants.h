#pragma once

#include <cstdint>

namespace cosmo::app::constants {

// --- Network Ports ---
constexpr uint16_t kDefaultHttpPort      = 8000;
constexpr uint16_t kDefaultWebSocketPort = 9000;

// --- Log Configuration ---
constexpr uint32_t kMaxLogFileCount     = 20;
constexpr uint32_t kMaxLogFileKeepDays  = 30;
constexpr uint32_t kMaxLogFileSizeMb    = 20;
constexpr uint32_t kLogFlushIntervalSec = 1;

}  // namespace cosmo::app::constants
