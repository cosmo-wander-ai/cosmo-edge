#pragma once

// Private ScenarioBench-only CPU snapshot.  This type does not assert that an
// arbitrary mapped address belongs to a decoder: callers must first validate
// the source envelope, registration/lifecycle and channel binding, and pass an
// adapter which maps exactly the verified physical addresses.  It never
// retains a NativeVideoBuffer or NativeFrameWitness after successful copying.
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <limits>
#include <memory>
#include <new>
#include <string>
#include <utility>
#include <vector>

#include "media/NativeVideoBuffer.h"

namespace cosmo::nn::axera::benchmark {

struct SnapshotKey {
    std::string channel_id;
    std::uint64_t stream_generation{};
    std::uint64_t frame_index{};
    std::int64_t timestamp{};
};

class SnapshotMap {
public:
    virtual ~SnapshotMap()                                                              = default;
    virtual const std::uint8_t* Map(std::uint64_t physical, std::size_t bytes) noexcept = 0;
    virtual bool Unmap(const std::uint8_t* ptr, std::size_t bytes) noexcept             = 0;
};

enum class SnapshotError {
    None,
    InvalidFrame,
    InvalidKey,
    InvalidBudget,
    BudgetExhausted,
    MappingFailed,
    CopyFailed,
    UnmapFailed
};

class SnapshotBudget final {
public:
    explicit SnapshotBudget(std::size_t max_inflight) noexcept : max_inflight_(max_inflight) {}
    SnapshotBudget(const SnapshotBudget&)            = delete;
    SnapshotBudget& operator=(const SnapshotBudget&) = delete;
    [[nodiscard]] std::size_t Capacity() const noexcept {
        return max_inflight_;
    }
    [[nodiscard]] std::size_t InUse() const noexcept {
        return in_use_.load(std::memory_order_acquire);
    }

private:
    friend struct SnapshotFactory;
    friend class SnapshotBudgetLease;
    [[nodiscard]] bool TryAcquire() noexcept {
        auto value = in_use_.load(std::memory_order_relaxed);
        while (value < max_inflight_) {
            if (in_use_.compare_exchange_weak(value, value + 1, std::memory_order_acq_rel,
                                              std::memory_order_relaxed))
                return true;
        }
        return false;
    }
    void Release() noexcept {
        in_use_.fetch_sub(1, std::memory_order_release);
    }
    const std::size_t max_inflight_;
    std::atomic<std::size_t> in_use_{0};
};

class SnapshotBudgetLease final {
public:
    SnapshotBudgetLease(const SnapshotBudgetLease&)            = delete;
    SnapshotBudgetLease& operator=(const SnapshotBudgetLease&) = delete;
    ~SnapshotBudgetLease() noexcept {
        budget_->Release();
    }

private:
    friend struct SnapshotFactory;
    explicit SnapshotBudgetLease(std::shared_ptr<SnapshotBudget> budget) noexcept
        : budget_(std::move(budget)) {}
    const std::shared_ptr<SnapshotBudget> budget_;
};

class BenchHostNv12Snapshot final {
public:
    [[nodiscard]] const SnapshotKey& Key() const noexcept {
        return key_;
    }
    [[nodiscard]] std::uint64_t DecoderToken() const noexcept {
        return decoder_token_;
    }
    [[nodiscard]] std::int64_t SourcePts() const noexcept {
        return source_pts_;
    }
    [[nodiscard]] std::int64_t SourceArrivalTimestamp() const noexcept {
        return source_arrival_timestamp_;
    }
    [[nodiscard]] std::int64_t PresentationSteadyMs() const noexcept {
        return presentation_steady_ms_;
    }
    [[nodiscard]] int Width() const noexcept {
        return width_;
    }
    [[nodiscard]] int Height() const noexcept {
        return height_;
    }
    [[nodiscard]] std::size_t YStride() const noexcept {
        return y_stride_;
    }
    [[nodiscard]] std::size_t UvStride() const noexcept {
        return uv_stride_;
    }
    [[nodiscard]] media::NativeVideoBufferFormat Format() const noexcept {
        return format_;
    }
    [[nodiscard]] media::NativeVideoColorSpace ColorSpace() const noexcept {
        return color_space_;
    }
    [[nodiscard]] media::NativeVideoColorRange ColorRange() const noexcept {
        return color_range_;
    }
    [[nodiscard]] const std::vector<std::uint8_t>& Y() const noexcept {
        return y_;
    }
    [[nodiscard]] const std::vector<std::uint8_t>& Uv() const noexcept {
        return uv_;
    }

private:
    friend struct SnapshotFactory;
    BenchHostNv12Snapshot(std::unique_ptr<SnapshotBudgetLease> lease, SnapshotKey key,
                          const media::NativeVideoBuffer& s, std::vector<std::uint8_t> y,
                          std::vector<std::uint8_t> uv)
        : lease_(std::move(lease)),
          key_(std::move(key)),
          decoder_token_(s.decoder_token),
          source_pts_(s.source_pts),
          source_arrival_timestamp_(s.source_arrival_timestamp),
          presentation_steady_ms_(s.presentation_steady_ms),
          width_(s.width),
          height_(s.height),
          y_stride_(s.planes[0].stride),
          uv_stride_(s.planes[1].stride),
          format_(s.format),
          color_space_(s.color_space),
          color_range_(s.color_range),
          y_(std::move(y)),
          uv_(std::move(uv)) {}
    // Declared before the image vectors: reverse destruction order frees the
    // plane storage before releasing its budget slot.
    const std::unique_ptr<SnapshotBudgetLease> lease_;
    const SnapshotKey key_;
    const std::uint64_t decoder_token_;
    const std::int64_t source_pts_, source_arrival_timestamp_, presentation_steady_ms_;
    const int width_, height_;
    const std::size_t y_stride_, uv_stride_;
    const media::NativeVideoBufferFormat format_;
    const media::NativeVideoColorSpace color_space_;
    const media::NativeVideoColorRange color_range_;
    const std::vector<std::uint8_t> y_, uv_;
};

// If SDK unmapping fails the caller must poison its device execution state and
// retain this object, or retry unmap using the same adapter.  Destroying an
// unresolved quarantine intentionally preserves the frame owner for process
// lifetime, preventing premature VDEC release; the unreleased mapping itself
// necessarily remains until process exit. Allocation failure is fatal rather
// than silently returning the frame to the decoder while it is still mapped.
class SnapshotQuarantine final {
public:
    SnapshotQuarantine(const SnapshotQuarantine&)            = delete;
    SnapshotQuarantine& operator=(const SnapshotQuarantine&) = delete;
    ~SnapshotQuarantine() noexcept {
        if (Outstanding()) {
            auto* retained = new (std::nothrow) media::NativeVideoBufferPtr(std::move(source_));
            if (!retained)
                std::terminate();
        }
    }
    [[nodiscard]] bool Outstanding() const noexcept {
        return mapped_[0] || mapped_[1];
    }
    [[nodiscard]] bool RetryUnmap(SnapshotMap& mapper) noexcept {
        for (int i = 1; i >= 0; --i) {
            if (mapped_[i] && mapper.Unmap(mapped_[i], bytes_[i]))
                mapped_[i] = nullptr;
        }
        if (!Outstanding())
            source_.reset();
        return !Outstanding();
    }

private:
    friend struct SnapshotFactory;
    explicit SnapshotQuarantine(media::NativeVideoBufferPtr source) : source_(std::move(source)) {
        bytes_ = {source_->planes[0].bytes, source_->planes[1].bytes};
    }
    media::NativeVideoBufferPtr source_;
    std::array<const std::uint8_t*, 2> mapped_{};
    std::array<std::size_t, 2> bytes_{};
};

struct SnapshotResult {
    std::shared_ptr<const BenchHostNv12Snapshot> snapshot;
    SnapshotError error{SnapshotError::InvalidFrame};
    std::unique_ptr<SnapshotQuarantine> quarantine;
};

struct SnapshotFactory {
    static SnapshotResult CopyValidated(const media::NativeVideoBufferPtr& source, const SnapshotKey& key,
                                        SnapshotMap& mapper,
                                        const std::shared_ptr<SnapshotBudget>& budget) noexcept {
        SnapshotResult result;
        if (!source || !source->Valid() ||
            source->memory_kind != media::NativeVideoMemoryKind::AxeraPhysical ||
            source->format != media::NativeVideoBufferFormat::NV12 ||
            source->color_space != media::NativeVideoColorSpace::Bt709 ||
            source->color_range != media::NativeVideoColorRange::Limited || source->width != 1920 ||
            source->height != 1080 || source->width_stride != 2048 || source->height_stride != 1080 ||
            source->crop_x != 0 || source->crop_y != 0 || source->planes[0].stride != 2048 ||
            source->planes[1].stride != 2048 || source->planes[0].bytes != 2211840 ||
            source->planes[1].bytes != 1105920 || source->bytes < 2211840 + 1105920 ||
            !source->frame_witness || !source->frame_witness->Matches(*source))
            return result;
        for (const auto& plane : {source->planes[0], source->planes[1]}) {
            if (plane.physical_address > std::numeric_limits<std::uint64_t>::max() - plane.bytes)
                return result;
        }
        const auto& y  = source->planes[0];
        const auto& uv = source->planes[1];
        if (y.physical_address < uv.physical_address + uv.bytes &&
            uv.physical_address < y.physical_address + y.bytes)
            return result;
        if (key.channel_id.empty() || key.stream_generation != source->stream_generation ||
            key.frame_index != source->frame_index || key.timestamp != source->source_timestamp) {
            result.error = SnapshotError::InvalidKey;
            return result;
        }
        if (!budget || budget->Capacity() == 0) {
            result.error = SnapshotError::InvalidBudget;
            return result;
        }
        if (!budget->TryAcquire()) {
            result.error = SnapshotError::BudgetExhausted;
            return result;
        }
        bool reserved = true;
        std::unique_ptr<SnapshotQuarantine> mappings;
        try {
            auto lease = std::unique_ptr<SnapshotBudgetLease>(new SnapshotBudgetLease(budget));
            reserved   = false;
            mappings.reset(new SnapshotQuarantine(source));
            for (std::size_t i = 0; i < 2; ++i) {
                const auto ptr = mapper.Map(source->planes[i].physical_address, source->planes[i].bytes);
                if (!ptr || ptr == reinterpret_cast<const std::uint8_t*>(-1)) {
                    result.error = SnapshotError::MappingFailed;
                    if (!mappings->RetryUnmap(mapper)) {
                        result.error      = SnapshotError::UnmapFailed;
                        result.quarantine = std::move(mappings);
                    }
                    return result;
                }
                mappings->mapped_[i] = ptr;
            }
            std::vector<std::uint8_t> y_copy(y.bytes), uv_copy(uv.bytes);
            std::memcpy(y_copy.data(), mappings->mapped_[0], y_copy.size());
            std::memcpy(uv_copy.data(), mappings->mapped_[1], uv_copy.size());
            // Do not publish a snapshot if either mapping remains unresolved.
            if (!mappings->RetryUnmap(mapper)) {
                result.error      = SnapshotError::UnmapFailed;
                result.quarantine = std::move(mappings);
                return result;
            }
            result.snapshot = std::shared_ptr<const BenchHostNv12Snapshot>(new BenchHostNv12Snapshot(
                std::move(lease), key, *source, std::move(y_copy), std::move(uv_copy)));
            result.error    = SnapshotError::None;
            return result;
        } catch (...) {
            if (reserved)
                budget->Release();
            result.error = SnapshotError::CopyFailed;
            if (mappings && !mappings->RetryUnmap(mapper)) {
                result.error      = SnapshotError::UnmapFailed;
                result.quarantine = std::move(mappings);
            }
            return result;
        }
    }
};

inline SnapshotResult CopyValidated(const media::NativeVideoBufferPtr& source, const SnapshotKey& key,
                                    SnapshotMap& mapper,
                                    const std::shared_ptr<SnapshotBudget>& budget) noexcept {
    return SnapshotFactory::CopyValidated(source, key, mapper, budget);
}
}  // namespace cosmo::nn::axera::benchmark
