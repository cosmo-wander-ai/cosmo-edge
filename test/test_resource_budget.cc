#include <filesystem>
#include <limits>

#include "catch_amalgamated.hpp"
#include "util/ResourceBudget.h"

TEST_CASE("Storage resource budget is derived from the target filesystem", "[resource][storage]") {
    const auto budget =
        cosmo::util::InspectStorageResourceBudget(std::filesystem::temp_directory_path().string(), 0, 0);
    REQUIRE(budget.valid);
    CHECK(budget.capacity_bytes > 0);
    CHECK(budget.available_bytes > 0);
    CHECK(budget.reserve_bytes == 0);
    CHECK(budget.usable_bytes == budget.available_bytes);

    const auto reserved = cosmo::util::InspectStorageResourceBudget(
        std::filesystem::temp_directory_path().string(), budget.available_bytes, 0);
    REQUIRE(reserved.valid);
    CHECK(reserved.usable_bytes == 0);
}

TEST_CASE("Storage resource budget defaults match upload admission policy", "[resource][storage]") {
    CHECK(cosmo::util::kDefaultStorageReserveBytes == 512ULL * 1024 * 1024);
    CHECK(cosmo::util::kDefaultStorageReservePercent == 0);
}

TEST_CASE("Reclaimable storage extends usable budget safely", "[resource][storage]") {
    cosmo::util::StorageResourceBudget budget;
    budget.valid        = true;
    budget.usable_bytes = 100;
    CHECK(cosmo::util::UsableStorageBytesAfterReclaim(budget, 25) == 125);

    budget.usable_bytes = std::numeric_limits<std::uint64_t>::max() - 5;
    CHECK(cosmo::util::UsableStorageBytesAfterReclaim(budget, 10) ==
          std::numeric_limits<std::uint64_t>::max());

    budget.valid = false;
    CHECK(cosmo::util::UsableStorageBytesAfterReclaim(budget, 25) == 0);
}

TEST_CASE("Event media admission preserves reserve and resumes at the retention waterline",
          "[media-admission]") {
    cosmo::util::EventMediaAdmission admission;
    cosmo::util::StorageResourceBudget budget;
    budget.valid           = true;
    budget.available_bytes = 512ULL * 1024 * 1024 - 1;
    CHECK_FALSE(admission.Admit(budget, 1));
    budget.available_bytes = 1024ULL * 1024 * 1024;
    CHECK_FALSE(admission.Admit(budget, 1));
    budget.available_bytes = 2ULL * 1024 * 1024 * 1024;
    CHECK(admission.Admit(budget, 1));
    CHECK_FALSE(admission.Admit(budget, budget.available_bytes));
    CHECK(admission.Admit(budget, budget.available_bytes - 512ULL * 1024 * 1024));
    budget.valid = false;
    CHECK_FALSE(admission.Admit(budget, 1));
}

TEST_CASE("Event media writer leaves invalid targets intact", "[media-admission]") {
    const auto root      = std::filesystem::temp_directory_path().string();
    const auto directory = std::filesystem::path(root) / "cosmo-media-write-directory";
    REQUIRE(std::filesystem::create_directory(directory));
    CHECK_FALSE(cosmo::util::WriteEventMediaFile(root, directory.string(), "metadata"));
    CHECK(std::filesystem::is_directory(directory));
    CHECK(std::filesystem::remove(directory));
    CHECK_FALSE(cosmo::util::AdmitEventMediaWrite(root, std::numeric_limits<std::uint64_t>::max()));
    CHECK_FALSE(cosmo::util::AdmitEventMediaWrite(root + "/missing-volume", 1));
}

TEST_CASE("Event media writer reports ENOSPC delivered by buffered close", "[media-admission]") {
    REQUIRE(std::filesystem::exists("/dev/full"));
    const auto root = std::filesystem::temp_directory_path().string();
    CHECK_FALSE(cosmo::util::WriteEventMediaFile(root, "/dev/full", std::string("small buffered write")));
    const std::uint8_t byte = 42;
    CHECK_FALSE(cosmo::util::WriteEventMediaFile(root, "/dev/full", &byte, 1));
    CHECK(std::filesystem::exists("/dev/full"));
}
