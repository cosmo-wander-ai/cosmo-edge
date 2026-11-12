#include "catch_amalgamated.hpp"
#include "util/PathUtil.h"
/*
 * test_camera_service_impl.cc - CameraServiceImpl 核心路径单元测试
 *
 * 测试策略: 不调用 InitCameraEntities（需要文件系统和DI），
 * 仅测试不依赖外部状态的纯逻辑路径。
 */
#include <arpa/inet.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>
#include <vector>

#include "mock/MockTaskService.h"
#include "service/camera/impl/CameraServiceImpl.h"
#include "service/onvif/impl/OnvifServiceImpl.h"
#include "support/MockDefaults.h"
#include "support/ScopedPathOverride.h"
#include "support/ScopedServiceOverride.h"

using namespace cosmo::service;
using namespace cosmo;

namespace {

struct CameraServiceDependencies {
    cosmo::test::MockTaskService taskSvc;
    cosmo::test::NamedExpectations expectations;
    cosmo::test::ScopedServiceOverride<ITaskLifecycle> taskLifecycle{taskSvc};
    cosmo::test::ScopedServiceOverride<ITaskChannel> taskChannel{taskSvc};

    CameraServiceDependencies() {
        cosmo::test::AllowTaskMutationSuccess(taskSvc, expectations);
    }
};

}  // namespace

TEST_CASE("CameraServiceImpl: idle ONVIF reachability survives preview release",
          "[CameraService][onvif][preview]") {
    struct Endpoint {
        int fd = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
        std::atomic<bool> stopping{false};
        std::thread worker;
        uint16_t port = 0;
        Endpoint() {
            sockaddr_in address{};
            address.sin_family      = AF_INET;
            address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            REQUIRE(fd >= 0);
            REQUIRE(::bind(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0);
            socklen_t length = sizeof(address);
            REQUIRE(::getsockname(fd, reinterpret_cast<sockaddr*>(&address), &length) == 0);
            port = ntohs(address.sin_port);
            REQUIRE(::listen(fd, 16) == 0);
            worker = std::thread([this] {
                while (!stopping.load()) {
                    pollfd descriptor{fd, POLLIN, 0};
                    if (::poll(&descriptor, 1, 20) > 0) {
                        const int client = ::accept(fd, nullptr, nullptr);
                        if (client >= 0)
                            ::close(client);
                    }
                }
            });
        }
        void Stop() {
            stopping = true;
            if (worker.joinable())
                worker.join();
            if (fd >= 0) {
                ::close(fd);
                fd = -1;
            }
        }
        ~Endpoint() {
            Stop();
        }
        std::string Url(const std::string& host) const {
            return "http://" + host + ":" + std::to_string(port) + "/onvif/device_service";
        }
    } endpoint;
    const auto base = "/tmp/cosmo_onvif_idle_" +
                      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    std::filesystem::create_directories(base);
    cosmo::test::ScopedPathOverride paths(base, base);
    OnvifServiceImpl onvif;
    onvif.Init();
    cosmo::test::ScopedServiceOverride<IOnvifService> source_service(onvif);
    struct TaskService : cosmo::test::MockTaskService {
        MsgCameraAttr current;
        bool GetChannelAttr(const std::string&, MsgCameraAttr& output) override {
            output = current;
            return true;
        }
    } tasks;
    cosmo::test::NamedExpectations expectations;
    cosmo::test::AllowTaskMutationSuccess(tasks, expectations);
    cosmo::test::ScopedServiceOverride<ITaskLifecycle> lifecycle(tasks);
    cosmo::test::ScopedServiceOverride<ITaskChannel> channel(tasks);
    CameraServiceImpl svc;
    std::string host = "127.0.0.1";
    SECTION("IPv4 endpoint") {}
    SECTION("hostname endpoint") {
        host = "localhost";
    }
    const nlohmann::json request = {{"endpoint", endpoint.Url(host)},
                                    {"username", "test"},
                                    {"password", "test"},
                                    {"profileToken", "profile"}};
    MsgCameraInfo config;
    config.videoChannelId = "onvif-idle";
    config.channelName    = "ONVIF idle";
    config.channelType    = MsgCameraType::MsgCameraTypeOnvif;
    config.url            = onvif.Save(request);
    std::string id;
    REQUIRE(svc.Add(config, id) == util::ErrorEnum::Success);
    const auto query = [&] {
        size_t total = 0;
        auto rows    = svc.Query("", -1, 1, 10, total);
        REQUIRE(rows.size() == 1);
        return rows.front();
    };
    REQUIRE(query().channelStatus == ChannelStatus::ChannelStatusOnline);
    REQUIRE(query().statusReason.empty());

    REQUIRE(svc.AcquirePreviewChannel(id) == util::ErrorEnum::Success);
    tasks.current.channelStatus = ChannelStatus::ChannelStatusOnline;
    tasks.current.dataStatus    = static_cast<int>(cosmo::service::camera::AlgDemuxStatus::AlgDemuxReading);
    REQUIRE(query().channelStatus == ChannelStatus::ChannelStatusOnline);
    REQUIRE_CALL(tasks, TaskIsStart(id + "-ChannelTask")).RETURN(true);
    REQUIRE_CALL(tasks, TaskStop(id + "-ChannelTask")).RETURN(true);
    REQUIRE_CALL(tasks, GetChannelInst(id)).RETURN(nullptr);
    svc.ReleasePreviewChannel(id);
    tasks.current.channelStatus = ChannelStatus::ChannelStatusOffline;
    tasks.current.dataStatus    = static_cast<int>(cosmo::service::camera::AlgDemuxStatus::AlgDemuxClosed);
    REQUIRE(query().channelStatus == ChannelStatus::ChannelStatusOnline);
    REQUIRE(svc.Update(config) == util::ErrorEnum::Success);
    REQUIRE(query().channelStatus == ChannelStatus::ChannelStatusOnline);

    // A reachable device must not mask real media or authentication failures.
    tasks.current.dataStatus = static_cast<int>(cosmo::service::camera::AlgDemuxStatus::AlgDemuxReadFailed);
    REQUIRE(query().channelStatus == ChannelStatus::ChannelStatusOffline);
    REQUIRE(query().statusReason == "stream_read_failed");
    tasks.current.dataStatus =
        static_cast<int>(cosmo::service::camera::AlgDemuxStatus::AlgDemuxOpenUnauthorized);
    tasks.current.channelStatus = ChannelStatus::ChannelStatusUnauthorized;
    REQUIRE(query().channelStatus == ChannelStatus::ChannelStatusUnauthorized);
    REQUIRE(query().statusReason == "authentication_failed");
    tasks.current.channelStatus = ChannelStatus::ChannelStatusOffline;
    tasks.current.dataStatus    = static_cast<int>(cosmo::service::camera::AlgDemuxStatus::AlgDemuxClosed);

    endpoint.Stop();
    REQUIRE(svc.Update(config) == util::ErrorEnum::Success);
    REQUIRE(query().channelStatus == ChannelStatus::ChannelStatusOffline);
    REQUIRE(query().statusReason == "connection_refused");
    Endpoint recovered;
    auto repaired        = request;
    repaired["endpoint"] = recovered.Url(host);
    onvif.Save(repaired, config.url);
    REQUIRE(svc.Update(config) == util::ErrorEnum::Success);
    REQUIRE(query().channelStatus == ChannelStatus::ChannelStatusOnline);
    REQUIRE(query().statusReason.empty());
    svc.Stop();
    std::filesystem::remove_all(base);
}

TEST_CASE("CameraServiceImpl: source errors survive serialization and clear after URL repair",
          "[CameraService][diagnostics]") {
    struct Socket {
        int fd = ::socket(AF_INET, SOCK_STREAM, 0);
        ~Socket() {
            if (fd >= 0)
                ::close(fd);
        }
        std::string Bind(bool listening) {
            REQUIRE(fd >= 0);
            sockaddr_in address{};
            address.sin_family      = AF_INET;
            address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            REQUIRE(::bind(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0);
            socklen_t length = sizeof(address);
            REQUIRE(::getsockname(fd, reinterpret_cast<sockaddr*>(&address), &length) == 0);
            if (listening)
                REQUIRE(::listen(fd, 8) == 0);
            return "rtsp://127.0.0.1:" + std::to_string(ntohs(address.sin_port)) + "/source";
        }
    } refused, available;
    const auto unavailable_url = refused.Bind(false);
    const auto available_url   = available.Bind(true);
    const auto base            = "/tmp/cosmo_camera_diagnostics_" +
                      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    std::filesystem::create_directories(base);
    cosmo::test::ScopedPathOverride paths(base, base);
    struct TaskService : cosmo::test::MockTaskService {
        MsgCameraAttr current;
        bool GetChannelAttr(const std::string&, MsgCameraAttr& output) override {
            output = current;
            return true;
        }
    } tasks;
    cosmo::test::NamedExpectations expectations;
    cosmo::test::AllowTaskMutationSuccess(tasks, expectations);
    cosmo::test::ScopedServiceOverride<ITaskLifecycle> lifecycle(tasks);
    cosmo::test::ScopedServiceOverride<ITaskChannel> channel(tasks);
    CameraServiceImpl svc;
    MsgCameraInfo config;
    config.channelName = "diagnostics";
    config.url         = unavailable_url;
    config.channelType = MsgCameraType::MsgCameraTypeLive;
    std::string id;
    REQUIRE(svc.Add(config, id) == util::ErrorEnum::Success);
    const auto query = [&]() {
        size_t total = 0;
        auto rows    = svc.Query("diagnostics", -1, 1, 10, total);
        REQUIRE(rows.size() == 1);
        return rows.front();
    };
    auto result = query();
    REQUIRE(result.channelStatus == ChannelStatus::ChannelStatusOffline);
    REQUIRE(result.statusReason == "connection_refused");
    const nlohmann::json encoded = static_cast<const MsgCameraAttr&>(result);
    REQUIRE(encoded.get<MsgCameraAttr>().statusReason == "connection_refused");
    REQUIRE_FALSE(encoded.contains("url"));
    REQUIRE(nlohmann::json::object().get<MsgCameraAttr>().statusReason.empty());

    config.videoChannelId = id;
    config.url            = available_url;
    REQUIRE(svc.Update(config) == util::ErrorEnum::Success);
    result = query();
    REQUIRE(result.channelStatus == ChannelStatus::ChannelStatusOnline);
    REQUIRE(result.statusReason.empty());

    // A reachable TCP port must not hide the actual demux failure.
    tasks.current.dataStatus = static_cast<int>(cosmo::service::camera::AlgDemuxStatus::AlgDemuxOpenFailed);
    result                   = query();
    REQUIRE(result.channelStatus == ChannelStatus::ChannelStatusOffline);
    REQUIRE(result.statusReason == "stream_open_failed");
    size_t total = 0;
    REQUIRE(svc.Query("", static_cast<int>(ChannelStatus::ChannelStatusOnline), 1, 10, total).empty());

    // Old dimensions from the previous source must not appear on the failed address.
    tasks.current.dataStatus = static_cast<int>(cosmo::service::camera::AlgDemuxStatus::AlgDemuxInit);
    tasks.current.width      = 1920;
    tasks.current.height     = 1080;
    tasks.current.fps        = 25;
    config.url               = unavailable_url;
    REQUIRE(svc.Update(config) == util::ErrorEnum::Success);
    result = query();
    REQUIRE(result.statusReason == "connection_refused");
    REQUIRE(result.width == 0);
    REQUIRE(result.height == 0);
    REQUIRE(result.fps == 0);
    REQUIRE(result.codec.empty());
    REQUIRE(svc.Delete(id) == util::ErrorEnum::Success);
    svc.Stop();
    std::filesystem::remove_all(base);
}

// ============================================================
// Service lifecycle
// ============================================================

TEST_CASE("CameraServiceImpl: Stop is safe and idempotent without Init", "[CameraService][lifecycle]") {
    CameraServiceImpl svc;
    REQUIRE_NOTHROW(svc.Stop());
    REQUIRE_NOTHROW(svc.Stop());
    REQUIRE_NOTHROW(svc.InitCameraEntities());

    MsgCameraInfo config;
    config.videoChannelId = "camera-after-stop";
    std::string id;
    REQUIRE(svc.Add(config, id) == cosmo::util::ErrorEnum::SysErr);
}

// ============================================================
// 查询参数校验
// ============================================================

TEST_CASE("CameraServiceImpl: Query with invalid pagination returns empty", "[CameraService]") {
    CameraServiceImpl svc;
    size_t total = 0;

    SECTION("pageNum = 0") {
        auto result = svc.Query("", 0, 0, 10, total);
        REQUIRE(result.empty());
    }

    SECTION("pageSize = 0") {
        auto result = svc.Query("", 0, 1, 0, total);
        REQUIRE(result.empty());
    }

    SECTION("negative page") {
        auto result = svc.Query("", 0, -1, 10, total);
        REQUIRE(result.empty());
    }
}

TEST_CASE("CameraServiceImpl: Query with valid pagination but no cameras returns empty", "[CameraService]") {
    CameraServiceImpl svc;
    size_t total = 0;
    auto result  = svc.Query("", 0, 1, 10, total);
    REQUIRE(result.empty());
    REQUIRE(total == 0);
}

// ============================================================
// 非存在 Camera 的操作安全
// ============================================================

TEST_CASE("CameraServiceImpl: operations on non-existent camera return proper errors", "[CameraService]") {
    CameraServiceImpl svc;

    SECTION("Delete non-existent camera returns CameraNotExist") {
        auto ret = svc.Delete("cam_not_exist");
        REQUIRE(ret == cosmo::util::ErrorEnum::CameraNotExist);
    }

    SECTION("Update non-existent camera returns CameraNotExist") {
        cosmo::MsgCameraInfo config;
        config.videoChannelId = "cam_not_exist";
        auto ret              = svc.Update(config);
        REQUIRE(ret == cosmo::util::ErrorEnum::CameraNotExist);
    }

    SECTION("GetTasks for non-existent camera returns empty") {
        auto tasks = svc.GetTasks("cam_not_exist");
        REQUIRE(tasks.empty());
    }

    SECTION("CaptureImage for non-existent camera returns nullptr") {
        auto frame = svc.CaptureImage("cam_not_exist");
        REQUIRE(frame == nullptr);
    }

    SECTION("Preview lease for non-existent camera returns CameraNotExist") {
        REQUIRE(svc.AcquirePreviewChannel("cam_not_exist") == cosmo::util::ErrorEnum::CameraNotExist);
        REQUIRE_NOTHROW(svc.ReleasePreviewChannel("cam_not_exist"));
    }

    SECTION("GetChannelName for non-existent camera returns empty") {
        auto name = svc.GetChannelName("cam_not_exist");
        REQUIRE(name.empty());
    }

    SECTION("QuerySwitch for non-existent camera returns CameraNotExist") {
        bool enable = false;
        auto ret    = svc.QuerySwitch("cam_not_exist", "alg1", enable);
        REQUIRE(ret == cosmo::util::ErrorEnum::CameraNotExist);
    }

    SECTION("ModifyTaskParam for non-existent camera returns CameraNotExist") {
        cosmo::MsgTaskConfig params;
        auto ret = svc.ModifyTaskParam("cam_not_exist", "alg1", params);
        REQUIRE(ret == cosmo::util::ErrorEnum::CameraNotExist);
    }

    SECTION("QueryTaskParam for non-existent camera returns CameraNotExist") {
        std::vector<cosmo::MsgDynamicKeyValue> params;
        auto ret = svc.QueryTaskParam("cam_not_exist", "alg1", params);
        REQUIRE(ret == cosmo::util::ErrorEnum::CameraNotExist);
    }

    SECTION("DeleteTask for non-existent camera returns CameraNotExist") {
        auto ret = svc.DeleteTask("cam_not_exist", "alg1");
        REQUIRE(ret == cosmo::util::ErrorEnum::CameraNotExist);
    }
}

TEST_CASE("CameraServiceImpl: live preview leases keep the channel active", "[CameraService][preview]") {
    const auto test_base = "/tmp/cosmo_camera_preview_lease_" +
                           std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
    std::filesystem::create_directories(test_base);
    cosmo::test::ScopedPathOverride path_override(test_base, test_base);

    CameraServiceDependencies mocks;
    CameraServiceImpl svc;

    MsgCameraInfo config;
    config.videoChannelId = "preview-camera";
    config.channelName    = "Preview Camera";
    config.url            = "rtsp://127.0.0.1:1/test";
    config.channelType    = MsgCameraType::MsgCameraTypeLive;
    std::string id;
    REQUIRE(svc.Add(config, id) == cosmo::util::ErrorEnum::Success);

    trompeloeil::sequence sequence;
    REQUIRE_CALL(mocks.taskSvc, TaskIsStart("preview-camera-ChannelTask"))
        .IN_SEQUENCE(sequence)
        .RETURN(false);
    REQUIRE_CALL(mocks.taskSvc, TaskStart("preview-camera", "preview-camera-ChannelTask"))
        .IN_SEQUENCE(sequence)
        .RETURN(true);
    REQUIRE(svc.AcquirePreviewChannel("preview-camera") == cosmo::util::ErrorEnum::Success);
    REQUIRE(svc.AcquirePreviewChannel("preview-camera") == cosmo::util::ErrorEnum::Success);

    // The first release retains the shared lease. The final release rechecks
    // task demand and stops the otherwise idle channel.
    svc.ReleasePreviewChannel("preview-camera");
    REQUIRE_CALL(mocks.taskSvc, TaskIsStart("preview-camera-ChannelTask")).IN_SEQUENCE(sequence).RETURN(true);
    REQUIRE_CALL(mocks.taskSvc, TaskStop("preview-camera-ChannelTask")).IN_SEQUENCE(sequence).RETURN(true);
    REQUIRE_CALL(mocks.taskSvc, GetChannelInst("preview-camera")).IN_SEQUENCE(sequence).RETURN(nullptr);
    svc.ReleasePreviewChannel("preview-camera");
    REQUIRE_NOTHROW(svc.ReleasePreviewChannel("preview-camera"));

    REQUIRE_NOTHROW(svc.Stop());
    std::filesystem::remove_all(test_base);
}

// ============================================================
// Notify 通知空 ID 列表安全
// ============================================================

TEST_CASE("CameraServiceImpl: NotifyAlgorithms with empty list does not crash", "[CameraService]") {
    CameraServiceImpl svc;

    SECTION("NotifyAlgorithmsChanged with empty ids") {
        REQUIRE_NOTHROW(svc.NotifyAlgorithmsChanged({}, true));
        REQUIRE_NOTHROW(svc.NotifyAlgorithmsChanged({}, false));
    }

    SECTION("NotifyAlgorithmsDeleted with empty ids") {
        REQUIRE_NOTHROW(svc.NotifyAlgorithmsDeleted({}));
    }

    SECTION("NotifyAlgorithmsChanged with non-existent ids does not crash") {
        REQUIRE_NOTHROW(svc.NotifyAlgorithmsChanged({"non_existent_1", "non_existent_2"}, false));
    }

    SECTION("NotifyAlgorithmsDeleted with non-existent ids does not crash") {
        REQUIRE_NOTHROW(svc.NotifyAlgorithmsDeleted({"non_existent_1"}));
    }
}

// ============================================================
// ScheduleInUse 无 Camera 时
// ============================================================

TEST_CASE("CameraServiceImpl: ScheduleInUse returns false when no cameras", "[CameraService]") {
    CameraServiceImpl svc;
    REQUIRE_FALSE(svc.ScheduleInUse("sched_not_exist"));
}

// ============================================================
// 并发查询安全
// ============================================================

TEST_CASE("CameraServiceImpl: concurrent Query calls are safe", "[CameraService][concurrency]") {
    CameraServiceImpl svc;

    std::atomic<bool> stop{false};
    std::atomic<int> readCount{0};

    std::vector<std::thread> readers;
    for (int i = 0; i < 4; i++) {
        readers.emplace_back([&]() {
            while (!stop.load(std::memory_order_relaxed)) {
                size_t total = 0;
                auto result  = svc.Query("", 0, 1, 10, total);
                (void)result;
                readCount.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    stop.store(true);

    for (auto& t : readers) {
        t.join();
    }

    REQUIRE(readCount.load() > 0);
}

TEST_CASE("CameraServiceImpl: concurrent explicit-id Add creates one camera",
          "[CameraService][concurrency]") {
    const auto test_base = "/tmp/cosmo_camera_add_concurrency_" +
                           std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
    std::filesystem::create_directories(test_base);
    cosmo::test::ScopedPathOverride path_override(test_base, test_base);

    CameraServiceDependencies mocks;
    ALLOW_CALL(mocks.taskSvc, TaskCreate(trompeloeil::_, trompeloeil::_, trompeloeil::_, trompeloeil::_))
        .RETURN(cosmo::util::ErrorEnum::Success);
    ALLOW_CALL(mocks.taskSvc, TaskChannelSetUrl(trompeloeil::_, trompeloeil::_));

    CameraServiceImpl svc;
    std::atomic<bool> go{false};
    std::array<cosmo::util::ErrorEnum, 2> results{};
    std::vector<std::thread> adders;
    for (size_t i = 0; i < results.size(); ++i) {
        adders.emplace_back([&, i]() {
            MsgCameraInfo config;
            config.videoChannelId = "fixed-camera-id";
            config.channelName    = "camera";
            config.url            = "rtsp://127.0.0.1:1/test";
            config.channelType    = MsgCameraType::MsgCameraTypeLive;
            std::string id;
            while (!go.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            results[i] = svc.Add(config, id);
        });
    }
    go.store(true, std::memory_order_release);
    for (auto& adder : adders) {
        adder.join();
    }

    const auto success_count   = std::count(results.begin(), results.end(), cosmo::util::ErrorEnum::Success);
    const auto duplicate_count = std::count(results.begin(), results.end(), cosmo::util::ErrorEnum::IDExist);
    REQUIRE(success_count == 1);
    REQUIRE(duplicate_count == 1);

    size_t total = 0;
    auto cameras = svc.Query("", -1, 1, 10, total);
    REQUIRE(total == 1);
    REQUIRE(cameras.size() == 1);

    REQUIRE_CALL(mocks.taskSvc, TaskStop("fixed-camera-id-ChannelTask")).RETURN(true);
    REQUIRE_CALL(mocks.taskSvc, TaskDelete("fixed-camera-id-ChannelTask"))
        .RETURN(cosmo::util::ErrorEnum::Success);
    REQUIRE_NOTHROW(svc.Stop());
    REQUIRE_NOTHROW(svc.Stop());

    std::filesystem::remove_all(test_base);
}

// ============================================================
// 生命周期完整测试 (依赖 DI 注入)
// ============================================================

TEST_CASE("CameraServiceImpl: Full lifecycle (Add Query Update Delete)", "[CameraService]") {
    std::string testBaseDir = "/tmp/cosmo_camera_test_" +
                              std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
    std::filesystem::create_directories(testBaseDir);

    cosmo::test::ScopedPathOverride path_override(testBaseDir, testBaseDir);
    CameraServiceDependencies mocks;

    // 我们必须手动初始化 Entities 以防由于文件系统变化导致的崩溃
    CameraServiceImpl svc;
    svc.InitCameraEntities();

    std::string newCameraId;

    SECTION("Add Camera successfully") {
        cosmo::MsgCameraInfo config;
        config.channelName = "Test_Camera_1";
        config.url         = "rtsp://test/1";

        auto ret = svc.Add(config, newCameraId);
        REQUIRE(ret == cosmo::util::ErrorEnum::Success);
        REQUIRE(!newCameraId.empty());
        REQUIRE(newCameraId.find("RT") == 0);

        // Verify Query
        size_t total = 0;
        auto list    = svc.Query("Test_Camera", -1, 1, 10, total);
        REQUIRE(total == 1);
        REQUIRE(list.size() == 1);
        REQUIRE(list[0].channelName == "Test_Camera_1");

        // Verify Update
        config.videoChannelId = newCameraId;
        config.channelName    = "Test_Camera_Updated";
        ret                   = svc.Update(config);
        REQUIRE(ret == cosmo::util::ErrorEnum::Success);

        list = svc.Query("", -1, 1, 10, total);
        REQUIRE(list.size() == 1);
        REQUIRE(list[0].channelName == "Test_Camera_Updated");

        // Verify GetChannelName
        auto name = svc.GetChannelName(newCameraId);
        REQUIRE(name == "Test_Camera_Updated");

        // Verify Delete
        ret = svc.Delete(newCameraId);
        REQUIRE(ret == cosmo::util::ErrorEnum::Success);

        list = svc.Query("", -1, 1, 10, total);
        REQUIRE(total == 0);
        REQUIRE(list.empty());
    }

    std::filesystem::remove_all(testBaseDir);
}

// ============================================================
// QueryUsbCameraList DI 测试
// ============================================================

TEST_CASE("CameraServiceImpl: QueryUsbCameraList via Dependency Injection", "[CameraService]") {
    std::string testDevDir =
        "/tmp/cosmo_dev_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
    std::filesystem::create_directories(testDevDir);

    // Create fake video device files
    std::ofstream(testDevDir + "/video0");
    std::ofstream(testDevDir + "/video1");
    std::ofstream(testDevDir + "/video99");
    std::ofstream(testDevDir + "/notavideo");

    CameraServiceImpl svc;
    svc.SetUsbDeviceDirMock(testDevDir);

    // 注入一个简单的 Check Mock，所有 video0 都失败，其他的都成功
    svc.SetUsbDeviceCheckMock([](const std::string& path) {
        if (path.find("video0") != std::string::npos)
            return false;
        return true;
    });

    auto devices = svc.QueryUsbCameraList();

    // Should find video1 and video99, but skip video0 (due to check) and notavideo (regex)
    REQUIRE(devices.size() == 2);

    // Check elements
    bool hasVideo1 = false, hasVideo99 = false;
    for (const auto& dev : devices) {
        if (dev.usbDeviceIndex == 1)
            hasVideo1 = true;
        if (dev.usbDeviceIndex == 99)
            hasVideo99 = true;
    }
    REQUIRE(hasVideo1);
    REQUIRE(hasVideo99);

    std::filesystem::remove_all(testDevDir);
}
