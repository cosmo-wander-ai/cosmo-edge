#ifdef COSMO_NN_USE_AXERA_BACKEND

#include <algorithm>
#include <charconv>
#include <fstream>
#include <optional>
#include <regex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "service/system/impl/AcceleratorMetricsProvider.h"

namespace cosmo::service::detail {
namespace {

    constexpr const char* kNpuEnablePath = "/proc/ax_proc/npu/enable";

    std::optional<bool> ReadNpuEnabled(std::istream& enable) {
        std::string state;
        std::string trailing;
        if (!(enable >> state) || (state != "0" && state != "1") || (enable >> trailing) || enable.bad())
            return std::nullopt;
        return state == "1";
    }

    std::string_view Trim(std::string_view text) {
        const auto first = text.find_first_not_of(" \t\r\n");
        if (first == std::string_view::npos)
            return {};
        return text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1);
    }

    std::optional<double> ParsePercent(std::string_view text) {
        text = Trim(text);
        if (text.empty() || text.back() != '%')
            return std::nullopt;
        text = Trim(text.substr(0, text.size() - 1));
        if (text.empty())
            return std::nullopt;
        unsigned int percent = 0;
        const auto end       = text.data() + text.size();
        const auto parsed    = std::from_chars(text.data(), end, percent);
        if (parsed.ec != std::errc{} || parsed.ptr != end || percent > 100)
            return std::nullopt;
        return static_cast<double>(percent) / 100.0;
    }

    std::optional<std::vector<double>> ParseNpuTop(std::istream& top) {
        constexpr std::string_view utilization_prefix = "utilization:";
        constexpr std::string_view core_prefix        = "core:";
        static const std::regex stale_message(R"(^nputop info not updated over [0-9]+ ms!$)");
        std::vector<double> cores;
        bool idle                 = false;
        bool awaiting_utilization = false;
        for (std::string raw; std::getline(top, raw);) {
            const auto line = Trim(raw);
            if (line == "nputop info is not enabled!")
                return std::nullopt;
            if (line == "nputop info is empty!" ||
                std::regex_match(line.begin(), line.end(), stale_message)) {
                idle = true;
            } else if (line.compare(0, core_prefix.size(), core_prefix) == 0) {
                if (awaiting_utilization || Trim(line.substr(core_prefix.size())).empty())
                    return std::nullopt;
                awaiting_utilization = true;
            } else if (line.compare(0, utilization_prefix.size(), utilization_prefix) == 0) {
                const auto load = ParsePercent(line.substr(utilization_prefix.size()));
                if (!awaiting_utilization || !load)
                    return std::nullopt;
                cores.push_back(*load);
                awaiting_utilization = false;
            }
        }
        if (top.bad() || awaiting_utilization || idle != cores.empty())
            return std::nullopt;
        return cores;
    }

    cosmo::MsgGpuInfo ReadNpuLoad(std::istream& enable, std::istream& top) {
        cosmo::MsgGpuInfo result;
        result.gpuusageAvailable = false;
        result.memoryDomain      = "unavailable";

        if (!ReadNpuEnabled(enable).value_or(false))
            return result;
        if (auto cores = ParseNpuTop(top)) {
            result.gpuusageAvailable = true;
            result.utilizationMetric = "busy-time-load";
            // The fullest vNPU indicates saturation. Keep driver samples per vNPU for telemetry.
            if (!cores->empty())
                result.gpuusage = *std::max_element(cores->begin(), cores->end());
            result.coreUtilizations = std::move(*cores);
        }
        return result;
    }

    class AxeraAcceleratorMetricsProvider final : public AcceleratorMetricsProvider {
    public:
        AxeraAcceleratorMetricsProvider() {
            std::ifstream enable(kNpuEnablePath);
            if (const auto enabled = ReadNpuEnabled(enable); enabled && !*enabled) {
                // AX accounting defaults off on some boots. Enable it once when the driver confirms that
                // state.
                std::ofstream command(kNpuEnablePath);
                command << "1\n";
            }
        }

        cosmo::MsgGpuInfo QueryUtilization() override {
            std::ifstream enable(kNpuEnablePath);
            std::ifstream top("/proc/ax_proc/npu/top");
            return ReadNpuLoad(enable, top);
        }

        int64_t QueryAvailableMemoryMB() override {
            return 0;
        }
    };

}  // namespace

std::unique_ptr<AcceleratorMetricsProvider> CreateAcceleratorMetricsProvider() {
    return std::make_unique<AxeraAcceleratorMetricsProvider>();
}

}  // namespace cosmo::service::detail

#endif  // COSMO_NN_USE_AXERA_BACKEND
