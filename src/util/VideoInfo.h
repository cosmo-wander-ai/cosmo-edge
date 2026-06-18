#pragma once

#include <cstddef>

namespace cosmo::media {

constexpr int kQueueSizeCoefficient = 1;  // Queue length coefficient

constexpr int kPictureMaxWidth  = 4096;
constexpr int kPictureMaxHeight = 4096;
constexpr int kPictureMinWidth  = 40;
constexpr int kPictureMinHeight = 40;

constexpr bool IsValidPicture(int width, int height) {
    return (width >= kPictureMinWidth) && (height >= kPictureMinHeight) &&
           ((width * height) <= (kPictureMaxWidth * kPictureMaxHeight));
}

constexpr int kVideo4KWidth  = 7680;
constexpr int kVideo4KHeight = 4320;

constexpr int kVideoMaxWidth  = kVideo4KWidth;
constexpr int kVideoMaxHeight = kVideo4KHeight;

constexpr int kVideoFrameMaxSize = kVideoMaxWidth * kVideoMaxHeight * 3;

constexpr int kVideoDefaultWidth       = 1920;
constexpr int kVideoDefaultHeight      = 1088;
constexpr int kVideoDefaultHeightAlign = 1088;

constexpr int kVideoMinWidth  = 1280;
constexpr int kVideoMinHeight = 720;

constexpr bool IsValidVideoWidth(int value) {
    return (value >= kVideoMinWidth) && (value <= kVideoMaxWidth);
}

constexpr bool IsValidVideoHeight(int value) {
    return (value >= kVideoMinHeight) && (value <= kVideoMaxHeight);
}

constexpr int kVideoInfoMaxDuration = 10 * 1000;  // 10 seconds

constexpr int kTimestampDiff = 1000;  // Video timestamp diff

constexpr bool IsValidVideoResolution(int width, int height) {
    return (width * height >= kVideoMinWidth * kVideoMinHeight) &&
           (width * height <= kVideoMaxWidth * kVideoMaxHeight);
}

constexpr double kFpsCtrlMaxFps = 1000000.0;  // Used when external FPS setting < 0

}  // namespace cosmo::media
