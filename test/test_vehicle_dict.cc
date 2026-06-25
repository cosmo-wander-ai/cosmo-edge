#include "catch_amalgamated.hpp"
/*
 * test_vehicle_dict.cc — VehicleDict unit tests
 *
 * Verifies dict functions return non-empty lists with valid key-value pairs.
 */
#include "util/VehicleDict.h"

using namespace cosmo;

TEST_CASE("VehicleDict: DictVehicleColor returns non-empty list", "[vehicle-dict]") {
    auto colors = DictVehicleColor();
    REQUIRE(!colors.empty());

    SECTION("All entries have non-empty keys") {
        for (const auto& kv : colors) {
            const std::string& keyStr = kv.key;
            REQUIRE(!keyStr.empty());
        }
    }
}

TEST_CASE("VehicleDict: DictVehiclePlateColor returns non-empty list", "[vehicle-dict]") {
    auto colors = DictVehiclePlateColor();
    REQUIRE(!colors.empty());

    SECTION("All entries have non-empty keys") {
        for (const auto& kv : colors) {
            const std::string& keyStr = kv.key;
            REQUIRE(!keyStr.empty());
        }
    }
}

TEST_CASE("VehicleDict: DictVehicleClass returns non-empty list", "[vehicle-dict]") {
    auto classes = DictVehicleClass();
    REQUIRE(!classes.empty());

    SECTION("All entries have non-empty keys") {
        for (const auto& kv : classes) {
            const std::string& keyStr = kv.key;
            REQUIRE(!keyStr.empty());
        }
    }
}

TEST_CASE("VehicleDict: DictVehicleOrientation returns non-empty list", "[vehicle-dict]") {
    auto orientations = DictVehicleOrientation();
    REQUIRE(!orientations.empty());

    SECTION("All entries have non-empty keys") {
        for (const auto& kv : orientations) {
            const std::string& keyStr = kv.key;
            REQUIRE(!keyStr.empty());
        }
    }
}
