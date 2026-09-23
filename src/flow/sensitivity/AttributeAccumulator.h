#pragma once

#include <map>

#include "infer/AiCommon.h"
#include "util/AttributeAnalysis.h"

namespace cosmo {

// Per-track evidence only. Frame ownership, expiry and reporting stay with the
// existing result-accumulation action.
class AttributeAccumulator {
public:
    void Observe(const AttributeSchema& schema, const AiDetectRstEl& target, int64_t timestamp,
                 int64_t interval);
    std::vector<AttributeValue> Finish(const AttributeSchema& schema, size_t minSamples) const;

private:
    struct Vote {
        size_t count{0};
        double score{0};
    };
    struct Evidence {
        int64_t lastSample{-1};
        size_t samples{0};
        bool failed{false};
        std::map<std::string, Vote> votes;
    };
    std::map<std::string, Evidence> evidence_;
};
}  // namespace cosmo
