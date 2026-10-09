#include <filesystem>
#include <fstream>

#include "catch_amalgamated.hpp"
#include "mock/MockDeviceInfoService.h"
#include "service/management/ManagementService.h"
#include "service/onvif/impl/OnvifServiceImpl.h"
#include "service/task/impl/ScheduleServiceImpl.h"
#include "support/ScopedPathOverride.h"
#include "support/ScopedServiceOverride.h"
#include "util/UuidUtil.h"

using namespace cosmo;
using namespace cosmo::service;
using Json = nlohmann::json;

TEST_CASE("Management native adapter persists versioned schedules and detects local drift",
          "[management][native]") {
    auto root = std::filesystem::temp_directory_path() / ("cosmo-native-management-" + util::GenerateUUID());
    std::filesystem::create_directories(root);
    test::ScopedPathOverride paths(root.string(), root.string());
    test::MockDeviceInfoService hardware;
    ALLOW_CALL(hardware, GetDevSn()).RETURN("native-test-device");
    test::ScopedServiceOverride<IDeviceHardware> hw(hardware);
    {
        ScheduleServiceImpl schedules;
        test::ScopedServiceOverride<IScheduleService> schedule_override(schedules);
        ManagementService service(root / "management", MakeNativeManagedResources());
        RequestDispatchContext ctx;
        ctx.principal     = "owner";
        ctx.http_method   = "POST";
        auto capabilities = service.Handle("capabilities", ctx, "{}");
#ifdef COSMO_NN_USE_CPU_BACKEND
        REQUIRE(capabilities["chip"] == "x86");
        REQUIRE(capabilities["runtimes"] == Json::array({"onnx"}));
#endif
        Json request = {{"operationId", "schedule-create"},
                        {"platformId", "platform-a"},
                        {"externalId", "schedule-a"},
                        {"versionId", "v1"},
                        {"hash", std::string(64, 'a')},
                        {"incarnation", capabilities["incarnation"]},
                        {"expiresAt", 4102444800LL},
                        {"action", "apply"},
                        {"kind", "schedule"},
                        {"name", "Office"},
                        {"config", {{"periods", {{{"week", 1}, {"begin", "08:00"}, {"end", "18:00"}}}}}},
                        {"files", Json::array()},
                        {"references", Json::array()},
                        {"activationPolicy", "apply"}};
        REQUIRE(service.Handle("applyoperation", ctx, request.dump())["state"] == "SUCCEEDED");
        auto inventory_request = Json{{"platformId", "platform-a"}, {"resourceIds", Json::array()}};
        auto rows = service.Handle("resourceinventory", ctx, inventory_request.dump())["resources"];
        REQUIRE(rows.size() == 1);
        REQUIRE(rows[0]["state"] == "READY");
        auto id = rows[0]["localId"].get<std::string>();
        ScheduleServiceImpl reloaded;
        REQUIRE(reloaded.Exist(id));
        MsgScheduleTemplate local_edit;
        local_edit.scheduleId   = id;
        local_edit.scheduleName = "Locally changed";
        REQUIRE(schedules.Update(local_edit) == util::ErrorEnum::Success);
        REQUIRE(service.Handle("resourceinventory", ctx, inventory_request.dump())["resources"][0]["state"] !=
                "READY");

        request["versionId"]                     = "v2";
        request["operationId"]                   = "invalid-time";
        request["config"]["periods"][0]["begin"] = "99:00";
        REQUIRE(service.Handle("applyoperation", ctx, request.dump())["state"] == "FAILED");
    }
    std::error_code error;
    std::filesystem::remove_all(root, error);
}

TEST_CASE("Management ONVIF source IDs survive retry and isolate channel versions", "[management][onvif]") {
    auto root = std::filesystem::temp_directory_path() / ("cosmo-managed-onvif-" + util::GenerateUUID());
    std::filesystem::create_directories(root);
    test::ScopedPathOverride paths(root.string(), root.string());
    const auto first  = "onvif://managed-" + std::string(32, 'a');
    const auto second = "onvif://managed-" + std::string(32, 'b');
    Json request      = {{"endpoint", "http://127.0.0.1:18080/onvif/device_service"},
                         {"username", "test-user"},
                         {"password", "test-password"},
                         {"profileToken", "profile-a"}};
    {
        OnvifServiceImpl service;
        service.Init();
        REQUIRE(service.SaveManaged(request, first) == first);
        REQUIRE(service.SaveManaged(request, first) == first);
        REQUIRE(service.SaveManaged(request, second) == second);
        REQUIRE(service.Describe(first)["profileToken"] == "profile-a");
        REQUIRE_FALSE(service.Describe(first).contains("password"));
        REQUIRE_THROWS(service.SaveManaged(request, "onvif://../../outside"));
    }
    {
        OnvifServiceImpl service;
        service.Init();
        REQUIRE(service.Describe(first)["profileToken"] == "profile-a");
        service.Remove(first);
        REQUIRE(service.Describe(second)["profileToken"] == "profile-a");
    }
    std::error_code error;
    std::filesystem::remove_all(root, error);
}
