// PixelFormatUtils — Pixel Format Utils implementation.

#include "media/PixelFormatUtils.h"

#include <algorithm>
#include <cstdint>
#include <limits>

#include "util/VideoInfo.h"

namespace cosmo {
namespace media {

    namespace {

        int GetWidthAlignment(PixelFormat format) {
            switch (format) {
                case PixelFormat::PIXEL_YUYV:
                case PixelFormat::PIXEL_YVYU:
                case PixelFormat::PIXEL_UYVY:
                case PixelFormat::PIXEL_I420:
                case PixelFormat::PIXEL_NV12:
                case PixelFormat::PIXEL_NV21:
                    return 2;
                default:
                    return 1;
            }
        }

        int GetHeightAlignment(PixelFormat format) {
            switch (format) {
                case PixelFormat::PIXEL_I420:
                case PixelFormat::PIXEL_NV12:
                case PixelFormat::PIXEL_NV21:
                    return 2;
                default:
                    return 1;
            }
        }

        bool ExpandAndAlignSpan(int64_t* start, int64_t* end, int64_t limit, int min_dimension,
                                int max_dimension, int alignment) {
            const auto current_length = *end - *start;
            if (current_length <= 0) {
                return false;
            }

            int64_t target_length = std::max<int64_t>(current_length, min_dimension);
            const auto remainder  = target_length % alignment;
            if (remainder != 0) {
                target_length += alignment - remainder;
            }
            if (target_length > max_dimension || target_length > limit) {
                return false;
            }

            auto remaining_growth          = target_length - current_length;
            const auto desired_left_growth = remaining_growth / 2;
            const auto grow_left           = std::min(desired_left_growth, *start);
            *start -= grow_left;
            remaining_growth -= grow_left;

            const auto grow_right = std::min(remaining_growth, limit - *end);
            *end += grow_right;
            remaining_growth -= grow_right;

            const auto extra_left_growth = std::min(remaining_growth, *start);
            *start -= extra_left_growth;
            remaining_growth -= extra_left_growth;
            return remaining_growth == 0;
        }

    }  // namespace

    PixelBaseType PixelFormatUtils::PixelFormatBaseType(PixelFormat format) {
        switch (format) {
            case PixelFormat::PIXEL_BGR32F:
            case PixelFormat::PIXEL_BGRA32F:
            case PixelFormat::PIXEL_RGB32F:
            case PixelFormat::PIXEL_RGBA32F:
                return PixelBaseType::PIXEL_FLOAT;
            default:
                break;
        }

        return PixelBaseType::PIXEL_UINT8;
    }

    size_t PixelFormatUtils::PixelFormatChannels(PixelFormat f) {
        switch (f) {
            case PixelFormat::PIXEL_BGR8:
            case PixelFormat::PIXEL_BGR32F:
            case PixelFormat::PIXEL_RGB8:
            case PixelFormat::PIXEL_RGB32F:
                return 3;

            case PixelFormat::PIXEL_YUYV:
            case PixelFormat::PIXEL_YVYU:
            case PixelFormat::PIXEL_UYVY:
            case PixelFormat::PIXEL_I420:
            case PixelFormat::PIXEL_NV12:
            case PixelFormat::PIXEL_NV21:
                return 3;

            case PixelFormat::PIXEL_RGBA8:
            case PixelFormat::PIXEL_RGBA32F:
            case PixelFormat::PIXEL_BGRA8:
            case PixelFormat::PIXEL_BGRA32F:
                return 4;
            default:
                break;
        }

        return 0;
    }

    size_t PixelFormatUtils::PixelFormatDepth(PixelFormat f) {
        switch (f) {
            case PixelFormat::PIXEL_BGR8:
            case PixelFormat::PIXEL_RGB8:
                return sizeof(uint8_t) * 24;

            case PixelFormat::PIXEL_RGBA8:
            case PixelFormat::PIXEL_BGRA8:
                return sizeof(uint8_t) * 32;

            case PixelFormat::PIXEL_BGR32F:
            case PixelFormat::PIXEL_RGB32F:
                return sizeof(float) * 24;

            case PixelFormat::PIXEL_RGBA32F:
            case PixelFormat::PIXEL_BGRA32F:
                return sizeof(float) * 32;

            case PixelFormat::PIXEL_I420:
            case PixelFormat::PIXEL_NV12:
            case PixelFormat::PIXEL_NV21:
                return sizeof(uint8_t) * 12;

            case PixelFormat::PIXEL_YUYV:
            case PixelFormat::PIXEL_YVYU:
            case PixelFormat::PIXEL_UYVY:
                return sizeof(uint8_t) * 16;
            default:
                break;
        }

        return 0;
    }

    std::optional<size_t> PixelFormatUtils::CalculateFrameSize(int width, int height, PixelFormat format) {
        if (width <= 0 || height <= 0) {
            return std::nullopt;
        }

        switch (format) {
            case PixelFormat::PIXEL_I420:
            case PixelFormat::PIXEL_NV12:
            case PixelFormat::PIXEL_NV21:
                if ((width % 2) != 0 || (height % 2) != 0) {
                    return std::nullopt;
                }
                break;
            case PixelFormat::PIXEL_YUYV:
            case PixelFormat::PIXEL_YVYU:
            case PixelFormat::PIXEL_UYVY:
                if ((width % 2) != 0) {
                    return std::nullopt;
                }
                break;
            default:
                break;
        }

        const auto depth = PixelFormatDepth(format);
        if (depth == 0) {
            return std::nullopt;
        }

        const auto width_size  = static_cast<size_t>(width);
        const auto height_size = static_cast<size_t>(height);
        if (width_size > std::numeric_limits<size_t>::max() / height_size) {
            return std::nullopt;
        }
        const auto pixel_count = width_size * height_size;

        if (pixel_count > std::numeric_limits<size_t>::max() / depth) {
            return std::nullopt;
        }
        const auto bit_count          = pixel_count * depth;
        constexpr size_t kBitsPerByte = 8;
        if ((bit_count % kBitsPerByte) != 0) {
            return std::nullopt;
        }

        const auto frame_size = bit_count / kBitsPerByte;
        if (frame_size == 0 || frame_size > static_cast<size_t>(kVideoFrameMaxSize)) {
            return std::nullopt;
        }
        return frame_size;
    }

    std::optional<util::Box> PixelFormatUtils::NormalizeCropRoi(int source_width, int source_height,
                                                                PixelFormat format,
                                                                const util::Box& requested_roi,
                                                                int min_dimension, int max_dimension) {
        if (!CalculateFrameSize(source_width, source_height, format) || min_dimension <= 0 ||
            max_dimension < min_dimension || requested_roi.width <= 0 || requested_roi.height <= 0 ||
            requested_roi.width > max_dimension || requested_roi.height > max_dimension) {
            return std::nullopt;
        }

        const auto source_width_i64  = static_cast<int64_t>(source_width);
        const auto source_height_i64 = static_cast<int64_t>(source_height);
        int64_t left                 = std::max<int64_t>(0, requested_roi.x);
        int64_t top                  = std::max<int64_t>(0, requested_roi.y);
        int64_t right  = std::min<int64_t>(source_width_i64, static_cast<int64_t>(requested_roi.x) +
                                                                 static_cast<int64_t>(requested_roi.width));
        int64_t bottom = std::min<int64_t>(source_height_i64, static_cast<int64_t>(requested_roi.y) +
                                                                  static_cast<int64_t>(requested_roi.height));
        if (left >= right || top >= bottom) {
            return std::nullopt;
        }

        if (!ExpandAndAlignSpan(&left, &right, source_width_i64, min_dimension, max_dimension,
                                GetWidthAlignment(format)) ||
            !ExpandAndAlignSpan(&top, &bottom, source_height_i64, min_dimension, max_dimension,
                                GetHeightAlignment(format))) {
            return std::nullopt;
        }

        util::Box normalized_roi{static_cast<int>(left), static_cast<int>(top),
                                 static_cast<int>(right - left), static_cast<int>(bottom - top)};
        if (!CalculateFrameSize(normalized_roi.width, normalized_roi.height, format)) {
            return std::nullopt;
        }
        return normalized_roi;
    }

    bool PixelFormatUtils::PixelFormatIsRGB(PixelFormat f) {
        if (f >= PixelFormat::PIXEL_RGB8 && f <= PixelFormat::PIXEL_RGBA32F)
            return true;

        return false;
    }

    bool PixelFormatUtils::PixelFormatIsBGR(PixelFormat f) {
        if (f >= PixelFormat::PIXEL_BGR8 && f <= PixelFormat::PIXEL_BGRA32F)
            return true;

        return false;
    }

    bool PixelFormatUtils::PixelFormatIsYUV(PixelFormat f) {
        if (f >= PixelFormat::PIXEL_YUYV && f <= PixelFormat::PIXEL_NV21)
            return true;

        return false;
    }

}  // namespace media
}  // namespace cosmo
