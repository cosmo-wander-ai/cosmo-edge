#pragma once
#include <atomic>
#include <memory>
#include <string>
#include <utility>

#include "flow/common/AlgDetectTypes.h"
namespace cosmo {
class AiClassifier;
class AiRecognizer;
// A one-owner handoff bound to the exact task result produced by the native
// classifier.  AlgDataCopy never copies this token into unrelated branches.
class NativeBodyFeatureProof final {
    friend class AiClassifier;
    friend class AiRecognizer;
    NativeBodyFeatureProof(std::string task, std::string channel, DataDetTrackClassifyPtr result)
        : task_(std::move(task)), channel_(std::move(channel)), result_(std::move(result)) {}
    bool Claim(const std::string& task, const std::string& channel,
               const DataDetTrackClassifyPtr& result) noexcept {
        if (task != task_ || channel != channel_ || !result || result != result_)
            return false;
        bool expected = false;
        return claimed_.compare_exchange_strong(expected, true);
    }
    std::string task_, channel_;
    DataDetTrackClassifyPtr result_;
    std::atomic<bool> claimed_{false};
};
}  // namespace cosmo
