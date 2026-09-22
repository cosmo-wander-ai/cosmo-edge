#include "media/MosaicPixels.h"

#include <algorithm>
#include <array>
#include <limits>

#include "media/PixelFormatUtils.h"

namespace cosmo::media {
namespace {

    bool IsSupported(PixelFormat format) {
        return format == PixelFormat::PIXEL_RGB8 || format == PixelFormat::PIXEL_BGR8 ||
               format == PixelFormat::PIXEL_I420 || format == PixelFormat::PIXEL_NV12 ||
               format == PixelFormat::PIXEL_NV21;
    }

    struct Region {
        int left;
        int top;
        int right;
        int bottom;
        int block;
    };

    bool NormalizeRegion(const util::Box& box, int width, int height, bool subsampled, int strength,
                         Region& region) {
        if (box.width <= 0 || box.height <= 0) {
            return false;
        }
        // Wider arithmetic also handles rectangles crossing INT_MAX safely.
        const int64_t right  = static_cast<int64_t>(box.x) + box.width;
        const int64_t bottom = static_cast<int64_t>(box.y) + box.height;
        if (box.x >= width || box.y >= height || right <= 0 || bottom <= 0) {
            return false;
        }
        const int64_t margin_x = std::max<int64_t>(2, (static_cast<int64_t>(box.width) + 19) / 20);
        const int64_t margin_y = std::max<int64_t>(2, (static_cast<int64_t>(box.height) + 19) / 20);
        region.left   = static_cast<int>(std::max<int64_t>(0, static_cast<int64_t>(box.x) - margin_x));
        region.top    = static_cast<int>(std::max<int64_t>(0, static_cast<int64_t>(box.y) - margin_y));
        region.right  = static_cast<int>(std::min<int64_t>(width, right + margin_x));
        region.bottom = static_cast<int>(std::min<int64_t>(height, bottom + margin_y));
        if (subsampled) {
            region.left &= ~1;
            region.top &= ~1;
            region.right  = (region.right + 1) & ~1;
            region.bottom = (region.bottom + 1) & ~1;
        }
        constexpr std::array<int, 3> min_blocks{8, 12, 16};
        constexpr std::array<int, 3> cell_counts{16, 10, 6};
        const int extent = std::max(region.right - region.left, region.bottom - region.top);
        const int cells  = cell_counts[strength - 1];
        region.block     = std::max(min_blocks[strength - 1], (extent + cells - 1) / cells);
        if (subsampled) {
            region.block = (region.block + 1) & ~1;
        }
        return true;
    }

    void MosaicPlane(uint8_t* pixels, size_t stride, int channels, const Region& region) {
        for (int top = region.top; top < region.bottom; top += region.block) {
            const int bottom = std::min(top + region.block, region.bottom);
            for (int left = region.left; left < region.right; left += region.block) {
                const int right = std::min(left + region.block, region.right);
                std::array<uint64_t, 3> totals{};
                for (int y = top; y < bottom; ++y) {
                    const auto* row = pixels + static_cast<size_t>(y) * stride;
                    for (int x = left; x < right; ++x) {
                        for (int channel = 0; channel < channels; ++channel) {
                            totals[channel] += row[static_cast<size_t>(x) * channels + channel];
                        }
                    }
                }
                const auto count = static_cast<uint64_t>(right - left) * (bottom - top);
                std::array<uint8_t, 3> averages{};
                for (int channel = 0; channel < channels; ++channel) {
                    averages[channel] = static_cast<uint8_t>(totals[channel] / count);
                }
                for (int y = top; y < bottom; ++y) {
                    auto* row = pixels + static_cast<size_t>(y) * stride;
                    for (int x = left; x < right; ++x) {
                        for (int channel = 0; channel < channels; ++channel) {
                            row[static_cast<size_t>(x) * channels + channel] = averages[channel];
                        }
                    }
                }
            }
        }
    }

}  // namespace

bool IsMosaicFrameValid(const VideoFramePtr& frame) {
    if (!frame || !frame->Active() || !frame->GetData() || !IsSupported(frame->GetPixelFormat()) ||
        frame->GetWidth() > static_cast<size_t>(std::numeric_limits<int>::max()) ||
        frame->GetHeight() > static_cast<size_t>(std::numeric_limits<int>::max())) {
        return false;
    }
    const auto size = PixelFormatUtils::CalculateFrameSize(
        static_cast<int>(frame->GetWidth()), static_cast<int>(frame->GetHeight()), frame->GetPixelFormat());
    return size && *size == frame->GetSize();
}

bool ApplyMosaicToPixels(uint8_t* pixels, size_t size, int width, int height, PixelFormat format,
                         const std::vector<util::Box>& boxes, int strength) {
    if (!pixels || !IsSupported(format) || strength < 1 || strength > 3) {
        return false;
    }
    const auto expected_size = PixelFormatUtils::CalculateFrameSize(width, height, format);
    if (!expected_size || *expected_size != size) {
        return false;
    }
    const bool packed = format == PixelFormat::PIXEL_RGB8 || format == PixelFormat::PIXEL_BGR8;
    std::vector<Region> regions;
    regions.reserve(boxes.size());
    for (const auto& box : boxes) {
        Region region{};
        if (!NormalizeRegion(box, width, height, !packed, strength, region)) {
            return false;
        }
        regions.push_back(region);
    }
    for (const auto& region : regions) {
        if (packed) {
            MosaicPlane(pixels, static_cast<size_t>(width) * 3, 3, region);
            continue;
        }
        MosaicPlane(pixels, static_cast<size_t>(width), 1, region);
        const Region chroma{region.left / 2, region.top / 2, region.right / 2, region.bottom / 2,
                            region.block / 2};
        const auto luma_size = static_cast<size_t>(width) * height;
        if (format == PixelFormat::PIXEL_I420) {
            MosaicPlane(pixels + luma_size, static_cast<size_t>(width) / 2, 1, chroma);
            MosaicPlane(pixels + luma_size + luma_size / 4, static_cast<size_t>(width) / 2, 1, chroma);
        } else {
            MosaicPlane(pixels + luma_size, static_cast<size_t>(width), 2, chroma);
        }
    }
    return true;
}

}  // namespace cosmo::media
