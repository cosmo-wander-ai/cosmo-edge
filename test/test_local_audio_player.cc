#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <string>
#include <vector>

#include "catch_amalgamated.hpp"
#include "service/media/impl/LocalAudioPlayer.h"
#include "util/UuidUtil.h"

namespace {
using cosmo::service::LocalAudioPlayer;
using cosmo::util::BoundedProcessResult;
using namespace std::chrono_literals;
namespace fs = std::filesystem;

void WriteLe(std::ofstream& output, uint32_t value, size_t bytes) {
    for (size_t i = 0; i < bytes; ++i)
        output.put(static_cast<char>((value >> (i * 8)) & 255));
}

void WriteWave(const fs::path& path, char sample) {
    constexpr uint32_t kDataBytes = 960;
    std::ofstream output(path, std::ios::binary);
    output.write("RIFF", 4);
    WriteLe(output, 36 + kDataBytes, 4);
    output.write("WAVEfmt ", 8);
    WriteLe(output, 16, 4);
    WriteLe(output, 1, 2);
    WriteLe(output, 1, 2);
    WriteLe(output, 48000, 4);
    WriteLe(output, 96000, 4);
    WriteLe(output, 2, 2);
    WriteLe(output, 16, 2);
    output.write("data", 4);
    WriteLe(output, kDataBytes, 4);
    const std::string samples(kDataBytes, sample);
    output.write(samples.data(), samples.size());
}

struct Directory {
    fs::path root = fs::temp_directory_path() / ("cosmo-local-audio-" + cosmo::util::GenerateUUID());
    Directory() {
        fs::create_directory(root);
    }
    ~Directory() {
        std::error_code ignored;
        fs::remove_all(root, ignored);
    }
};

// Fake child processes expose starts, completions and cancellations without touching a physical speaker.
class AudioChildren {
public:
    BoundedProcessResult Run(const std::vector<std::string>& argv, std::chrono::steady_clock::time_point,
                             const std::function<bool()>& cancelled) {
        BoundedProcessResult result;
        if (argv.front().find("ffmpeg") != std::string::npos) {
            ++conversions;
            const auto input = std::find(argv.begin(), argv.end(), "-i");
            if (input == argv.end() || std::next(input) == argv.end())
                return result;
            std::error_code ec;
            fs::copy_file(*std::next(input), argv.back(), fs::copy_options::overwrite_existing, ec);
            result.exitCode = ec ? 1 : 0;
            return result;
        }
        std::unique_lock<std::mutex> lock(mutex_);
        attempts_.emplace_back(argv.back());
        changed_.notify_all();
        if (fail_next) {
            fail_next       = false;
            result.exitCode = 1;
            return result;
        }
        while (!permits_ && !cancelled())
            changed_.wait_for(lock, 5ms);
        if (cancelled()) {
            ++cancellations_;
            result.failure = "process_cancelled";
        } else {
            --permits_;
            ++completions_;
            result.exitCode = 0;
        }
        changed_.notify_all();
        return result;
    }

    void Allow(unsigned count) {
        std::lock_guard<std::mutex> lock(mutex_);
        permits_ += count;
        changed_.notify_all();
    }
    bool WaitForAttempts(unsigned count) {
        std::unique_lock<std::mutex> lock(mutex_);
        return changed_.wait_for(lock, 3s, [&] { return attempts_.size() >= count; });
    }
    bool WaitForCompletions(unsigned count) {
        std::unique_lock<std::mutex> lock(mutex_);
        return changed_.wait_for(lock, 3s, [&] { return completions_ >= count; });
    }
    std::vector<fs::path> Attempts() {
        std::lock_guard<std::mutex> lock(mutex_);
        return attempts_;
    }
    unsigned Cancellations() {
        std::lock_guard<std::mutex> lock(mutex_);
        return cancellations_;
    }

    unsigned conversions{0};
    bool fail_next{false};

private:
    std::mutex mutex_;
    std::condition_variable changed_;
    std::vector<fs::path> attempts_;
    unsigned completions_{0};
    unsigned cancellations_{0};
    unsigned permits_{0};
};

struct Fixture {
    Directory directory;
    AudioChildren children;
    LocalAudioPlayer::Runner runner = [this](const auto& argv, auto deadline, const auto& cancelled) {
        return children.Run(argv, deadline, cancelled);
    };
    LocalAudioPlayer player{directory.root / "cache", [] { return std::string{}; }, runner};
    std::string reason;

    LocalAudioPlayer::ClipPtr Prepare(const std::string& name, char sample = 0) {
        const auto source = directory.root / (name + ".wav");
        WriteWave(source, sample);
        return player.Prepare(name, source, reason);
    }
};
}  // namespace

TEST_CASE("Local audio cache reuses intact PCM and rejects incomplete conversions", "[local-audio]") {
    Fixture fixture;
    auto first = fixture.Prepare("alarm");
    REQUIRE(first);
    REQUIRE(first->duration == 10ms);
    REQUIRE(fixture.reason.empty());
    REQUIRE(fixture.children.conversions == 1);
    const auto source = fixture.directory.root / "alarm.wav";
    LocalAudioPlayer restarted(
        fixture.directory.root / "cache", [] { return std::string{}; }, fixture.runner);
    auto reused = restarted.Prepare("alarm", source, fixture.reason);
    REQUIRE(reused);
    CHECK(reused->path == first->path);
    REQUIRE(fixture.children.conversions == 1);

    auto changed = fixture.Prepare("alarm", 1);
    REQUIRE(changed);
    REQUIRE(changed->path != first->path);
    REQUIRE(fs::exists(first->path));
    REQUIRE(fixture.children.conversions == 2);

    fs::resize_file(source, fs::file_size(source) - 2);
    REQUIRE_FALSE(fixture.player.Prepare("alarm", source, fixture.reason));
    CHECK(fixture.reason == "conversionFailed");
    CHECK(fs::exists(changed->path));
    REQUIRE_FALSE(fixture.player.Prepare("../alarm", source, fixture.reason));
    CHECK(fixture.reason == "invalidAudioFile");
}

TEST_CASE("Local audio finishes the current clip before queued alarms use a replacement", "[local-audio]") {
    Fixture fixture;
    const auto first  = fixture.Prepare("first");
    const auto second = fixture.Prepare("second", 1);
    REQUIRE(first);
    REQUIRE(second);
    fixture.player.SetClip(first);
    for (int count = 0; count < 3; ++count)
        fixture.player.Enqueue();
    REQUIRE(fixture.children.WaitForAttempts(1));
    fixture.player.SetClip(second);
    fixture.children.Allow(3);
    REQUIRE(fixture.children.WaitForCompletions(3));
    fixture.player.Stop();
    const std::vector<fs::path> expected{first->path, second->path, second->path};
    CHECK(fixture.children.Attempts() == expected);
}

TEST_CASE("Local audio retains a busy playback and clears cancelled backlog", "[local-audio]") {
    Fixture fixture;
    const auto clip = fixture.Prepare("alarm");
    REQUIRE(clip);
    fixture.children.fail_next = true;
    fixture.player.SetClip(clip);
    fixture.player.Enqueue();
    REQUIRE(fixture.children.WaitForAttempts(2));
    fixture.children.Allow(1);
    REQUIRE(fixture.children.WaitForCompletions(1));

    fixture.player.Enqueue();
    fixture.player.Enqueue();
    REQUIRE(fixture.children.WaitForAttempts(3));
    fixture.player.SetClip({});
    REQUIRE(fixture.children.Cancellations() == 1);
    fixture.player.Enqueue();
    fixture.player.SetClip(clip);
    fixture.player.Enqueue();
    fixture.children.Allow(1);
    REQUIRE(fixture.children.WaitForCompletions(2));
    fixture.player.Stop();
    fixture.player.SetClip(clip);
    fixture.player.Enqueue();
    fixture.player.Stop();
    REQUIRE(fixture.children.Attempts().size() == 4);
}
