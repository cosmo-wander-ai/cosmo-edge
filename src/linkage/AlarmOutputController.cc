#include "linkage/AlarmOutputController.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fstream>
#include <nlohmann/json.hpp>
#include <set>
#include <stdexcept>
#include <utility>

#ifdef __linux__
#include <fcntl.h>
#include <unistd.h>
#endif

#include "util/FileUtil.h"
#include "util/Log.h"

namespace cosmo::linkage {
namespace {
#ifdef __linux__
    std::string ReadAttribute(const std::string& path) {
        std::ifstream input(path);
        std::string value;
        input >> value;
        return value;
    }

    class LinuxAlarmOutputPin : public AlarmOutputPin {
    public:
        LinuxAlarmOutputPin(int fd, std::string path, bool active_low)
            : fd_(fd), path_(std::move(path)), active_low_(active_low) {}
        ~LinuxAlarmOutputPin() override {
            close(fd_);
        }
        bool SetActive(bool active) override {
            // Keep the board's export, direction and active_low settings intact.
            const auto inverted = ReadAttribute(path_ + "/active_low");
            if (ReadAttribute(path_ + "/direction") != "out" || (inverted != "0" && inverted != "1")) {
                return false;
            }
            const bool physical_high = active != active_low_;
            const char value         = (physical_high != (inverted == "1")) ? '1' : '0';
            return lseek(fd_, 0, SEEK_SET) >= 0 && write(fd_, &value, 1) == 1;
        }

    private:
        int fd_;
        std::string path_;
        bool active_low_;
    };
#endif

    std::unique_ptr<AlarmOutputPin> OpenPin(const AlarmOutputChannel& channel) {
#ifdef __linux__
        const auto path = "/sys/class/gpio/gpio" + std::to_string(channel.gpio);
        if (ReadAttribute(path + "/direction") != "out") {
            LOG_ERRO("AlarmOut {}: board GPIO is not exported as an output", channel.id);
            return nullptr;
        }
        const int fd = open((path + "/value").c_str(), O_WRONLY | O_CLOEXEC | O_NOFOLLOW);
        if (fd < 0) {
            LOG_ERRO("AlarmOut {}: cannot open GPIO value: {}", channel.id, std::strerror(errno));
            return nullptr;
        }
        return std::make_unique<LinuxAlarmOutputPin>(fd, path, channel.active_low);
#else
        LOG_ERRO("AlarmOut {}: Linux GPIO support is unavailable", channel.id);
        return nullptr;
#endif
    }
}  // namespace

std::vector<AlarmOutputChannel> LoadAlarmOutputChannels(const std::string& file) {
    constexpr size_t kMaxConfigBytes = 64 * 1024;
    const auto content               = cosmo::util::ReadFile(file, kMaxConfigBytes + 1);
    if (content.empty()) {
        return {};
    }
    if (content.size() > kMaxConfigBytes) {
        LOG_ERRO("{}", "AlarmOut board configuration exceeds size limit");
        return {};
    }
    try {
        const auto doc      = nlohmann::json::parse(content);
        const auto& outputs = doc.at("outputs");
        if (!outputs.is_array() || outputs.size() > 64) {
            throw std::invalid_argument("outputs must be an array with at most 64 channels");
        }
        std::vector<AlarmOutputChannel> channels;
        std::set<int> ids;
        std::set<int> pins;
        for (const auto& item : outputs) {
            if (!item.at("id").is_number_integer() || !item.at("gpio").is_number_integer() ||
                !item.at("activeLow").is_boolean()) {
                throw std::invalid_argument("invalid channel field types");
            }
            const auto id   = item.at("id").get<int64_t>();
            const auto gpio = item.at("gpio").get<int64_t>();
            if (id < 1 || id > 64 || gpio < 0 || gpio > 65535) {
                throw std::invalid_argument("invalid channel mapping");
            }
            AlarmOutputChannel channel{static_cast<int>(id), static_cast<int>(gpio),
                                       item.at("activeLow").get<bool>()};
            if (!ids.insert(channel.id).second || !pins.insert(channel.gpio).second) {
                throw std::invalid_argument("duplicate channel or GPIO line");
            }
            channels.push_back(std::move(channel));
        }
        return channels;
    } catch (const std::exception& error) {
        LOG_ERRO("AlarmOut board configuration rejected: {}", error.what());
        return {};
    }
}

AlarmOutputController::AlarmOutputController(std::vector<AlarmOutputChannel> channels, PinFactory factory)
    : channels_(std::move(channels)), factory_(factory ? std::move(factory) : OpenPin) {}

AlarmOutputController::~AlarmOutputController() {
    Stop();
}

bool AlarmOutputController::Pulse(int channel, std::chrono::milliseconds duration) {
    if (duration.count() <= 0 || duration > std::chrono::seconds(600)) {
        return false;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (stopped_) {
        return false;
    }
    const auto mapping = std::find_if(channels_.begin(), channels_.end(),
                                      [channel](const auto& item) { return item.id == channel; });
    if (mapping == channels_.end()) {
        LOG_ERRO("AlarmOut {} has no board mapping", channel);
        return false;
    }
    auto& output = outputs_[channel];
    if (!output.pin) {
        output.pin = factory_(*mapping);
        if (!output.pin) {
            return false;
        }
    }
    // Start the reset worker before activating any physical output.
    if (!worker_.joinable()) {
        try {
            worker_ = std::thread(&AlarmOutputController::Run, this);
        } catch (const std::exception& error) {
            LOG_ERRO("AlarmOut reset worker could not start: {}", error.what());
            return false;
        }
    }
    if (!output.active && !output.pin->SetActive(true)) {
        LOG_ERRO("AlarmOut {} activation failed", channel);
        return false;
    }
    const auto deadline = std::chrono::steady_clock::now() + duration;
    output.deadline     = output.active ? std::max(output.deadline, deadline) : deadline;
    output.active       = true;
    changed_.notify_all();
    return true;
}

void AlarmOutputController::Run() {
    std::unique_lock<std::mutex> lock(mutex_);
    while (!stopped_) {
        auto next      = std::chrono::steady_clock::time_point::max();
        const auto now = std::chrono::steady_clock::now();
        for (auto& [id, output] : outputs_) {
            if (!output.active) {
                continue;
            }
            if (output.deadline <= now) {
                if (output.pin->SetActive(false)) {
                    output.active = false;
                    continue;
                }
                LOG_ERRO("AlarmOut {} reset failed; retrying", id);
                output.deadline = now + std::chrono::seconds(1);
            }
            next = std::min(next, output.deadline);
        }
        if (next == std::chrono::steady_clock::time_point::max()) {
            changed_.wait(lock);
        } else {
            changed_.wait_until(lock, next);
        }
    }
}

void AlarmOutputController::Stop() {
    std::lock_guard<std::mutex> stop_lock(stop_mutex_);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopped_ = true;
        changed_.notify_all();
    }
    if (worker_.joinable()) {
        worker_.join();
    }
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& [id, output] : outputs_) {
        if (output.pin && output.active && !output.pin->SetActive(false)) {
            LOG_ERRO("AlarmOut {} reset failed during shutdown", id);
        }
    }
    outputs_.clear();
}

}  // namespace cosmo::linkage
