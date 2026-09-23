#include "flow/sensitivity/AttributeAccumulator.h"

#include <algorithm>
#include <cmath>

namespace cosmo {
void AttributeAccumulator::Observe(const AttributeSchema& schema, const AiDetectRstEl& target,
                                   int64_t timestamp, int64_t interval) {
    if (target.bFilter || !target.areaSign.shielded_areas.empty() || target.areaSign.areas.empty())
        return;
    for (const auto& definition : schema.attributes) {
        const auto source = target.classificationObservations.find(definition.sourceNode);
        if (source == target.classificationObservations.end() ||
            source->second.modelCode != definition.modelCode)
            continue;
        auto& evidence = evidence_[definition.key];
        if (source->second.failed) {
            evidence.failed = true;
            continue;
        }
        if (evidence.lastSample >= 0 &&
            (timestamp <= evidence.lastSample || timestamp - evidence.lastSample < interval))
            continue;
        std::map<std::string, double> candidates;
        for (const auto& result : source->second.results) {
            if (!std::isfinite(result.confidence) || result.confidence < definition.threshold ||
                result.confidence > 1)
                continue;
            for (const auto& option : definition.options) {
                if (option.label == result.label)
                    candidates[option.value] = std::max(candidates[option.value], double(result.confidence));
            }
        }
        if (candidates.empty())
            continue;
        if (definition.type == "single") {
            const auto best =
                std::max_element(candidates.begin(), candidates.end(),
                                 [](const auto& a, const auto& b) { return a.second < b.second; });
            if (std::count_if(candidates.begin(), candidates.end(),
                              [&](const auto& item) { return item.second == best->second; }) != 1)
                continue;
            const auto selected = *best;
            candidates.clear();
            candidates.insert(selected);
        }
        evidence.lastSample = timestamp;
        ++evidence.samples;
        for (const auto& [value, score] : candidates) {
            auto& vote = evidence.votes[value];
            ++vote.count;
            vote.score += score;
        }
    }
}

std::vector<AttributeValue> AttributeAccumulator::Finish(const AttributeSchema& schema,
                                                         size_t minSamples) const {
    std::vector<AttributeValue> result;
    for (const auto& definition : schema.attributes) {
        AttributeValue value;
        value.key        = definition.key;
        const auto found = evidence_.find(definition.key);
        if (found != evidence_.end()) {
            const auto& evidence = found->second;
            value.samples        = static_cast<int64_t>(evidence.samples);
            if (evidence.samples == 0 && evidence.failed)
                value.status = "failed";
            double score = 0;
            if (evidence.samples >= minSamples && evidence.samples > 0) {
                for (const auto& [name, vote] : evidence.votes) {
                    if (double(vote.count) / evidence.samples >= definition.minRatio) {
                        value.values.push_back(name);
                        score += vote.score / vote.count;
                    }
                }
            }
            if (!value.values.empty()) {
                value.status     = "valid";
                value.confidence = score / value.values.size();
            }
        }
        result.push_back(std::move(value));
    }
    return result;
}
}  // namespace cosmo
