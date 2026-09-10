#include <algorithm>

#include "catch_amalgamated.hpp"
#include "platform/NetworkManagerConfig.h"

namespace {
using Args = std::vector<std::string>;
struct NmFixture {
    std::vector<Args> commands;
    std::string active = "test-uuid";
    std::string fail;
    bool profile_exists = false;
    int Run(const Args& argv, std::string& out) {
        commands.push_back(argv);
        auto has = [&](const std::string& value) {
            return std::find(argv.begin(), argv.end(), value) != argv.end();
        };
        if (!fail.empty() && has(fail)) {
            out = "simulated failure";
            return 1;
        }
        if (has("GENERAL.CON-UUID"))
            out = active + "\n";
        if (has("connection.uuid")) {
            if (!profile_exists)
                return 1;
            out = "new-uuid\n";
        }
        if (has("add"))
            profile_exists = true;
        return 0;
    }
    bool Has(const std::string& value) const {
        return std::any_of(commands.begin(), commands.end(),
                           [&](const Args& a) { return std::find(a.begin(), a.end(), value) != a.end(); });
    }
    Args Modification() const {
        for (const auto& a : commands)
            if (std::find(a.begin(), a.end(), "modify") != a.end())
                return a;
        return {};
    }
};
std::string Property(const Args& args, const std::string& key) {
    auto it = std::find(args.begin(), args.end(), key);
    REQUIRE(it != args.end());
    REQUIRE(++it != args.end());
    return *it;
}
}  // namespace

TEST_CASE("NetworkManager persists and reapplies the factory IP on the active profile", "[network-manager]") {
    NmFixture fixture;
    cosmo::platform::internal::NetworkManagerConfig nm(
        [&](const Args& a, std::string& o) { return fixture.Run(a, o); });
    REQUIRE(nm.Apply("eth0", false, "192.168.100.1/24", "", "114.114.114.114", true));
    auto modify = fixture.Modification();
    REQUIRE(Property(modify, "uuid") == "test-uuid");
    REQUIRE(Property(modify, "ipv4.method") == "manual");
    REQUIRE(Property(modify, "ipv4.addresses") == "192.168.100.1/24");
    REQUIRE(Property(modify, "ipv4.gateway").empty());
    REQUIRE(Property(modify, "ipv4.dns") == "114.114.114.114");
    REQUIRE(fixture.Has("reapply"));
    REQUIRE_FALSE(fixture.Has("up"));
    REQUIRE_FALSE(fixture.Has("add"));
}
TEST_CASE("NetworkManager DHCP clears the previous static address", "[network-manager]") {
    NmFixture f;
    cosmo::platform::internal::NetworkManagerConfig nm(
        [&](const Args& a, std::string& o) { return f.Run(a, o); });
    REQUIRE(nm.Apply("eth0", true, "192.168.100.1/24", "192.168.100.254", "", true));
    auto modify = f.Modification();
    REQUIRE(Property(modify, "ipv4.method") == "auto");
    REQUIRE(Property(modify, "ipv4.addresses").empty());
    REQUIRE(Property(modify, "ipv4.gateway").empty());
    REQUIRE(Property(modify, "ipv4.ignore-auto-dns") == "no");
}
TEST_CASE("NetworkManager failures propagate and activation fallback is bounded", "[network-manager]") {
    NmFixture f;
    cosmo::platform::internal::NetworkManagerConfig nm(
        [&](const Args& a, std::string& o) { return f.Run(a, o); });
    SECTION("failed persistent write does not activate") {
        f.fail = "modify";
        REQUIRE_FALSE(nm.Apply("eth0", false, "192.168.100.1/24", "", "", true));
        REQUIRE_FALSE(f.Has("reapply"));
        REQUIRE_FALSE(f.Has("up"));
    }
    SECTION("unsupported reapply uses the same profile") {
        f.fail = "reapply";
        REQUIRE(nm.Apply("eth0", false, "192.168.100.1/24", "", "", true));
        REQUIRE(f.Has("up"));
    }
    SECTION("unavailable manager does not create a connection") {
        f.fail = "GENERAL.CON-UUID";
        REQUIRE_FALSE(nm.Apply("eth0", false, "192.168.100.1/24", "", "", true));
        REQUIRE_FALSE(f.Has("modify"));
        REQUIRE_FALSE(f.Has("add"));
    }
}
TEST_CASE("NetworkManager creates a profile only for a disconnected interface", "[network-manager]") {
    NmFixture f;
    f.active = "--";
    cosmo::platform::internal::NetworkManagerConfig nm(
        [&](const Args& a, std::string& o) { return f.Run(a, o); });
    REQUIRE(nm.Apply("eth1", false, "192.168.1.18/24", "", "", false));
    REQUIRE(f.Has("add"));
    REQUIRE(f.Has("up"));
    REQUIRE(Property(f.Modification(), "ipv4.never-default") == "yes");
    REQUIRE(Property(f.Modification(), "uuid") == "new-uuid");
}
TEST_CASE("NetworkManager DNS updates the persistent profile", "[network-manager]") {
    NmFixture f;
    cosmo::platform::internal::NetworkManagerConfig nm(
        [&](const Args& a, std::string& o) { return f.Run(a, o); });
    REQUIRE(nm.SetDns("eth0", "1.1.1.1,8.8.8.8"));
    REQUIRE(Property(f.Modification(), "ipv4.dns") == "1.1.1.1,8.8.8.8");
    REQUIRE(f.Has("reapply"));
}
