#include <filesystem>
#include <fstream>

#include "catch_amalgamated.hpp"
#include "service/management/ManagedDeviceChip.h"
#include "util/UuidUtil.h"

namespace {
struct ChipIdentityFixture {
    std::filesystem::path root =
        std::filesystem::temp_directory_path() / ("cosmo-chip-identity-" + cosmo::util::GenerateUUID());
    std::filesystem::path proc = root / "proc";
    std::filesystem::path sys  = root / "sys";

    ChipIdentityFixture() {
        std::filesystem::create_directories(proc);
        std::filesystem::create_directories(sys);
    }
    ~ChipIdentityFixture() {
        std::error_code error;
        std::filesystem::remove_all(root, error);
    }
    void Write(const std::filesystem::path& path, const std::string& value) {
        std::ofstream file(path, std::ios::binary);
        file.write(value.data(), static_cast<std::streamsize>(value.size()));
        REQUIRE(file.good());
    }
    std::string Detect() const {
        return cosmo::service::detail::ReadSophonManagementChip(proc, sys);
    }
};
}  // namespace

TEST_CASE("Management identifies BM1688 from the hardware model with generic compatible",
          "[management][chip]") {
    ChipIdentityFixture fixture;
    // Some Sophon kernels expose this generic compatible even on BM1688 hardware.
    fixture.Write(fixture.proc / "compatible", std::string("cvitek,cv181x") + '\0');
    REQUIRE(fixture.Detect() == "unknown");
    fixture.Write(fixture.proc / "model", std::string("Sophon. BM1688 ASIC. ARM64.") + '\0');
    REQUIRE(fixture.Detect() == "bm1688");
}

TEST_CASE("Management reads NUL-separated compatible and the sysfs device tree fallback",
          "[management][chip]") {
    ChipIdentityFixture fixture;
    fixture.Write(fixture.sys / "compatible", std::string("vendor,board") + '\0' + "sophgo,bm1688" + '\0');
    REQUIRE(fixture.Detect() == "bm1688");
}

TEST_CASE("Management preserves Sophon chip families and refuses conflicting evidence",
          "[management][chip]") {
    ChipIdentityFixture fixture;
    fixture.Write(fixture.proc / "compatible", "sophgo,CV186X");
    REQUIRE(fixture.Detect() == "cv186x");
    fixture.Write(fixture.proc / "model", "Sophon CV186AH");
    REQUIRE(fixture.Detect() == "cv186x");
    fixture.Write(fixture.sys / "model", "Sophon BM1688 ASIC");
    REQUIRE(fixture.Detect() == "unknown");
}

TEST_CASE("Management keeps missing and unrecognized hardware identities unknown", "[management][chip]") {
    ChipIdentityFixture fixture;
    REQUIRE(fixture.Detect() == "unknown");
    fixture.Write(fixture.proc / "compatible", "cvitek,cv181x");
    fixture.Write(fixture.proc / "model", "SOPHON");
    REQUIRE(fixture.Detect() == "unknown");
    fixture.Write(fixture.proc / "model", "bm16880 cv186unknown");
    REQUIRE(fixture.Detect() == "unknown");
}

TEST_CASE("Management rejects oversized chip identity fields", "[management][chip]") {
    ChipIdentityFixture fixture;
    fixture.Write(fixture.proc / "model", "BM1688 " + std::string(4096, 'x'));
    REQUIRE(fixture.Detect() == "unknown");
}
