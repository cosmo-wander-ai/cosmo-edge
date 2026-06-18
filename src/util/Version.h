// Version information for the application.

#pragma once

namespace cosmo::util {

constexpr int kVersionMajor = 1;
constexpr int kVersionMinor = 5;
constexpr int kVersionPatch = 0;
constexpr int kVersionBuild = 0;

// Returns the application description string.
constexpr const char* GetProgramDesc() {
    return "Algorithm Analysis Engine";
}

// Returns the full version string, e.g. "Version 1.1.0.0 Mar 27 2026".
[[nodiscard]] const char* GetVersion();

// Returns the abbreviated version string, e.g. "V1.1.0.0".
[[nodiscard]] const char* GetAbbrVersion();

}  // namespace cosmo::util
