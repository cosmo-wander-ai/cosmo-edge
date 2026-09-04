#pragma once

#include <exception>
#include <string>

#include "util/PathUtil.h"

namespace cosmo::test {

/// Restores both process-wide path roots when the current scope exits.
class ScopedPathOverride final {
public:
    ScopedPathOverride(const std::string& data_root, const std::string& app_root)
        : previous_data_root_(path::GetBaseDir()), previous_app_root_(path::GetAppBaseDir()) {
        path::OverrideRootPathForTest(data_root, app_root);
    }

    ~ScopedPathOverride() noexcept {
        try {
            path::OverrideRootPathForTest(previous_data_root_, previous_app_root_);
        } catch (...) {
            std::terminate();
        }
    }

    ScopedPathOverride(const ScopedPathOverride&)            = delete;
    ScopedPathOverride& operator=(const ScopedPathOverride&) = delete;
    ScopedPathOverride(ScopedPathOverride&&)                 = delete;
    ScopedPathOverride& operator=(ScopedPathOverride&&)      = delete;

private:
    std::string previous_data_root_;
    std::string previous_app_root_;
};

}  // namespace cosmo::test
