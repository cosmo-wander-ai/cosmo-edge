#include "nn/utils/tracker/fire_tracker.h"

namespace cosmo::nn {

FireTracker::FireTracker(DeviceType type, int id) : AbstractTracker() {
    device_type = type;
    device_id   = id;

    pos_prob    = 15;
    neg_prob    = 13;
    sensi_value = 128;
}

FireTracker::~FireTracker() {
    fire_filters.clear();
    idle_fire_filters.clear();
}

void FireTracker::SetPosProb(float v) {
    pos_prob = v;
    for (size_t i = 0; i < fire_filters.size(); i++) {
        fire_filters.at(i)->SetPosProb(pos_prob);
    }
}

float FireTracker::GetPosProb() const {
    return pos_prob;
}

void FireTracker::SetNegProb(float v) {
    neg_prob = v;
    for (size_t i = 0; i < fire_filters.size(); i++) {
        fire_filters.at(i)->SetNegProb(neg_prob);
    }
}

float FireTracker::GetNegProb() const {
    return neg_prob;
}

void FireTracker::SetSensiValue(int v) {
    sensi_value = v;
    for (size_t i = 0; i < fire_filters.size(); i++) {
        fire_filters.at(i)->SetSensiValue(sensi_value);
    }
}

int FireTracker::GetSensiValue() const {
    return sensi_value;
}

Status FireTracker::Filter(std::shared_ptr<Blob> blob) {
    for (size_t i = 0; i < fire_filters.size(); i++) {
        Rect2f region = fire_filters.at(i)->region;
        int x         = std::ceil(region.x);
        int y         = std::ceil(region.y);
        int w         = std::floor(region.width);
        int h         = std::floor(region.height);

        RETURN_ON_FAIL(fire_filters.at(i)->Update(blob, x, y, w, h, nullptr));
    }
    return COSMO_NN_OK;
}

Status FireTracker::GetResult(std::vector<TrackingBox>& output) {
    for (size_t i = 0; i < fire_filters.size(); i++) {
        auto& f = fire_filters.at(i);
        if (f->flag) {
            TrackingBox res;
            res.id           = f->id;
            res.class_id     = f->class_id;
            res.confidence   = f->confidence;
            res.status       = f->status;
            res.box          = f->region;
            res.motion_state = STILL;

            output.emplace_back(res);
        }
    }

    return COSMO_NN_OK;
}

bool FireTracker::OutOfRegion(Rect2f& rect) {
    double x1, y1, x2, y2;
    x1 = rect.x;
    y1 = rect.y;
    x2 = rect.x + rect.width;
    y2 = rect.y + rect.height;

    Rect2f region;
    GetRegion(region);

    if (region.x <= x1 && region.y <= y1) {
        if (region.x + region.width >= x2 && region.y + region.height >= y2) {
            return false;
        }
        return true;
    }

    return true;
}

std::unique_ptr<FireFilter> FireTracker::GetIdleFireFilter() {
    if (idle_fire_filters.empty()) {
        auto f = std::make_unique<FireFilter>(device_type, device_id, next_filter_id_++);
        f->SetPosProb(pos_prob);
        f->SetNegProb(neg_prob);
        f->SetSensiValue(sensi_value);

        return f;
    }

    auto f = std::move(idle_fire_filters.front());
    idle_fire_filters.pop_front();

    f->id                     = next_filter_id_++;
    f->flag                   = false;
    f->status                 = TrackingStatus::NEW;
    f->time_since_last_update = 0;
    f->low_thresh_count       = 0;

    f->SetPosProb(pos_prob);
    f->SetNegProb(neg_prob);
    f->SetSensiValue(sensi_value);

    f->Clear();

    // delete fire filter when idle count bigger than 2
    while (idle_fire_filters.size() > 2) {
        idle_fire_filters.pop_back();
    }

    return f;
}

Status FireTracker::Update(const std::vector<TrackingBox>& bboxes, std::vector<TrackingBox>& output) {
    std::vector<TrackingBox> high_conf_boxes;
    std::vector<TrackingBox> low_conf_boxes;
    std::vector<SecondFireFilter> utrack;
    std::vector<Rect2f> secondPredictedBoxes;

    for (size_t i = 0; i < bboxes.size(); i++) {
        float score  = bboxes.at(i).confidence;
        int class_id = bboxes.at(i).class_id;
        if (config.thresh.size() >= class_id && config.thresh.size() == config.thresh_low.size()) {
            if (score > config.thresh.at(class_id - 1)) {
                high_conf_boxes.push_back(bboxes.at(i));
            } else if (score >= config.thresh_low.at(class_id - 1)) {
                low_conf_boxes.push_back(bboxes.at(i));
            }
        } else {
            LOGE("warning error config.thresh or config.thresh_low.\n");
            return Status(COSMO_NN_ERR_TRACKER_CFG, "error config.thresh or config.thresh_low");
        }
    }

    //  1 Initialize Kalman tracker--------------------------------------------------------------
    if (fire_filters.size() == 0) {  // the first frame met
        // initialize kalman trackers using first detections.
        for (size_t i = 0; i < high_conf_boxes.size(); i++) {
            if (OutOfRegion(high_conf_boxes.at(i).box)) {
                LOGE("Trackingbox out of region\n");
                continue;
            }

            std::unique_ptr<FireFilter> fire_filter = GetIdleFireFilter();
            fire_filter->region                     = bboxes.at(i).box;
            fire_filter->class_id                   = high_conf_boxes.at(i).class_id;
            fire_filter->confidence                 = high_conf_boxes.at(i).confidence;

            fire_filters.emplace_back(std::move(fire_filter));
        }
        return COSMO_NN_OK;
    }

    //  2 Remove predicted boxes --------------------------------------------------------------
    predictedBoxes.clear();
    for (auto it = fire_filters.begin(); it != fire_filters.end(); it++) {
        TrackingBox pBox;
        pBox.id         = (*it)->id;
        pBox.class_id   = (*it)->class_id;
        pBox.confidence = (*it)->confidence;
        pBox.box        = (*it)->region;
        predictedBoxes.push_back(pBox.box);
    }

    // 3 Initialize Hungarian cost matrix via IOU
    unsigned int trkNum = predictedBoxes.size();
    unsigned int detNum = high_conf_boxes.size();

    costMatrix.clear();
    costMatrix.resize(trkNum, std::vector<double>(detNum, 0));
    costDisMatrix.clear();
    costDisMatrix.resize(trkNum, std::vector<double>(detNum, 0));

    // First high-confidence matching
    for (size_t i = 0; i < trkNum; i++) {  // compute iou matrix as a distance matrix
        for (size_t j = 0; j < detNum; j++) {
            double iou_weight = 1 - GetDIOU(predictedBoxes.at(i), high_conf_boxes.at(j).box);
            double weight     = iou_weight;
            weight            = weight < 0 ? 0 : weight;
            costMatrix[i][j]  = weight;
        }
    }

    // 4 Initialize Hungarian algorithm
    assignment.clear();
    unmatchedTrajectories.clear();
    unmatchedDetections.clear();
    allItems.clear();
    matchedItems.clear();
    HungAlgo.Solve(costMatrix, assignment);

    // 5 After matching detections to trackers, find unmatched indices,
    if (detNum >= trkNum) {  //	there are unmatched detections
        for (int n = 0; n < detNum; n++)
            allItems.insert(n);

        for (int i = 0; i < trkNum; ++i)
            matchedItems.insert(assignment.at(i));

        set_difference(allItems.begin(), allItems.end(), matchedItems.begin(), matchedItems.end(),
                       std::insert_iterator<std::set<int>>(unmatchedDetections, unmatchedDetections.begin()));
    } else if (detNum < trkNum) {  // there are unmatched trajectory/predictions
        for (int i = 0; i < trkNum; ++i) {
            // unassigned label will be set as -1 in the assignment algorithm
            if (assignment.at(i) == -1) {
                unmatchedTrajectories.insert(i);
            }
        }
    }

    // 6 For matched boxes, update Kalman filter
    // Track state unchanged during first match
    matchedPairs.clear();
    for (int i = 0; i < trkNum; ++i) {
        int assignm = assignment.at(i);
        if (assignm == -1)  // pass over invalid values
            continue;

        if (costDisMatrix.at(i).at(assignm) < 0.9) {
            fire_filters.at(i)->status                 = TRACKING;
            fire_filters.at(i)->class_id               = high_conf_boxes.at(assignm).class_id;
            fire_filters.at(i)->time_since_last_update = 0;
            fire_filters.at(i)->low_thresh_count       = 0;
            fire_filters.at(i)->confidence             = high_conf_boxes.at(assignm).confidence;

            matchedPairs.push_back(std::pair<int, int>(i, assignm));
        } else {
            unmatchedTrajectories.insert(i);
        }
    }

    // 7 For unmatched boxes, create new tracker
    for (auto umd : unmatchedDetections) {
        if (OutOfRegion(high_conf_boxes.at(umd).box)) {
            continue;
        }
        std::unique_ptr<FireFilter> fire_filter = GetIdleFireFilter();
        fire_filter->region                     = high_conf_boxes.at(umd).box;
        fire_filter->class_id                   = high_conf_boxes.at(umd).class_id;
        fire_filter->confidence                 = high_conf_boxes.at(umd).confidence;

        fire_filters.emplace_back(std::move(fire_filter));
    }

    // Second matching pass
    for (auto umtrack : unmatchedTrajectories) {
        SecondFireFilter tmp_tracker = {fire_filters.at(umtrack).get(), umtrack};
        utrack.emplace_back(tmp_tracker);
        secondPredictedBoxes.push_back(predictedBoxes.at(umtrack));
    }

    size_t utrkNum   = utrack.size();
    size_t lowDetNum = low_conf_boxes.size();

    costMatrix.clear();
    costMatrix.resize(utrkNum, std::vector<double>(lowDetNum, 0));
    costDisMatrix.clear();
    costDisMatrix.resize(utrkNum, std::vector<double>(lowDetNum, 0));

    for (size_t i = 0; i < utrkNum; i++) {  // compute iou matrix as a distance matrix
        for (size_t j = 0; j < lowDetNum; j++) {
            double iou_weight      = 1 - GetDIOU(secondPredictedBoxes.at(i), low_conf_boxes.at(j).box);
            double weight          = iou_weight;
            weight                 = weight < 0 ? 0 : weight;
            costMatrix.at(i).at(j) = weight;
        }
    }

    // Initialize Hungarian algorithm
    assignment.clear();
    unmatchedTrajectories.clear();
    unmatchedDetections.clear();
    allItems.clear();
    matchedItems.clear();
    HungAlgo.Solve(costMatrix, assignment);

    if (lowDetNum >= utrkNum) {  //	there are unmatched detections
        for (int n = 0; n < lowDetNum; n++)
            allItems.insert(n);

        for (int i = 0; i < utrkNum; ++i)
            matchedItems.insert(assignment.at(i));

        set_difference(allItems.begin(), allItems.end(), matchedItems.begin(), matchedItems.end(),
                       std::insert_iterator<std::set<int>>(unmatchedDetections, unmatchedDetections.begin()));
    } else if (lowDetNum < utrkNum) {  // there are unmatched trajectory/predictions
        for (int i = 0; i < utrkNum; ++i) {
            if (assignment.at(i) == -1) {
                // unassigned label will be set as -1 in the assignment algorithm
                fire_filters.at(utrack.at(i).filter_index)->status = LOSS;

                fire_filters.at(utrack.at(i).filter_index)->confidence = -1;

                unmatchedTrajectories.insert(utrack.at(i).filter_index);
            }
        }
    }

    matchedPairs.clear();
    for (int i = 0; i < utrkNum; ++i) {
        int assignm = assignment.at(i);
        if (assignm == -1)  // pass over invalid values
            continue;

        if (costDisMatrix.at(i).at(assignm) < 0.9) {
            fire_filters.at(utrack.at(i).filter_index)->status                 = TRACKING;
            fire_filters.at(utrack.at(i).filter_index)->time_since_last_update = 0;
            fire_filters.at(utrack.at(i).filter_index)->low_thresh_count += 1;
            fire_filters.at(utrack.at(i).filter_index)->confidence = low_conf_boxes.at(assignm).confidence;

            matchedPairs.push_back(std::pair<int, int>(i, assignm));
        } else {
            fire_filters.at(utrack.at(i).filter_index)->status     = LOSS;
            fire_filters.at(utrack.at(i).filter_index)->confidence = -1;
            unmatchedTrajectories.insert(utrack.at(i).filter_index);
            unmatchedDetections.insert(assignm);
        }
    }

    // 8 Extract tracking results

    for (auto it = fire_filters.begin(); it != fire_filters.end();) {
        if ((*it)->time_since_last_update > config.max_age ||
            (*it)->low_thresh_count > config.thresh_low_timeout) {
            idle_fire_filters.emplace_back(std::move(*it));
            it = fire_filters.erase(it);
        } else {
            it++;
        }
    }

    return COSMO_NN_OK;
}

}  // namespace cosmo::nn