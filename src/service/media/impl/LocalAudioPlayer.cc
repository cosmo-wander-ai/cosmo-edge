#include "service/media/impl/LocalAudioPlayer.h"

#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <fstream>
#include <limits>
#include <system_error>
#include <utility>

#include "cryptopp/sha.h"
#include "util/Log.h"
#include "util/PathUtil.h"
#include "util/ResourceBudget.h"

namespace cosmo::service {
namespace fs = std::filesystem;
using namespace std::chrono_literals;

namespace {
    using Clock                        = std::chrono::steady_clock;
    constexpr char kPcm[]              = "plughw:CARD=cv186xdac,DEV=0";
    constexpr char kAplay[]            = "/usr/bin/aplay";
    constexpr char kFfmpeg[]           = "/opt/sophon/sophon-ffmpeg-latest/bin/ffmpeg";
    constexpr uint32_t kSampleRate     = 48000;
    constexpr uint32_t kBytesPerSecond = kSampleRate * 2;
    constexpr auto kRetryDelay         = 500ms;

    std::string ProbeHardware() {
        std::ifstream model_file("/proc/device-tree/model");
        std::string model;
        std::getline(model_file, model, '\0');
        if (model.find("BM1688") == std::string::npos)
            return "unsupportedBoard";
        if (access(kAplay, X_OK) != 0)
            return "playbackToolUnavailable";
        if (access(kFfmpeg, X_OK) != 0)
            return "decoderUnavailable";

        std::error_code ec;
        fs::directory_iterator cards("/sys/class/sound", ec);
        for (fs::directory_iterator end; !ec && cards != end; cards.increment(ec)) {
            const auto name = cards->path().filename().string();
            if (name.compare(0, 4, "card") != 0 || name.size() == 4 ||
                name.find_first_not_of("0123456789", 4) != std::string::npos)
                continue;
            std::ifstream id_file(cards->path() / "id");
            std::string id;
            if (std::getline(id_file, id) && id == "cv186xdac") {
                const auto index = name.substr(4);
                if (access(("/dev/snd/controlC" + index).c_str(), R_OK) == 0 &&
                    access(("/dev/snd/pcmC" + index + "D0p").c_str(), R_OK | W_OK) == 0)
                    return {};
            }
        }
        return "audioDeviceUnavailable";
    }

    bool PrivateCacheDirectory(const fs::path& path) {
        if (::mkdir(path.c_str(), 0700) != 0 && errno != EEXIST)
            return false;
        struct stat info {};
        return ::lstat(path.c_str(), &info) == 0 && S_ISDIR(info.st_mode) && info.st_uid == geteuid() &&
               (info.st_mode & 0077) == 0;
    }

    std::string SourceDigest(const fs::path& source) {
        std::ifstream input(source, std::ios::binary);
        CryptoPP::SHA256 hash;
        std::array<CryptoPP::byte, 32768> buffer;
        while (input) {
            input.read(reinterpret_cast<char*>(buffer.data()), buffer.size());
            hash.Update(buffer.data(), static_cast<size_t>(input.gcount()));
        }
        if (!input.eof() || input.bad())
            return {};
        std::array<CryptoPP::byte, CryptoPP::SHA256::DIGESTSIZE> digest;
        hash.Final(digest.data());
        constexpr char kHex[] = "0123456789abcdef";
        std::string text;
        text.reserve(digest.size() * 2);
        for (const auto byte : digest) {
            text.push_back(kHex[byte >> 4]);
            text.push_back(kHex[byte & 15]);
        }
        return text;
    }

    uint32_t ReadLe32(const uint8_t* data) {
        return uint32_t(data[0]) | (uint32_t(data[1]) << 8) | (uint32_t(data[2]) << 16) |
               (uint32_t(data[3]) << 24);
    }

    uint16_t ReadLe16(const uint8_t* data) {
        return uint16_t(data[0]) | (uint16_t(data[1]) << 8);
    }

    // Verify the canonical PCM stream and RIFF chunk boundaries before publishing or reusing a cache.
    std::chrono::milliseconds WaveDuration(const fs::path& file) {
        std::error_code ec;
        if (!fs::is_regular_file(fs::symlink_status(file, ec)))
            return 0ms;
        const auto size = fs::file_size(file, ec);
        std::ifstream input(file, std::ios::binary);
        std::array<uint8_t, 16> bytes{};
        const auto read = [&](size_t count) -> bool {
            return static_cast<bool>(input.read(reinterpret_cast<char*>(bytes.data()), count));
        };
        if (ec || !read(12) || std::memcmp(bytes.data(), "RIFF", 4) != 0 ||
            std::memcmp(bytes.data() + 8, "WAVE", 4) != 0 || uint64_t(ReadLe32(bytes.data() + 4)) + 8 != size)
            return 0ms;

        bool pcm             = false;
        uint32_t audio_bytes = 0;
        uint64_t offset      = 12;
        while (offset + 8 <= size && read(8)) {
            const auto length = ReadLe32(bytes.data() + 4);
            offset += 8 + uint64_t(length) + (length & 1);
            if (offset > size)
                return 0ms;
            if (std::memcmp(bytes.data(), "fmt ", 4) == 0) {
                if (pcm || length < 16 || !read(16))
                    return 0ms;
                pcm = ReadLe16(bytes.data()) == 1 && ReadLe16(bytes.data() + 2) == 1 &&
                      ReadLe32(bytes.data() + 4) == kSampleRate &&
                      ReadLe32(bytes.data() + 8) == kBytesPerSecond && ReadLe16(bytes.data() + 12) == 2 &&
                      ReadLe16(bytes.data() + 14) == 16;
            } else if (std::memcmp(bytes.data(), "data", 4) == 0) {
                if (!pcm || audio_bytes || !length || (length & 1))
                    return 0ms;
                audio_bytes = length;
            }
            input.seekg(static_cast<std::streamoff>(offset));
        }
        if (offset != size)
            return 0ms;
        return std::chrono::milliseconds((uint64_t(audio_bytes) * 1000 + kBytesPerSecond - 1) /
                                         kBytesPerSecond);
    }

    struct TemporaryWave {
        fs::path path;
        ~TemporaryWave() {
            std::error_code ec;
            if (!path.empty())
                fs::remove(path, ec);
        }
    };

    bool CreateTemporaryWave(const fs::path& directory, TemporaryWave& temporary) {
        auto filename = (directory / ".prepare-XXXXXX").string();
        const auto fd = mkstemp(filename.data());
        if (fd < 0)
            return false;
        temporary.path = filename;
        return close(fd) == 0;
    }
}  // namespace

LocalAudioPlayer::LocalAudioPlayer()
    : LocalAudioPlayer(fs::path(path::GetBaseDir()) / "localAudioOutCache", ProbeHardware,
                       [](const auto& argv, auto deadline, const auto& cancelled) {
                           return util::RunBoundedProcess(argv, {}, deadline, cancelled);
                       }) {}

LocalAudioPlayer::LocalAudioPlayer(fs::path cache_directory, Probe probe, Runner runner)
    : cache_directory_(std::move(cache_directory)), probe_(std::move(probe)), runner_(std::move(runner)) {}

LocalAudioPlayer::~LocalAudioPlayer() {
    Stop();
}

std::string LocalAudioPlayer::UnavailableReason() const {
    return probe_();
}

void LocalAudioPlayer::PruneCache(const fs::path& requested) {
    prepared_clips_.erase(std::remove_if(prepared_clips_.begin(), prepared_clips_.end(),
                                         [](const auto& clip) { return clip.expired(); }),
                          prepared_clips_.end());
    std::error_code ec;
    fs::directory_iterator files(cache_directory_, ec);
    for (fs::directory_iterator end; !ec && files != end; files.increment(ec)) {
        const auto& path = files->path();
        if (path == requested ||
            std::any_of(prepared_clips_.begin(), prepared_clips_.end(), [&](const auto& ref) {
                const auto clip = ref.lock();
                return clip && path == clip->path;
            }))
            continue;
        const auto name = path.filename().string();
        if (name.compare(0, 8, "clip-v1-") == 0 || name.compare(0, 9, ".prepare-") == 0) {
            std::error_code ignored;
            if (fs::is_regular_file(files->symlink_status(ignored)))
                fs::remove(path, ignored);
        }
    }
}

LocalAudioPlayer::ClipPtr LocalAudioPlayer::Prepare(const std::string& file_id, const fs::path& source,
                                                    std::string& reason) {
    reason = "invalidAudioFile";
    std::error_code ec;
    if (!path::IsSafePathComponent(file_id) || !source.is_absolute() ||
        !fs::is_regular_file(fs::symlink_status(source, ec)))
        return {};
    auto format = source.extension().string();
    std::transform(format.begin(), format.end(), format.begin(),
                   [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
    if (format != ".mp3" && format != ".wav" && format != ".ogg")
        return {};
    const auto digest = SourceDigest(source);
    if (digest.empty())
        return {};
    reason = "cacheUnavailable";
    if (!PrivateCacheDirectory(cache_directory_))
        return {};
    const auto output = cache_directory_ / ("clip-v1-" + file_id + "-" + digest + ".wav");
    PruneCache(output);
    auto duration = WaveDuration(output);
    if (duration == 0ms) {
        const auto budget = util::InspectStorageResourceBudget(cache_directory_.string());
        if (!budget.valid)
            return {};
        // WAV/RIFF chunk sizes are 32-bit. FFmpeg may finish successfully when -fs cuts a stream,
        // so reaching this ceiling is a rejected conversion, never a playable partial file.
        const auto limit = std::min<uint64_t>(budget.usable_bytes, std::numeric_limits<uint32_t>::max());
        if (limit <= 44) {
            reason = "insufficientSpace";
            return {};
        }
        TemporaryWave temporary;
        if (!CreateTemporaryWave(cache_directory_, temporary))
            return {};
        const std::vector<std::string> argv{kFfmpeg,
                                            "-nostdin",
                                            "-v",
                                            "error",
                                            "-xerror",
                                            "-y",
                                            "-protocol_whitelist",
                                            "file",
                                            "-f",
                                            format.substr(1),
                                            "-i",
                                            source.string(),
                                            "-map",
                                            "0:a:0",
                                            "-map_metadata",
                                            "-1",
                                            "-ac",
                                            "1",
                                            "-ar",
                                            "48000",
                                            "-c:a",
                                            "pcm_s16le",
                                            "-fs",
                                            std::to_string(limit),
                                            "-f",
                                            "wav",
                                            temporary.path.string()};
        const auto result = runner_(argv, Clock::now() + 5min, [this] {
            std::lock_guard<std::mutex> lock(mutex_);
            return stopped_;
        });
        const auto size   = fs::file_size(temporary.path, ec);
        if ((!ec && size >= limit) || result.error.find("No space left on device") != std::string::npos) {
            reason = "insufficientSpace";
            return {};
        }
        duration = WaveDuration(temporary.path);
        if (!result.failure.empty() || result.exitCode != 0 || duration == 0ms) {
            reason = "conversionFailed";
            return {};
        }
        fs::permissions(temporary.path, fs::perms::owner_read, ec);
        if (ec)
            return {};
        fs::rename(temporary.path, output, ec);
        if (ec)
            return {};
        temporary.path.clear();
    }
    auto clip = std::make_shared<Clip>(Clip{output, duration});
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopped_) {
            reason = "audioDeviceUnavailable";
            return {};
        }
        if (!worker_.joinable()) {
            try {
                worker_ = std::thread(&LocalAudioPlayer::Run, this);
            } catch (const std::system_error&) {
                reason = "playbackToolUnavailable";
                return {};
            }
        }
    }
    prepared_clips_.push_back(clip);
    reason.clear();
    return clip;
}

void LocalAudioPlayer::SetClip(ClipPtr clip) {
    std::unique_lock<std::mutex> lock(mutex_);
    if (stopped_)
        return;
    selected_ = std::move(clip);
    if (!selected_) {
        ++generation_;
        pending_ = 0;
        changed_.notify_all();
        changed_.wait(lock, [this] { return !playing_; });
    } else {
        changed_.notify_all();
    }
}

void LocalAudioPlayer::Enqueue() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (selected_ && !stopped_ && pending_ < std::numeric_limits<uint64_t>::max()) {
        ++pending_;
        changed_.notify_one();
    }
}

void LocalAudioPlayer::Stop() {
    std::lock_guard<std::mutex> stop_lock(stop_mutex_);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopped_ = true;
        selected_.reset();
        pending_ = 0;
    }
    changed_.notify_all();
    if (worker_.joinable())
        worker_.join();
}

void LocalAudioPlayer::Run() {
    std::unique_lock<std::mutex> lock(mutex_);
    auto retry = kRetryDelay;
    for (;;) {
        changed_.wait(lock, [this] { return stopped_ || (selected_ && pending_); });
        if (stopped_)
            return;
        const auto clip       = selected_;
        const auto generation = generation_;
        playing_              = clip;
        lock.unlock();

        const auto cancelled = [this, generation] {
            std::lock_guard<std::mutex> guard(mutex_);
            return stopped_ || generation != generation_;
        };
        util::BoundedProcessResult result;
        try {
            result.failure = UnavailableReason();
            if (result.failure.empty())
                result = runner_({kAplay, "-q", "-D", kPcm, "-t", "wav", clip->path.string()},
                                 Clock::now() + clip->duration + 5s, cancelled);
        } catch (const std::exception&) {
            result.failure = "playbackFailed";
        }
        const bool played = result.failure.empty() && result.exitCode == 0;
        if (played)
            LOG_INFO("Local Audio Out finished one alarm on {}", kPcm);
        else if (retry == kRetryDelay && !cancelled())
            LOG_WARN("Local Audio Out playback failed (exit {}, {}), retrying", result.exitCode,
                     result.failure.empty() ? result.error.substr(0, 256) : result.failure);

        lock.lock();
        playing_.reset();
        changed_.notify_all();
        if (stopped_)
            return;
        if (generation != generation_) {
            retry = kRetryDelay;
        } else if (played) {
            --pending_;
            retry = kRetryDelay;
        } else {
            const bool changed = changed_.wait_for(
                lock, retry, [&] { return stopped_ || generation != generation_ || selected_ != clip; });
            retry = changed ? kRetryDelay : std::min(retry * 2, 30000ms);
        }
    }
}

}  // namespace cosmo::service
