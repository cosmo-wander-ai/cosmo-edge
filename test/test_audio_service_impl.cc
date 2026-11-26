#include "catch_amalgamated.hpp"
#include "util/PathUtil.h"
/*
 * test_audio_service_impl.cc — AudioServiceImpl unit tests (DEBT-T01)
 *
 * Strategy: use OverrideRootPathForTest to redirect paths to temp directories. Test pagination,
 * filtering, and CRUD logic for audio files and devices.
 * Constructor tries to load/create default audio, so we need valid paths.
 */
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>

#include "service/media/impl/AudioServiceImpl.h"
#include "service/media/impl/LocalAudioPlayer.h"
#include "service/network/IHttpClient.h"
#include "support/ScopedPathOverride.h"
#include "support/ScopedServiceOverride.h"
#include "util/FileUtil.h"

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

struct LocalAudioFixture : AudioTestFixture {
    const fs::path cache = fs::path(testDir) / "local-audio-cache";
    std::string unavailable_reason;
    std::string prepared_source;
    int conversions{0};
    bool fail_conversion{false};

    static void WriteWave(const fs::path& path, char sample = 1) {
        char wave[] =
            "RIFF\x28\0\0\0"
            "WAVEfmt \x10\0\0\0\x01\0\x01\0\x80\xbb\0\0\0\x77\x01\0\x02\0\x10\0"
            "data\x04\0\0\0\x01\0\x01\0";
        wave[44] = sample;
        std::ofstream(path, std::ios::binary).write(wave, sizeof(wave) - 1);
    }

    std::unique_ptr<LocalAudioPlayer> Player() {
        return std::make_unique<LocalAudioPlayer>(
            cache, [this] { return unavailable_reason; },
            [this](const std::vector<std::string>& args, auto, const auto&) {
                const auto input = std::find(args.begin(), args.end(), "-i");
                if (input != args.end()) {
                    ++conversions;
                    prepared_source = *std::next(input);
                    if (fail_conversion) {
                        return cosmo::util::BoundedProcessResult{1, {}, {}, {}};
                    }
                    WriteWave(args.back());
                }
                return cosmo::util::BoundedProcessResult{0, {}, {}, {}};
            });
    }

    std::string Import(AudioServiceImpl& audio, const std::string& name) const {
        const auto path = fs::path(testDir) / name;
        WriteWave(path, name.front());
        REQUIRE(audio.AddAudioFile(path.string()));
        int total        = 0;
        const auto files = audio.QueryAudioFiles(total, name, {}, 1, 10);
        REQUIRE(files.size() == 1);
        return files.front().id;
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

TEST_CASE("Local Audio Out persists the selected library file and protects it across restarts",
          "[AudioService][local-audio]") {
    LocalAudioFixture fixture;
    std::string selected;
    std::string reason;
    {
        AudioServiceImpl audio(fixture.Player());
        const auto initial = audio.QueryLocalAudioOut();
        CHECK(initial.available);
        CHECK_FALSE(initial.enabled);
        CHECK(initial.fileId == "1234567890");

        selected = fixture.Import(audio, "warning.WAV");
        REQUIRE(audio.SetLocalAudioOut(true, selected, reason));
        CHECK(fixture.conversions == 1);
        CHECK_FALSE(audio.RemoveAudioFile(selected, reason));
        CHECK(reason == "localAudioFileInUse");
    }

    // A lost derived cache must be prepared again from the uploaded source.
    fs::remove_all(fixture.cache);
    AudioServiceImpl audio(fixture.Player());
    CHECK(fixture.conversions == 2);
    const auto restored = audio.QueryLocalAudioOut();
    REQUIRE(restored.enabled);
    REQUIRE(restored.fileId == selected);
    REQUIRE(restored.reason.empty());
    REQUIRE(audio.SetLocalAudioOut(false, selected, reason));
    REQUIRE_FALSE(audio.RemoveAudioFile(selected, reason));
    REQUIRE(reason == "localAudioFileInUse");

    LocalAudioFixture::WriteWave(fs::path(fixture.testDir) / "files/Audio/beep.ogg");
    REQUIRE(audio.SetLocalAudioOut(true, "1234567890", reason));
    CHECK(fixture.prepared_source == fixture.testDir + "/files/Audio/beep.ogg");
    REQUIRE(audio.RemoveAudioFile(selected, reason));
}

TEST_CASE("Local Audio Out keeps the old selection when preparing or persisting a new one fails",
          "[AudioService][local-audio]") {
    LocalAudioFixture fixture;
    AudioServiceImpl audio(fixture.Player());
    std::string reason;
    const auto first = fixture.Import(audio, "first.wav");
    const auto next  = fixture.Import(audio, "next.wav");
    REQUIRE(audio.SetLocalAudioOut(true, first, reason));
    const auto settings_path  = fs::path(fixture.testDir) / "conf/localAudioOut.json";
    const auto saved_settings = cosmo::util::ReadFile(settings_path.string());
    REQUIRE_FALSE(saved_settings.empty());

    SECTION("unknown file ID") {
        CHECK_FALSE(audio.SetLocalAudioOut(true, "unknown", reason));
        CHECK(reason == "fileNotFound");
    }
    SECTION("conversion fails") {
        fixture.fail_conversion = true;
        CHECK_FALSE(audio.SetLocalAudioOut(true, next, reason));
        CHECK_FALSE(reason.empty());
        CHECK(cosmo::util::ReadFile(settings_path.string()) == saved_settings);
    }
    SECTION("uploaded file was replaced by a symlink") {
        const auto candidate = fs::path(fixture.testDir + audio.GetAudioFileWebPath(next));
        REQUIRE(fs::remove(candidate));
        fs::create_symlink(fixture.prepared_source, candidate);
        CHECK_FALSE(audio.SetLocalAudioOut(true, next, reason));
        CHECK(reason == "invalidAudioFile");
    }
    SECTION("settings cannot be atomically replaced") {
        REQUIRE(fs::remove(settings_path));
        REQUIRE(fs::create_directory(settings_path));
        CHECK_FALSE(audio.SetLocalAudioOut(true, next, reason));
        CHECK(reason == "configWriteFailed");
    }
    const auto state = audio.QueryLocalAudioOut();
    CHECK(state.enabled);
    CHECK(state.fileId == first);
    CHECK_FALSE(audio.RemoveAudioFile(first, reason));
    CHECK(reason == "localAudioFileInUse");
    CHECK(audio.RemoveAudioFile(next, reason));
}

TEST_CASE("Local Audio Out can be disabled when the DAC and selected source become unavailable",
          "[AudioService][local-audio]") {
    LocalAudioFixture fixture;
    std::string selected;
    std::string reason;
    {
        AudioServiceImpl audio(fixture.Player());
        selected = fixture.Import(audio, "removed.wav");
        REQUIRE(audio.SetLocalAudioOut(true, selected, reason));
        REQUIRE(fs::remove(fixture.prepared_source));
    }
    fixture.unavailable_reason = "audioDeviceUnavailable";
    {
        AudioServiceImpl audio(fixture.Player());
        const auto state = audio.QueryLocalAudioOut();
        CHECK_FALSE(state.available);
        CHECK(state.enabled);
        CHECK(state.reason == fixture.unavailable_reason);
        REQUIRE(audio.SetLocalAudioOut(false, selected, reason));
    }
    AudioServiceImpl restored(fixture.Player());
    CHECK_FALSE(restored.QueryLocalAudioOut().enabled);
    CHECK(restored.QueryLocalAudioOut().fileId == selected);
}

TEST_CASE("Local Audio Out handles malformed saved configuration without changing the original",
          "[AudioService][local-audio]") {
    LocalAudioFixture fixture;
    const auto settings        = fs::path(fixture.testDir) / "conf/localAudioOut.json";
    const std::string original = R"({"enabled":true,"fileId":"../unexpected"})";
    std::ofstream(settings) << original;
    AudioServiceImpl audio(fixture.Player());
    const auto state = audio.QueryLocalAudioOut();
    REQUIRE(state.available);
    CHECK_FALSE(state.enabled);
    CHECK(state.fileId == "1234567890");
    CHECK(state.reason == "configInvalid");
    CHECK(fixture.conversions == 0);
    CHECK(cosmo::util::ReadFile(settings.string()) == original);
}
