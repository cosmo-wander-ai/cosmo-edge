#pragma once

#include <chrono>
#include <condition_variable>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace cosmo::linkage {

// Board wiring belongs in local configuration, never in a strategy received from the network.
struct AlarmOutputChannel {
    int id{0};
    int gpio{0};
    bool active_low{false};
};

// conf/linkAge/alarmOutputs.json: {"outputs":[{"id":1,"gpio":N,"activeLow":true}]}
// Require explicit, board-verified polarity. Empty/malformed mappings cannot activate outputs.
std::vector<AlarmOutputChannel> LoadAlarmOutputChannels(const std::string& file);

// Internal hardware boundary. Opening a pin must not change its state.
class AlarmOutputPin {
public:
    virtual ~AlarmOutputPin()           = default;
    virtual bool SetActive(bool active) = 0;
};

class AlarmOutputController {
public:
    using PinFactory = std::function<std::unique_ptr<AlarmOutputPin>(const AlarmOutputChannel&)>;
    explicit AlarmOutputController(std::vector<AlarmOutputChannel> channels, PinFactory factory = {});
    ~AlarmOutputController();

    // Repeated alarms extend the current pulse; callers never sleep for the pulse duration.
    bool Pulse(int channel, std::chrono::milliseconds duration);
    void Stop();

private:
    struct Output {
        std::unique_ptr<AlarmOutputPin> pin;
        std::chrono::steady_clock::time_point deadline;
        bool active{false};
    };
    void Run();

    const std::vector<AlarmOutputChannel> channels_;
    PinFactory factory_;
    std::map<int, Output> outputs_;
    std::mutex mutex_;
    std::mutex stop_mutex_;
    std::condition_variable changed_;
    std::thread worker_;
    bool stopped_{false};
};

}  // namespace cosmo::linkage
