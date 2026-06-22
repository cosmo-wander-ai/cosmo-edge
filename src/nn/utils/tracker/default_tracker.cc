#include "nn/utils/tracker/default_tracker.h"

namespace cosmo::nn {

DefaultTracker::DefaultTracker() : AbstractTracker() {}

DefaultTracker::~DefaultTracker() {}

Status DefaultTracker::Update(const std::vector<TrackingBox>& bboxes, std::vector<TrackingBox>& output) {
    std::vector<TrackingBox> high_conf_boxes;
    std::vector<TrackingBox> low_conf_boxes;
    std::vector<SecondTracker> utrack;
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
    if (trackers.size() == 0) {  // the first frame met
        // initialize kalman trackers using first detections.
        for (size_t i = 0; i < high_conf_boxes.size(); i++) {
            if (!Contains(track_region, high_conf_boxes.at(i).box)) {
                LOGI("Trackingbox out of region\n");
                continue;
            }

            EigenKalmanTracker trk = EigenKalmanTracker(high_conf_boxes.at(i).box, config, next_id_++);
            trk.class_id           = high_conf_boxes.at(i).class_id;
            trk.confidence         = high_conf_boxes.at(i).confidence;
            trackers.push_back(trk);
        }
        return COSMO_NN_OK;
    }

    //  2 Remove predicted boxes --------------------------------------------------------------
    predictedBoxes.clear();
    for (auto it = trackers.begin(); it != trackers.end(); it++) {
        if (it->status != NEW) {
            int base     = std::min((*it).last_detect_position.height, (*it).last_detect_position.width);
            double bias  = Distance(it->last_detect_position, it->last_predict_position);
            bias         = bias < base ? bias : base;
            double range = config.dynamic_match * (base + bias);
            range        = range > config.max_range ? config.max_range : range;
            (*it).search_range = range;
        }
        TrackingBox pBox = (*it).Predict();
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
    for (unsigned int i = 0; i < trkNum; i++) {  // compute iou matrix as a distance matrix
        for (unsigned int j = 0; j < detNum; j++) {
            double iou_weight = 1 - GetDIOU(predictedBoxes.at(i), high_conf_boxes.at(j).box);

            double dis_weight = Distance(predictedBoxes.at(i), high_conf_boxes.at(j).box);

            double class_weight =
                config.classid_same
                    ? (trackers.at(i).class_id == high_conf_boxes.at(j).class_id ? 0 : 9999999)
                    : 0;

            double weight = iou_weight * dis_weight;
            weight        = weight < 0 ? 0 : weight;
            if (dis_weight > trackers.at(i).search_range || class_weight > 0)
                weight = 999999;

            costMatrix.at(i).at(j)    = weight;
            costDisMatrix.at(i).at(j) = dis_weight;
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

        if (costDisMatrix.at(i).at(assignm) < trackers.at(i).search_range) {
            trackers.at(i).status   = TRACKING;
            trackers.at(i).class_id = high_conf_boxes.at(assignm).class_id;
            trackers.at(i).Update(high_conf_boxes.at(assignm).box, high_conf_boxes.at(assignm).confidence);

            matchedPairs.push_back(std::pair<int, int>(i, assignm));
        } else {
            unmatchedTrajectories.insert(i);
        }
    }

    // 7 For unmatched boxes, create new tracker
    for (auto umd : unmatchedDetections) {
        if (!Contains(track_region, high_conf_boxes.at(umd).box)) {
            continue;
        }

        EigenKalmanTracker trk = EigenKalmanTracker(high_conf_boxes.at(umd).box, config, next_id_++);
        trk.class_id           = high_conf_boxes.at(umd).class_id;
        trk.confidence         = high_conf_boxes.at(umd).confidence;
        trackers.push_back(trk);
    }

    // Second matching pass
    for (auto umtrack : unmatchedTrajectories) {
        SecondTracker tmp_tracker = {trackers.at(umtrack), umtrack};
        utrack.push_back(tmp_tracker);
        secondPredictedBoxes.push_back(predictedBoxes.at(umtrack));
    }

    unsigned int utrkNum   = utrack.size();
    unsigned int lowDetNum = low_conf_boxes.size();

    costMatrix.clear();
    costMatrix.resize(utrkNum, std::vector<double>(lowDetNum, 0));
    costDisMatrix.clear();
    costDisMatrix.resize(utrkNum, std::vector<double>(lowDetNum, 0));
    for (unsigned int i = 0; i < utrkNum; i++) {  // compute iou matrix as a distance matrix
        for (unsigned int j = 0; j < lowDetNum; j++) {
            double iou_weight = 1 - GetDIOU(secondPredictedBoxes.at(i), low_conf_boxes.at(j).box);

            double dis_weight = Distance(secondPredictedBoxes.at(i), low_conf_boxes.at(j).box);

            double weight = iou_weight * dis_weight;
            weight        = weight < 0 ? 0 : weight;

            costMatrix.at(i).at(j)    = weight;
            costDisMatrix.at(i).at(j) = dis_weight;
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
                trackers.at(utrack.at(i).trackers_index).status     = LOSS;
                trackers.at(utrack.at(i).trackers_index).confidence = -1;

                unmatchedTrajectories.insert(utrack.at(i).trackers_index);
            }
        }
    }

    matchedPairs.clear();
    for (int i = 0; i < utrkNum; ++i) {
        int assignm = assignment.at(i);
        if (assignm == -1)  // pass over invalid values
            continue;

        if (costDisMatrix.at(i).at(assignm) < trackers.at(utrack.at(i).trackers_index).search_range) {
            trackers.at(utrack.at(i).trackers_index).status     = TRACKING;
            trackers.at(utrack.at(i).trackers_index).confidence = low_conf_boxes.at(assignm).confidence;
            trackers.at(utrack.at(i).trackers_index)
                .Update(low_conf_boxes.at(assignm).box, low_conf_boxes.at(assignm).confidence);

            matchedPairs.push_back(std::pair<int, int>(i, assignm));
        } else {
            trackers.at(utrack.at(i).trackers_index).status     = LOSS;
            trackers.at(utrack.at(i).trackers_index).confidence = -1;
            unmatchedTrajectories.insert(utrack.at(i).trackers_index);
        }
    }

    output.clear();
    for (auto it = trackers.begin(); it != trackers.end();) {
        bool contains = Contains(track_region, it->last_predict_position);
        if (it->time_since_last_update > config.max_age || it->low_thresh_count > config.thresh_low_timeout ||
            (!contains && it->status == LOSS)) {
            it = trackers.erase(it);
        } else {
            if (it->hits >= config.min_hits) {
                TrackingBox res;
                res.box      = it->status == TRACKING ? it->last_detect_position : it->last_predict_position;
                res.id       = it->id;
                res.class_id = it->class_id;
                res.confidence   = it->confidence;
                res.status       = it->status;
                res.motion_state = it->motion_state;

                output.push_back(res);
            }
            it++;
        }
    }

    return COSMO_NN_OK;
}

}  // namespace cosmo::nn