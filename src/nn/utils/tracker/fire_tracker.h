#pragma once

#include <deque>
#include <memory>

#include "nn/utils/tracker/abstract_tracker.h"
#include "nn/utils/tracker/fire_filter.h"

namespace cosmo::nn {

struct SecondFireFilter {
    FireFilter* firefilter;
    int filter_index;
};

class FireTracker : public AbstractTracker {
public:
    FireTracker(DeviceType type, int device_id = 0);
    virtual ~FireTracker();

    virtual Status Update(const std::vector<TrackingBox>& input, std::vector<TrackingBox>& output) override;

    Status Filter(std::shared_ptr<Blob>);

    Status GetResult(std::vector<TrackingBox>& output);

    void SetPosProb(float v);
    float GetPosProb() const;

    void SetNegProb(float v);
    float GetNegProb() const;

    void SetSensiValue(int v);
    int GetSensiValue() const;

private:
    std::unique_ptr<FireFilter> GetIdleFireFilter();

private:
    bool OutOfRegion(Rect2f& rect);

    std::deque<std::unique_ptr<FireFilter>> idle_fire_filters;
    std::vector<std::unique_ptr<FireFilter>> fire_filters;

    DeviceType device_type;
    int device_id;

    // filter param
    float pos_prob;
    float neg_prob;
    int sensi_value;

    unsigned int next_filter_id_ = 0;  // per-instance filter ID counter (not shared across tasks)
};

}  // namespace cosmo::nn
