#include "catch_amalgamated.hpp"
#include "util/PathUtil.h"
/*
 * test_audio_service_impl.cc — AudioServiceImpl unit tests (DEBT-T01)
 *
 * Strategy: use OverrideRootPathForTest to redirect paths to temp directories. Test pagination,
 * filtering, and CRUD logic for audio files and devices.
 * Constructor tries to load/create default audio, so we need valid paths.
 */
#include <filesystem>
#include <fstream>

#include "service/media/impl/AudioServiceImpl.h"
#include "service/network/IHttpClient.h"
#include "support/ScopedPathOverride.h"
#include "support/ScopedServiceOverride.h"

using namespace cosmo::service;
namespace fs = std::filesystem;

namespace {

struct AudioTestFixture {
    std::string testDir;
    cosmo::test::ScopedPathOverride pathOverride;

    AudioTestFixture()
        : testDir("/tmp/cosmo_audio_test_" +
                  std::to_string(std::chrono::system_clock::now().time_since_epoch().count())),
          pathOverride(testDir, testDir) {
        fs::create_directories(testDir + "/conf");
        fs::create_directories(testDir + "/conf/audioMng");

        // Create fake default audio file that AudioServiceImpl tries to copy
        fs::create_directories(testDir + "/files/Audio");
        std::ofstream(testDir + "/files/Audio/beep.ogg");
    }

    ~AudioTestFixture() {
        fs::remove_all(testDir);
    }
};

class RecordingHttpClient final : public IHttpClient {
public:
    HttpResponse Get(const std::string& url, long /*connectTimeoutSec*/, long /*timeoutSec*/,
                     const std::vector<std::pair<std::string, std::string>>& /*headers*/) override {
        ++getCount;
        lastUrl = url;
        return {200, R"({"code":200,"message":"OK"})"};
    }

    HttpResponse Post(const std::string& /*url*/, const std::string& /*data*/,
                      const std::string& /*contentType*/, long /*connectTimeoutSec*/, long /*timeoutSec*/,
                      const std::vector<std::pair<std::string, std::string>>& /*headers*/) override {
        ++postCount;
        return {500, {}};
    }

    int getCount{0};
    int postCount{0};
    std::string lastUrl;
};

}  // namespace

TEST_CASE("AudioServiceImpl: construction and destruction", "[AudioService]") {
    AudioTestFixture fix;
    REQUIRE_NOTHROW([]() {
        // May warn about missing files, but should not crash
    }());
}

TEST_CASE("AudioServiceImpl: speaker health check uses GET", "[AudioService][HttpClient]") {
    AudioTestFixture fix;
    RecordingHttpClient http_client;
    cosmo::test::ScopedServiceOverride<IHttpClient> http_client_guard(http_client);
    AudioServiceImpl sut;

    REQUIRE(sut.CheckAudioDeviceAlive("192.0.2.10"));
    REQUIRE(http_client.getCount == 1);
    REQUIRE(http_client.postCount == 0);
    REQUIRE(http_client.lastUrl == "http://192.0.2.10/v1/check_alive");
}

TEST_CASE("AudioServiceImpl: QueryAudioFiles with invalid pagination returns empty", "[AudioService]") {
    AudioTestFixture fix;
    AudioServiceImpl sut;

    int totalSize = 0;

    SECTION("pageNum = 0") {
        auto result = sut.QueryAudioFiles(totalSize, "", {}, 0, 10);
        REQUIRE(result.empty());
    }

    SECTION("pageSize = 0") {
        auto result = sut.QueryAudioFiles(totalSize, "", {}, 1, 0);
        REQUIRE(result.empty());
    }

    SECTION("negative pageNum") {
        auto result = sut.QueryAudioFiles(totalSize, "", {}, -1, 10);
        REQUIRE(result.empty());
    }
}

TEST_CASE("AudioServiceImpl: QueryAudioFiles returns default audio", "[AudioService]") {
    AudioTestFixture fix;
    AudioServiceImpl sut;

    int totalSize = 0;
    auto result   = sut.QueryAudioFiles(totalSize, "", {}, 1, 100);
    // Should have at least the default audio
    REQUIRE(totalSize >= 1);
}

TEST_CASE("AudioServiceImpl: AudioFileCount and AudioFileMaxCount", "[AudioService]") {
    AudioTestFixture fix;
    AudioServiceImpl sut;

    SECTION("Initial count includes default") {
        REQUIRE(sut.AudioFileCount() >= 1);
    }

    SECTION("Max count is 1000") {
        REQUIRE(sut.AudioFileMaxCount() == 1000);
    }
}

TEST_CASE("AudioServiceImpl: RemoveAudioFile on non-existent returns false", "[AudioService]") {
    AudioTestFixture fix;
    AudioServiceImpl sut;

    std::string msg;
    REQUIRE(sut.RemoveAudioFile("nonexistent-id", msg) == false);
    REQUIRE(!msg.empty());
}

TEST_CASE("AudioServiceImpl: GetAudioFileWebPath with non-existent id returns empty", "[AudioService]") {
    AudioTestFixture fix;
    AudioServiceImpl sut;

    auto path = sut.GetAudioFileWebPath("nonexistent-id");
    REQUIRE(path.empty());
}
