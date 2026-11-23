#pragma once

// Reader for the experimental detections_v1 metadata contract. It does not
// decode model heads, perform NMS, access a device, or transform coordinates.
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>
#include <stdexcept>
#include <utility>
#include <vector>

namespace cosmo::nn::axera::detections_v1 {

static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559,
              "detections_v1 requires IEEE 754 binary32");

enum class Error {
    None,
    InvalidContract,
    InvalidMetadata,
    InvalidBuffer,
    InvalidValue,
    Overflow,
    InvalidInput,
    AllocationFailure,
};

struct Contract {
    std::size_t capacity;
    float confidence;
    float iou;
    std::size_t class_count;
};

// Shape, accessible byte count and memory layout must come from a verified
// runtime contract, not from the output itself. The caller must ensure device
// completion, visibility and buffer lifetime before providing a host view.
// This ABI only accepts packed native-endian FLOAT32.
struct TensorView {
    const void* data;
    std::size_t byte_length;
    std::array<std::size_t, 3> dimensions;
    std::size_t rank;
    bool float32;
    bool packed;
};

struct Detection {
    float cx, cy, width, height, score;
    std::int32_t class_id;
};

struct Result {
    Error error = Error::None;
    std::vector<Detection> detections;
};

inline bool SameBits(float a, float b) noexcept {
    std::uint32_t left, right;
    std::memcpy(&left, &a, sizeof(left));
    std::memcpy(&right, &b, sizeof(right));
    return left == right;
}

inline Result Parse(const Contract& expected, const TensorView& tensor) {
    auto fail = [](Error error) { return Result{error, {}}; };
    // Match the experimental graph generator, not an invented device limit.
    constexpr std::size_t kMaxGraphCapacity = 1000;
    constexpr std::size_t kColumns          = 6;
    if (expected.capacity == 0 || expected.capacity > kMaxGraphCapacity ||
        !std::isfinite(expected.confidence) || !std::isfinite(expected.iou) || expected.confidence <= 0.f ||
        expected.confidence > 1.f || expected.iou < 0.f || expected.iou >= 1.f ||
        std::signbit(expected.confidence) || expected.class_count == 0 ||
        expected.class_count > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()))
        return fail(Error::InvalidContract);

    if (tensor.rank != 3 || !tensor.float32 || !tensor.packed || tensor.dimensions[0] != 1 ||
        tensor.dimensions[1] != expected.capacity + 1 || tensor.dimensions[2] != kColumns)
        return fail(Error::InvalidMetadata);
    const auto rows = expected.capacity + 1;
    if (rows > std::numeric_limits<std::size_t>::max() / kColumns ||
        rows * kColumns > std::numeric_limits<std::size_t>::max() / sizeof(float) ||
        rows * kColumns * sizeof(float) >
            static_cast<std::size_t>(std::numeric_limits<std::ptrdiff_t>::max()))
        return fail(Error::InvalidMetadata);
    const auto required_bytes = rows * kColumns * sizeof(float);
    // A padded/strided device output must be converted into a separately
    // verified packed metadata view by the caller before using this parser.
    if (!tensor.data || tensor.byte_length != required_bytes)
        return fail(Error::InvalidBuffer);

    const auto* bytes = static_cast<const unsigned char*>(tensor.data);
    auto value        = [&](std::size_t index) {
        float out;
        std::memcpy(&out, bytes + index * sizeof(float), sizeof(out));
        return out;
    };
    // Validate every element before examining the flags, including unused rows.
    for (std::size_t i = 0; i < rows * kColumns; ++i)
        if (!std::isfinite(value(i)))
            return fail(Error::InvalidValue);
    const float count_value = value(0), overflow = value(1), invalid = value(2);
    if (std::signbit(count_value) || std::trunc(count_value) != count_value ||
        static_cast<double>(count_value) > static_cast<double>(expected.capacity) ||
        !(SameBits(overflow, 0.f) || SameBits(overflow, 1.f)) ||
        !(SameBits(invalid, 0.f) || SameBits(invalid, 1.f)) || !SameBits(value(3), 1.f) ||
        !SameBits(value(4), expected.confidence) || !SameBits(value(5), expected.iou))
        return fail(Error::InvalidValue);
    // Simultaneous graph error flags are contradictory: do not classify an
    // invalid frame as a capacity-only event.
    if (SameBits(overflow, 1.f) && SameBits(invalid, 1.f))
        return fail(Error::InvalidValue);
    if (SameBits(overflow, 1.f))
        return fail(Error::Overflow);
    if (SameBits(invalid, 1.f))
        return fail(Error::InvalidInput);
    const auto count = static_cast<std::size_t>(count_value);
    try {
        std::vector<Detection> staged;
        staged.reserve(count);
        for (std::size_t row = 1; row <= expected.capacity; ++row) {
            const auto offset = row * kColumns;
            if (row > count) {
                for (std::size_t col = 0; col < kColumns; ++col)
                    if (value(offset + col) != 0.f)
                        return fail(Error::InvalidValue);
                continue;
            }
            const float cx = value(offset), cy = value(offset + 1), width = value(offset + 2),
                        height = value(offset + 3), score = value(offset + 4), cls = value(offset + 5);
            if (width < 0.f || height < 0.f || score < expected.confidence || score > 1.f ||
                std::signbit(cls) || std::trunc(cls) != cls ||
                static_cast<double>(cls) >= static_cast<double>(expected.class_count) ||
                !std::isfinite(cx - width * 0.5f) || !std::isfinite(cx + width * 0.5f) ||
                !std::isfinite(cy - height * 0.5f) || !std::isfinite(cy + height * 0.5f))
                return fail(Error::InvalidValue);
            staged.push_back({cx, cy, width, height, score, static_cast<std::int32_t>(cls)});
        }
        return {Error::None, std::move(staged)};
    } catch (const std::bad_alloc&) {
        return fail(Error::AllocationFailure);
    } catch (const std::length_error&) {
        return fail(Error::AllocationFailure);
    }
}

}  // namespace cosmo::nn::axera::detections_v1
