#pragma once

#include <set>
#include <vector>

#include "nn/core/status.h"
#include "nn/utils/tracker/hungarian.h"
#include "nn/utils/tracker_common.h"

namespace cosmo::nn {

class AbstractTracker {
public:
    AbstractTracker();

    virtual ~AbstractTracker();

    Status SetRegion(Rect2f& region);
    Status GetRegion(Rect2f& region);

    Status SetTrackerConfig(TrackerConfig& c);
    Status GetTrackerConfig(TrackerConfig& c);

    virtual Status Update(const std::vector<TrackingBox>& input, std::vector<TrackingBox>& output) = 0;

protected:
    std::vector<Rect2f> predictedBoxes;

    std::vector<std::vector<double>> costMatrix;  // Maximum matching cost matrix
    std::vector<std::vector<double>> costDisMatrix;
    std::vector<int> assignment;
    std::set<int> unmatchedDetections;
    std::set<int> unmatchedTrajectories;
    std::set<int> allItems;
    std::set<int> matchedItems;
    std::vector<std::pair<int, int>> matchedPairs;

    HungarianAlgorithm HungAlgo;

    Rect2f track_region = Rect2f(0, 0, 1920, 1080);

    TrackerConfig config;
};

}  // namespace cosmo::nn
