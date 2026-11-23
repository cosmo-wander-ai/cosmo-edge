#pragma once
// Candidate: SDK pool query must run before mapping under the device owner.
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>

#include "ax_sys_api.h"
#include "media/NativeVideoBuffer.h"
namespace cosmo::helmet_candidate {
struct HelmetPoolApi {
    virtual ~HelmetPoolApi()          = default;
    virtual AX_U64 Base(AX_BLK block) = 0;
    virtual AX_U64 Size(AX_BLK block) = 0;
};
struct HelmetSystemPoolApi final : HelmetPoolApi {
    AX_U64 Base(AX_BLK b) override {
        return AX_POOL_Handle2PhysAddr(b);
    }
    AX_U64 Size(AX_BLK b) override {
        return AX_POOL_Handle2BlkSize(b);
    }
};
class HelmetPoolProof;
inline std::optional<HelmetPoolProof> VerifyHelmetPoolBlock(
    media::NativeVideoBufferPtr, HelmetPoolApi&,
    media::NativeVideoColorSpace space = media::NativeVideoColorSpace::Bt709,
    media::NativeVideoColorRange range = media::NativeVideoColorRange::Limited);
class HelmetPoolProof final {
public:
    bool Matches(const media::NativeVideoBufferPtr& frame) const noexcept {
        if (!frame || frame.get() != frame_.get() || frame.owner_before(frame_) ||
            frame_.owner_before(frame) || !frame->frame_witness || !frame->frame_witness->Matches(*frame))
            return false;
        const auto* d = static_cast<const AX_VIDEO_FRAME_T*>(frame->backend_frame);
        return d && d->u32BlkId[0] == block_ && d->u32BlkId[0] == frame->planes[0].block_id &&
               d->u32BlkId[1] == frame->planes[1].block_id && d->u64PhyAddr[0] == y_ &&
               d->u64PhyAddr[1] == uv_ && frame->planes[0].physical_address == y_ &&
               frame->planes[1].physical_address == uv_ && frame->planes[0].bytes == 2211840 &&
               frame->planes[1].bytes == 1105920 && frame->color_space == space_ &&
               frame->color_range == range_;
    }

private:
    friend std::optional<HelmetPoolProof> VerifyHelmetPoolBlock(media::NativeVideoBufferPtr, HelmetPoolApi&,
                                                                media::NativeVideoColorSpace,
                                                                media::NativeVideoColorRange);
    HelmetPoolProof(media::NativeVideoBufferPtr f, AX_BLK b, AX_U64 y, AX_U64 uv,
                    media::NativeVideoColorSpace space, media::NativeVideoColorRange range)
        : frame_(std::move(f)), block_(b), y_(y), uv_(uv), space_(space), range_(range) {}
    media::NativeVideoBufferPtr frame_;
    AX_BLK block_;
    AX_U64 y_, uv_;
    media::NativeVideoColorSpace space_;
    media::NativeVideoColorRange range_;
};
inline std::optional<HelmetPoolProof> VerifyHelmetPoolBlock(media::NativeVideoBufferPtr f, HelmetPoolApi& api,
                                                            media::NativeVideoColorSpace space,
                                                            media::NativeVideoColorRange range) {
    if (!((space == media::NativeVideoColorSpace::Bt709 && range == media::NativeVideoColorRange::Limited) ||
          (space == media::NativeVideoColorSpace::Bt601 && range == media::NativeVideoColorRange::Full)))
        return {};
    if (!f || !f->Valid() || !f->frame_witness || !f->frame_witness->Matches(*f) || f->color_space != space ||
        f->color_range != range || f->planes[0].bytes != 2211840 || f->planes[1].bytes != 1105920)
        return {};
    const auto* d = static_cast<const AX_VIDEO_FRAME_T*>(f->backend_frame);
    if (!d || d->u32BlkId[0] == AX_INVALID_BLOCKID || d->u32BlkId[0] != f->planes[0].block_id ||
        d->u32BlkId[1] != f->planes[1].block_id || d->u64PhyAddr[0] != f->planes[0].physical_address ||
        d->u64PhyAddr[1] != f->planes[1].physical_address || d->u32FrameSize != f->bytes)
        return {};
    const auto y = d->u64PhyAddr[0], uv = d->u64PhyAddr[1];
    const auto base = api.Base(d->u32BlkId[0]), size = api.Size(d->u32BlkId[0]);
    if (!base || !size || base > std::numeric_limits<AX_U64>::max() - size || y < base || uv < base ||
        y - base > size || uv - base > size || 2211840 > size - (y - base) || 1105920 > size - (uv - base) ||
        y > std::numeric_limits<AX_U64>::max() - 2211840 || uv != y + 2211840)
        return {};
    return HelmetPoolProof(std::move(f), d->u32BlkId[0], y, uv, space, range);
}
}  // namespace cosmo::helmet_candidate
