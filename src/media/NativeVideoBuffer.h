#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace cosmo::media {

enum class NativeVideoBufferFormat {
    Unknown = 0,
    I420,
    NV12,
    NV21,
};

enum class NativeVideoColorSpace {
    Unspecified = 0,
    Bt601,
    Bt709,
    Bt2020,
    Unsupported,
};

enum class NativeVideoColorRange {
    Unspecified = 0,
    Limited,
    Full,
};

enum class NativeVideoMemoryKind { DmaBuf = 0, AxeraPhysical };

struct NativeVideoBuffer;
class VideoDecoderAxera;

// Immutable attestation of one AX decoder-owned output. Only the AX decoder
// may construct it after resolving the exact output token against verified AU
// evidence. It is never synthesized from caller-supplied color enums.
class NativeFrameWitness final {
public:
    [[nodiscard]] bool Matches(const NativeVideoBuffer& frame) const;
    [[nodiscard]] uint64_t Generation() const {
        return generation_;
    }
    [[nodiscard]] uint64_t Token() const {
        return token_;
    }
    [[nodiscard]] uint64_t FrameIndex() const {
        return frame_index_;
    }
    [[nodiscard]] int64_t SourcePts() const {
        return source_pts_;
    }
    [[nodiscard]] int64_t SourceTimestamp() const {
        return source_timestamp_;
    }
    [[nodiscard]] int64_t SourceArrivalTimestamp() const {
        return source_arrival_timestamp_;
    }
    [[nodiscard]] int64_t PresentationSteadyMs() const {
        return presentation_steady_ms_;
    }

private:
    friend class VideoDecoderAxera;
    NativeFrameWitness(const NativeVideoBuffer& frame, uint64_t generation, uint64_t token,
                       int64_t source_pts);

    std::shared_ptr<void> owner_;
    const void* backend_frame_{nullptr};
    std::array<uint64_t, 3> physical_{};
    std::array<size_t, 3> plane_bytes_{};
    std::array<size_t, 3> plane_stride_{};
    std::array<uint32_t, 3> block_ids_{};
    size_t bytes_{0};
    size_t plane_count_{0};
    int fd_{-1}, width_{0}, height_{0}, width_stride_{0}, height_stride_{0}, crop_x_{0}, crop_y_{0};
    NativeVideoBufferFormat format_{NativeVideoBufferFormat::Unknown};
    NativeVideoColorSpace space_{NativeVideoColorSpace::Unspecified};
    NativeVideoColorRange range_{NativeVideoColorRange::Unspecified};
    uint64_t generation_{0}, token_{0}, frame_index_{0};
    int64_t source_pts_{0};
    int64_t source_timestamp_{0};
    int64_t source_arrival_timestamp_{0}, presentation_steady_ms_{0};
};

struct NativeVideoPlane {
    uint64_t physical_address{0};
    size_t bytes{0};
    size_t stride{0};
    uint32_t block_id{0};
};

/// Optional, backend-owned image buffer presented to a hardware consumer.
///
/// The file descriptor is borrowed from owner. Keeping this object alive keeps
/// the underlying decoder buffer referenced; consumers must not close fd.
struct NativeVideoBuffer {
    // Default preserves the existing Rockchip DMA-BUF contract.
    NativeVideoMemoryKind memory_kind{NativeVideoMemoryKind::DmaBuf};
    int fd{-1};
    size_t bytes{0};
    int width{0};
    int height{0};
    int width_stride{0};
    int height_stride{0};
    NativeVideoBufferFormat format{NativeVideoBufferFormat::Unknown};
    NativeVideoColorSpace color_space{NativeVideoColorSpace::Unspecified};
    NativeVideoColorRange color_range{NativeVideoColorRange::Unspecified};
    std::array<NativeVideoPlane, 3> planes{};
    size_t plane_count{0};
    int crop_x{0};
    int crop_y{0};
    // Borrowed backend-specific descriptor; valid only while owner is retained.
    const void* backend_frame{nullptr};
    uint64_t frame_index{0};
    uint64_t stream_generation{0};
    uint64_t decoder_token{0};
    int64_t source_pts{0};
    int64_t source_timestamp{0};  // PTS-derived presentation wall time
    int64_t source_arrival_timestamp{0};
    int64_t presentation_steady_ms{0};
    std::shared_ptr<void> owner;
    std::shared_ptr<const NativeFrameWitness> frame_witness;

    [[nodiscard]] bool Valid() const {
        const bool common = bytes > 0 && width > 0 && height > 0 && width_stride >= width &&
                            height_stride >= height && format != NativeVideoBufferFormat::Unknown && owner;
        if (!common)
            return false;
        if (memory_kind == NativeVideoMemoryKind::DmaBuf)
            return fd >= 0;
        if (memory_kind == NativeVideoMemoryKind::AxeraPhysical)
            return fd < 0 && plane_count == 2 && backend_frame && crop_x >= 0 && crop_y >= 0 &&
                   planes[0].physical_address && planes[1].physical_address && planes[0].bytes &&
                   planes[1].bytes && planes[0].stride && planes[1].stride;
        return false;
    }
};

inline NativeFrameWitness::NativeFrameWitness(const NativeVideoBuffer& frame, uint64_t generation,
                                              uint64_t token, int64_t source_pts)
    : owner_(frame.owner),
      backend_frame_(frame.backend_frame),
      bytes_(frame.bytes),
      plane_count_(frame.plane_count),
      fd_(frame.fd),
      width_(frame.width),
      height_(frame.height),
      width_stride_(frame.width_stride),
      height_stride_(frame.height_stride),
      crop_x_(frame.crop_x),
      crop_y_(frame.crop_y),
      format_(frame.format),
      space_(frame.color_space),
      range_(frame.color_range),
      generation_(generation),
      token_(token),
      frame_index_(frame.frame_index),
      source_pts_(source_pts),
      source_timestamp_(frame.source_timestamp),
      source_arrival_timestamp_(frame.source_arrival_timestamp),
      presentation_steady_ms_(frame.presentation_steady_ms) {
    for (size_t i = 0; i < frame.planes.size(); ++i) {
        physical_[i]     = frame.planes[i].physical_address;
        plane_bytes_[i]  = frame.planes[i].bytes;
        plane_stride_[i] = frame.planes[i].stride;
        block_ids_[i]    = frame.planes[i].block_id;
    }
}

inline bool NativeFrameWitness::Matches(const NativeVideoBuffer& frame) const {
    if (frame.memory_kind != NativeVideoMemoryKind::AxeraPhysical || !owner_ || !frame.owner ||
        owner_.get() != frame.owner.get() || owner_.owner_before(frame.owner) ||
        frame.owner.owner_before(owner_) || !backend_frame_ || backend_frame_ != frame.backend_frame ||
        frame.fd != fd_ || frame.bytes != bytes_ || frame.plane_count != plane_count_ ||
        frame.width != width_ || frame.height != height_ || frame.width_stride != width_stride_ ||
        frame.height_stride != height_stride_ || frame.crop_x != crop_x_ || frame.crop_y != crop_y_ ||
        frame.format != format_ || frame.color_space != space_ || frame.color_range != range_ ||
        frame.frame_index != frame_index_ || frame.stream_generation != generation_ ||
        frame.decoder_token != token_ || frame.source_pts != source_pts_ ||
        frame.source_timestamp != source_timestamp_ ||
        frame.source_arrival_timestamp != source_arrival_timestamp_ ||
        frame.presentation_steady_ms != presentation_steady_ms_ || generation_ == 0 || token_ == 0 ||
        space_ == NativeVideoColorSpace::Unspecified || range_ == NativeVideoColorRange::Unspecified)
        return false;
    for (size_t i = 0; i < frame.planes.size(); ++i)
        if (physical_[i] != frame.planes[i].physical_address || plane_bytes_[i] != frame.planes[i].bytes ||
            plane_stride_[i] != frame.planes[i].stride || block_ids_[i] != frame.planes[i].block_id)
            return false;
    return true;
}

using NativeVideoBufferPtr = std::shared_ptr<const NativeVideoBuffer>;

}  // namespace cosmo::media
