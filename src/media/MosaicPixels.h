#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "media/VideoFrame.h"
#include "util/Rect.h"

namespace cosmo::media {

// Reject inconsistent frame metadata before copying or accessing pixel memory.
bool IsMosaicFrameValid(const VideoFramePtr& frame);

// Process a private, contiguous host buffer. Validates every region before any
// write. Adds a 5% margin (at least two pixels) on each side and expands chroma
// coverage outward. Invalid/outside regions and unsupported layouts fail closed.
bool ApplyMosaicToPixels(uint8_t* pixels, size_t size, int width, int height, PixelFormat format,
                         const std::vector<util::Box>& boxes, int strength);

}  // namespace cosmo::media
