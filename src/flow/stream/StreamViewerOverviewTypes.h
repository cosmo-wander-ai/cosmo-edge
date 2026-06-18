// StreamViewerOverviewTypes.h — Private type definitions for StreamViewerOverview.
// Shared across StreamViewerOverview.cc, StreamViewerLiveData.cc, StreamViewerDraw.cc.

#pragma once

#include <map>
#include <vector>

#include "flow/stream/StreamViewerOverview.h"
#include "media/Color.h"

struct cosmo::StreamViewerOverview::AttrLines {
    cosmo::media::Color color{200, 200, 200};
    int lineWidth{2};
    std::vector<std::pair<cosmo::util::Point, cosmo::util::Point>> lines;
};

struct cosmo::StreamViewerOverview::OverviewInfo {
    std::vector<cosmo::StreamOverviewText> texts;
    std::map<uint32_t, cosmo::StreamViewerOverview::AttrLines> attrLines;
};
