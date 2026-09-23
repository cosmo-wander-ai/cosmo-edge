#include <chrono>
#include <filesystem>
#include <fstream>
#include <map>

#include "api/ApiRouter.h"
#include "catch_amalgamated.hpp"
#include "service/management/ManagementService.h"
#include "support/ApiRouterTestDependencies.h"
#include "support/ScopedServiceOverride.h"
#include "util/UuidUtil.h"

using namespace cosmo;
using namespace cosmo::service;
using Json   = nlohmann::json;
namespace fs = std::filesystem;

namespace {
struct NativeState {
    std::map<std::string, Json> resources;
    int creates{0};
    bool interrupt_after_effect{false};
    bool fail{false};
    bool bad_config{false};
    std::vector<std::string> transitions;
};
class TestResources final : public IManagedResources {
public:
    explicit TestResources(std::shared_ptr<NativeState> state) : state_(std::move(state)) {}
    Json DeviceFacts() override {
        return {{"sn", "test-device"}, {"chip", "x86"}, {"runtimes", {"onnx"}}};
    }
    bool Exists(const std::string&, const std::string& id) override {
        return state_->resources.count(id);
    }
    void Apply(Json& resource, const fs::path&) override {
        if (state_->fail)
            throw ManagementError("NATIVE_VALIDATION_FAILED");
        if (state_->bad_config)
            (void)Json::object().at("missing").get<std::string>();
        auto id = resource["localId"].get<std::string>();
        if (!state_->resources.count(id))
            ++state_->creates;
        state_->resources[id] = {
            {"state", "READY"}, {"runtimeState", "stopped"}, {"fingerprint", "native-original"}};
        if (state_->interrupt_after_effect) {
            state_->interrupt_after_effect = false;
            throw std::runtime_error("simulated lost process after native write");
        }
    }
    void Remove(const Json& resource) override {
        state_->resources.erase(resource["localId"].get<std::string>());
    }
    Json Inspect(const Json& resource) override {
        auto it = state_->resources.find(resource["localId"].get<std::string>());
        return it == state_->resources.end() ? Json{{"state", "MISSING"}} : it->second;
    }
    void Activate(const Json& resource, bool enabled) override {
        auto id = resource["localId"].get<std::string>();
        state_->transitions.push_back(id + (enabled ? ":on" : ":off"));
        state_->resources[id]["runtimeState"] = enabled ? "running" : "stopped";
    }

private:
    std::shared_ptr<NativeState> state_;
};
struct Fixture {
    fs::path root = fs::temp_directory_path() / ("cosmo-management-" + util::GenerateUUID());
    std::shared_ptr<NativeState> native = std::make_shared<NativeState>();
    std::unique_ptr<ManagementService> service;
    RequestDispatchContext context;
    Json facts;
    Fixture() {
        context.principal   = "account-a";
        context.http_method = "POST";
        Restart();
        facts = Call("capabilities");
    }
    ~Fixture() {
        service.reset();
        std::error_code error;
        fs::remove_all(root, error);
    }
    void Restart() {
        service = std::make_unique<ManagementService>(root, std::make_unique<TestResources>(native));
    }
    Json Call(const std::string& method, const Json& body = Json::object()) {
        return service->Handle(method, context, body.dump());
    }
    Json Request(std::string external = "channel-a", std::string version = "v1",
                 std::string kind = "channel") {
        return {{"operationId", external + "-" + version},
                {"platformId", "platform-a"},
                {"externalId", external},
                {"versionId", version},
                {"hash", std::string(64, 'a')},
                {"incarnation", facts["incarnation"]},
                {"expiresAt", 4102444800LL},
                {"action", "apply"},
                {"kind", kind},
                {"name", external},
                {"config", Json::object()},
                {"files", Json::array()},
                {"references", Json::array()},
                {"activationPolicy", "prepare"}};
    }
    Json Rows() {
        return Call("resourceinventory",
                    {{"platformId", "platform-a"}, {"resourceIds", Json::array()}})["resources"];
    }
};
}  // namespace

TEST_CASE("Management authenticates transports and preserves device incarnation", "[management]") {
    Fixture f;
    auto before = f.facts["incarnation"];
    f.Restart();
    REQUIRE(f.Call("capabilities")["incarnation"] == before);
    SECTION("MQTT cannot claim HTTP ownership") {
        f.context.transport = RequestTransport::kMqtt;
        REQUIRE_THROWS_AS(f.Call("capabilities"), ManagementError);
    }
    SECTION("empty principal") {
        f.context.principal.clear();
        REQUIRE_THROWS_AS(f.Call("capabilities"), ManagementError);
    }
    SECTION("HTTP method is enforced") {
        f.context.http_method = "GET";
        REQUIRE_THROWS_AS(f.Call("capabilities"), ManagementError);
    }
    SECTION("lost management data changes generation") {
        f.service.reset();
        fs::remove_all(f.root);
        f.Restart();
        REQUIRE(f.Call("capabilities")["incarnation"] != before);
    }
}

TEST_CASE("Management reconciles a lost result without duplicating native resources", "[management]") {
    Fixture f;
    auto request                     = f.Request();
    f.native->interrupt_after_effect = true;
    REQUIRE_THROWS_AS(f.Call("applyoperation", request), std::runtime_error);
    REQUIRE(f.native->creates == 1);
    f.Restart();
    REQUIRE(f.Call("operationstatus", {{"operationId", request["operationId"]}})["state"] == "SUCCEEDED");
    REQUIRE(f.native->creates == 1);
    REQUIRE(f.Rows().size() == 1);
    REQUIRE(f.Rows()[0]["state"] == "READY");
    REQUIRE(f.Call("applyoperation", request)["state"] == "SUCCEEDED");
    request["name"] = "changed";
    REQUIRE_THROWS_AS(f.Call("applyoperation", request), ManagementError);
    REQUIRE(f.native->creates == 1);
}

TEST_CASE("Management versions, dependencies and native readback are authoritative", "[management]") {
    Fixture f;
    auto first = f.Request();
    REQUIRE(f.Call("applyoperation", first)["state"] == "SUCCEEDED");
    auto second = f.Request("channel-a", "v2");
    REQUIRE(f.Call("applyoperation", second)["state"] == "SUCCEEDED");
    auto rows = f.Rows();
    REQUIRE(rows.size() == 2);
    REQUIRE(rows[0]["localId"] != rows[1]["localId"]);
    SECTION("ready is not inferred from the receipt") {
        f.native->resources.erase(rows[0]["localId"].get<std::string>());
        REQUIRE(f.Rows()[0]["state"] == "MISSING");
    }
    SECTION("immutable version cannot be changed with a new operation ID") {
        first["operationId"] = "another";
        first["config"]      = {{"url", "changed"}};
        REQUIRE_THROWS_AS(f.Call("applyoperation", first), ManagementError);
    }
    SECTION("cross-platform writes, stale incarnation and expired requests are rejected") {
        second["operationId"] = "different";
        second["platformId"]  = "platform-b";
        REQUIRE_THROWS_AS(f.Call("applyoperation", second), ManagementError);
        second["platformId"]  = "platform-a";
        second["incarnation"] = "old";
        REQUIRE_THROWS_AS(f.Call("applyoperation", second), ManagementError);
        second["incarnation"] = f.facts["incarnation"];
        second["expiresAt"]   = 1;
        REQUIRE_THROWS_AS(f.Call("applyoperation", second), ManagementError);
    }
    SECTION("shared references prevent cascading removal") {
        auto child = f.Request("task-a", "v1", "task");
        child["references"].push_back({{"kind", "channel"},
                                       {"externalId", "channel-a"},
                                       {"versionId", "v1"},
                                       {"localId", rows[0]["localId"]}});
        REQUIRE(f.Call("applyoperation", child)["state"] == "SUCCEEDED");
        first["operationId"] = "remove";
        first["action"]      = "remove";
        REQUIRE_THROWS_AS(f.Call("applyoperation", first), ManagementError);
        REQUIRE(f.Rows().size() == 3);
    }
}

TEST_CASE("Management prepares stopped task versions and switches one logical task", "[management]") {
    Fixture f;
    auto first  = f.Request("task-a", "v1", "task");
    auto second = f.Request("task-a", "v2", "task");
    REQUIRE(f.Call("applyoperation", first)["state"] == "SUCCEEDED");
    REQUIRE(f.Rows()[0]["runtimeState"] == "stopped");
    auto activate = Json{{"operationId", "activate-1"},
                         {"externalId", "task-a"},
                         {"versionId", "v1"},
                         {"incarnation", f.facts["incarnation"]},
                         {"enabled", true}};
    REQUIRE(f.Call("taskactivate", activate)["state"] == "SUCCEEDED");
    REQUIRE(f.Call("applyoperation", second)["state"] == "SUCCEEDED");
    REQUIRE(f.Rows()[0]["runtimeState"] == "running");
    REQUIRE(f.Rows()[1]["runtimeState"] == "stopped");
    activate["operationId"] = "activate-2";
    activate["versionId"]   = "v2";
    REQUIRE(f.Call("taskactivate", activate)["state"] == "SUCCEEDED");
    auto rows = f.Rows();
    REQUIRE(rows[0]["runtimeState"] == "stopped");
    REQUIRE(rows[1]["runtimeState"] == "running");
    REQUIRE(f.native->transitions[f.native->transitions.size() - 2] ==
            rows[0]["localId"].get<std::string>() + ":off");
    REQUIRE(f.native->transitions.back() == rows[1]["localId"].get<std::string>() + ":on");
    activate["operationId"] = "disable";
    activate["enabled"]     = false;
    REQUIRE(f.Call("taskactivate", activate)["state"] == "SUCCEEDED");
    for (auto& row : f.Rows())
        REQUIRE(row["runtimeState"] == "stopped");
}

TEST_CASE("Management persists failure without claiming READY and can retry a new operation",
          "[management]") {
    Fixture f;
    auto request   = f.Request();
    f.native->fail = true;
    REQUIRE(f.Call("applyoperation", request)["state"] == "FAILED");
    f.Restart();
    REQUIRE(f.Call("operationstatus", {{"operationId", request["operationId"]}})["state"] == "FAILED");
    REQUIRE(f.Rows()[0]["state"] == "FAILED");
    request["operationId"] = "retry";
    f.native->fail         = false;
    REQUIRE(f.Call("applyoperation", request)["state"] == "SUCCEEDED");
    REQUIRE(f.native->creates == 1);
}

TEST_CASE("Management file transfer resumes, rejects corrupt retries and deduplicates by hash",
          "[management]") {
    Fixture f;
    const std::string hash  = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
    Json request            = {{"hash", hash}, {"size", 3}, {"name", "../../ignored.bin"}};
    auto prepared           = f.Call("prepareresource", request);
    f.context.upload_id     = prepared["uploadId"];
    f.context.upload_offset = "0";
    f.context.http_method   = "PUT";
    REQUIRE(f.service->Handle("uploadchunk", f.context, "a")["offset"] == 1);
    REQUIRE(f.service->Handle("uploadchunk", f.context, "a")["offset"] == 1);
    REQUIRE_THROWS_AS(f.service->Handle("uploadchunk", f.context, "x"), ManagementError);
    f.Restart();
    f.context.http_method = "POST";
    REQUIRE(f.Call("prepareresource", request)["offset"] == 1);
    f.context.http_method   = "PUT";
    f.context.upload_offset = "1";
    f.context.principal     = "different-account";
    REQUIRE_THROWS_AS(f.service->Handle("uploadchunk", f.context, "bc"), ManagementError);
    f.context.principal = "account-a";
    REQUIRE(f.service->Handle("uploadchunk", f.context, "bc")["hash"] == hash);
    REQUIRE(f.service->Handle("uploadchunk", f.context, "bc")["hash"] == hash);
    f.context.http_method = "POST";
    REQUIRE(f.Call("prepareresource", request)["complete"] == true);
    REQUIRE(fs::file_size(f.root / "blobs" / hash) == 3);
    REQUIRE(!fs::exists(f.root.parent_path() / "ignored.bin"));
    auto resource     = f.Request("model-a", "v1", "model");
    resource["files"] = Json::array({{{"hash", hash}, {"name", "model.onnx"}, {"size", 3}}});
    REQUIRE(f.Call("applyoperation", resource)["state"] == "SUCCEEDED");
    std::ofstream(f.root / "blobs" / hash, std::ios::binary | std::ios::trunc) << "bad";
    resource["operationId"] = "corrupt-blob";
    resource["versionId"]   = "v2";
    REQUIRE_THROWS_AS(f.Call("applyoperation", resource), ManagementError);
}

TEST_CASE("Management upload validates size, offsets, final digest and slot quota", "[management]") {
    Fixture f;
    auto prepare = [&](char c, std::int64_t size) {
        return f.Call("prepareresource", {{"hash", std::string(64, c)}, {"size", size}, {"name", "file"}});
    };
    REQUIRE_THROWS_AS(prepare('a', -1), ManagementError);
    REQUIRE_THROWS_AS(prepare('a', 5LL * 1024 * 1024 * 1024 + 1), ManagementError);
    auto session            = prepare('a', 3);
    f.context.upload_id     = session["uploadId"];
    f.context.http_method   = "PUT";
    f.context.upload_offset = "-1";
    REQUIRE_THROWS_AS(f.service->Handle("uploadchunk", f.context, "abc"), ManagementError);
    f.context.upload_offset = "0";
    REQUIRE_THROWS_AS(f.service->Handle("uploadchunk", f.context, "abc"), ManagementError);
    REQUIRE(!fs::exists(f.root / "blobs" / std::string(64, 'a')));
    f.context.http_method = "POST";
    for (char c : std::string("01234567"))
        REQUIRE(prepare(c, 1).contains("uploadId"));
    REQUIRE_THROWS_AS(prepare('8', 1), ManagementError);
}

TEST_CASE("Management invalid native configuration becomes a terminal failure", "[management]") {
    Fixture f;
    f.native->bad_config = true;
    REQUIRE(f.Call("applyoperation", f.Request())["state"] == "FAILED");
    f.native->bad_config = false;
    REQUIRE(f.Call("applyoperation", f.Request("other"))["state"] == "SUCCEEDED");
}
TEST_CASE("Management fences new operations until an interrupted intent is reconciled", "[management]") {
    Fixture f;
    auto first                       = f.Request();
    f.native->interrupt_after_effect = true;
    REQUIRE_THROWS(f.Call("applyoperation", first));
    REQUIRE_THROWS_AS(f.Call("applyoperation", f.Request("other")), ManagementError);
    REQUIRE(f.Call("operationstatus", {{"operationId", first["operationId"]}})["state"] == "SUCCEEDED");
    REQUIRE(f.Call("applyoperation", f.Request("other"))["state"] == "SUCCEEDED");
}

TEST_CASE("Management API routes reject anonymous HTTP and forged MQTT principals",
          "[management][ApiRouter]") {
    Fixture f;
    test::ApiRouterTestDependencies dependencies;
    test::ScopedServiceOverride<IManagementService> service_override(*f.service);
    ApiRouter router(MessageFromType::MessageFromHttp);
    for (const char* endpoint : {"Capabilities", "ResourceInventory", "PrepareResource", "UploadChunk",
                                 "OperationStatus", "ApplyOperation", "TaskActivate"}) {
        REQUIRE(router.SupportsRoute(std::string("/gtw/cwai/Management/") + endpoint));
    }
    RequestDispatchContext context;
    context.uri         = "/gtw/cwai/Management/Capabilities";
    context.http_method = "POST";
    context.principal   = "forged-principal";
    ALLOW_CALL(dependencies.authSvc, IsValidToken("")).RETURN(false);
    std::string response;
    REQUIRE_FALSE(router.DispatchRequest(context, "{}", response));
    REQUIRE(Json::parse(response)["resCode"] == 0);

    context.credential = "trusted-session";
    ALLOW_CALL(dependencies.authSvc, IsValidToken("trusted-session")).RETURN(true);
    REQUIRE(router.DispatchRequest(context, "{}", response));
    REQUIRE(Json::parse(response)["resCode"] == 1);
    REQUIRE(Json::parse(response)["resData"]["managementProtocol"] == 1);

    ApiRouter mqtt(MessageFromType::MessageFromMqtt);
    context.transport = RequestTransport::kMqtt;
    context.principal = "forged-principal";
    REQUIRE(mqtt.DispatchRequest(context, "{}", response));
    REQUIRE(Json::parse(response)["resCode"] == 0);
}

TEST_CASE("Management rejects native drift and propagates missing transitive dependencies", "[management]") {
    Fixture f;
    auto model = f.Request("model", "v1", "model");
    REQUIRE(f.Call("applyoperation", model)["state"] == "SUCCEEDED");
    auto model_id       = f.Rows()[0]["localId"];
    auto scene          = f.Request("scene", "v1", "scene");
    scene["references"] = Json::array(
        {{{"kind", "model"}, {"externalId", "model"}, {"versionId", "v1"}, {"localId", model_id}}});
    REQUIRE(f.Call("applyoperation", scene)["state"] == "SUCCEEDED");
    Json scene_id;
    for (auto row : f.Rows())
        if (row["kind"] == "scene")
            scene_id = row["localId"];
    auto task          = f.Request("task", "v1", "task");
    task["references"] = Json::array(
        {{{"kind", "scene"}, {"externalId", "scene"}, {"versionId", "v1"}, {"localId", scene_id}}});
    REQUIRE(f.Call("applyoperation", task)["state"] == "SUCCEEDED");
    f.native->resources[model_id.get<std::string>()]["fingerprint"] = "locally-edited";
    for (auto row : f.Rows()) {
        if (row["kind"] == "model")
            REQUIRE(row["state"] == "DRIFTED");
        else
            REQUIRE(row["state"] == "DEPENDENCY_UNAVAILABLE");
    }
}

TEST_CASE("Management rejects activating a drifted task but always permits stopping it", "[management]") {
    Fixture f;
    auto task = f.Request("task", "v1", "task");
    REQUIRE(f.Call("applyoperation", task)["state"] == "SUCCEEDED");
    auto id                                = f.Rows()[0]["localId"].get<std::string>();
    Json activate                          = {{"operationId", "activate-drifted"},
                                              {"externalId", "task"},
                                              {"versionId", "v1"},
                                              {"incarnation", f.facts["incarnation"]},
                                              {"enabled", true}};
    f.native->resources[id]["fingerprint"] = "local-change";
    REQUIRE(f.Call("taskactivate", activate)["state"] == "FAILED");
    REQUIRE(f.native->transitions.empty());
    activate["enabled"]     = false;
    activate["operationId"] = "stop-drifted";
    REQUIRE(f.Call("taskactivate", activate)["state"] == "SUCCEEDED");
    REQUIRE(f.native->transitions.back() == id + ":off");
}
