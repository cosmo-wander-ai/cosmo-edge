// Test-only SDK fake extracted from test_axera_vdec_mock.cc.
#include <sys/wait.h>
#include <unistd.h>

#include <atomic>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <limits>
#include <map>
#include <thread>
#include <type_traits>
#include <vector>

#include "ax_sys_api.h"
#include "ax_vdec_api.h"
#include "media/AxeraNativeVideoBuffer.h"
#include "media/VideoDecoderAxera.h"
#include "util/Log.h"

namespace {
struct FakeVdec {
    int allocations{0};
    int frees{0};
    int unsafe_frees{0};
    int init{0};
    int deinit{0};
    bool fail_send{false};
    bool fail_destroy{false};
    bool fail_release{false};
    bool fail_stop{false};
    bool fail_disable{false};
    bool fail_get{false};
    bool distinct_frames{false};
    int failure_mode{0};
    int released_frames{0};
    std::vector<AX_U64> released_pts;
    int mmap_calls{0};
    AX_VDEC_GRP next_group{1};
    std::map<AX_VDEC_GRP, std::deque<AX_U64>> output;
    std::map<AX_U64, const AX_U8*> in_flight;
    std::map<AX_U64, std::vector<AX_U8>> frame_y;
    std::map<AX_U64, std::vector<AX_U8>> frame_uv;
    std::vector<AX_U8> y  = std::vector<AX_U8>(144 * 144, 32);
    std::vector<AX_U8> uv = std::vector<AX_U8>(144 * 144 / 2, 128);
} fake;
std::mutex stop_barrier_mutex;
std::condition_variable stop_barrier_cv;
bool block_stop   = false;
bool stop_entered = false;
bool unblock_stop = false;
}  // namespace

namespace cosmo::log {
size_t LogFormatArg(const char*, const char*, int, int, int, fmt::string_view, fmt::format_args) {
    return 0;
}
}  // namespace cosmo::log

namespace cosmo::media {
VideoDecoder::VideoDecoder(size_t) {}
VideoDecoder::~VideoDecoder() = default;
void VideoDecoder::SetCodecType(VideoCodecType type, int width, int height) {
    codec_type_ = type;
    width_      = static_cast<size_t>(width);
    height_     = static_cast<size_t>(height);
}
DecodedVideoFrame VideoDecoder::GetDecodedFrame() {
    return {};
}
VideoFrame::VideoFrame(int width, int height, PixelFormat format, uint64_t index, int64_t timestamp)
    : active_(true),
      size_(static_cast<size_t>(width) * height * 3 / 2),
      width_(width),
      height_(height),
      pixel_fmt_(format),
      frame_index_(index),
      timestamp_(timestamp) {
    host_frame_data_ = static_cast<uint8_t*>(std::malloc(size_));
    active_          = host_frame_data_ != nullptr && fake.failure_mode != 7;
}
VideoFrame::~VideoFrame() {
    std::free(host_frame_data_);
}
bool VideoFrame::Active() const {
    return active_;
}
size_t VideoFrame::GetSize() const {
    return size_;
}
uint8_t* VideoFrame::GetData() {
    return fake.failure_mode == 8 ? nullptr : host_frame_data_;
}
void VideoFrame::SetFrameIndex(uint64_t index) {
    frame_index_ = index;
}
uint64_t VideoFrame::GetFrameIndex() const {
    return frame_index_;
}
}  // namespace cosmo::media

extern "C" {
AX_S32 AX_SYS_Init() {
    ++fake.init;
    return 0;
}
AX_S32 AX_SYS_Deinit() {
    ++fake.deinit;
    return 0;
}
AX_S32 AX_SYS_MemAlloc(AX_U64* physical, AX_VOID** vir, AX_U32 bytes, AX_U32, const AX_S8*) {
    *vir = std::malloc(bytes);
    if (!*vir)
        return -1;
    *physical = reinterpret_cast<AX_U64>(*vir);
    ++fake.allocations;
    return 0;
}
AX_S32 AX_SYS_MemFree(AX_U64, AX_VOID* vir) {
    for (const auto& [pts, input] : fake.in_flight) {
        (void)pts;
        if (input == vir)
            ++fake.unsafe_frees;
    }
    std::free(vir);
    ++fake.frees;
    return 0;
}
AX_VOID* AX_SYS_Mmap(AX_U64 physical, AX_U32) {
    ++fake.mmap_calls;
    return fake.failure_mode == 6 ? nullptr : reinterpret_cast<void*>(physical);
}
AX_S32 AX_SYS_Munmap(AX_VOID*, AX_U32) {
    return 0;
}
AX_S32 AX_VDEC_Init(const AX_VDEC_MOD_ATTR_T*) {
    return 0;
}
AX_S32 AX_VDEC_Deinit() {
    return 0;
}
AX_S32 AX_VDEC_CreateGrpEx(AX_VDEC_GRP* group, const AX_VDEC_GRP_ATTR_T*) {
    *group = fake.next_group++;
    return 0;
}
AX_S32 AX_VDEC_SetChnAttr(AX_VDEC_GRP, AX_VDEC_CHN, const AX_VDEC_CHN_ATTR_T*) {
    return 0;
}
AX_S32 AX_VDEC_EnableChn(AX_VDEC_GRP, AX_VDEC_CHN) {
    return 0;
}
AX_S32 AX_VDEC_StartRecvStream(AX_VDEC_GRP, const AX_VDEC_RECV_PIC_PARAM_T*) {
    return 0;
}
AX_S32 AX_VDEC_StopRecvStream(AX_VDEC_GRP) {
    if (block_stop) {
        std::unique_lock<std::mutex> lock(stop_barrier_mutex);
        stop_entered = true;
        stop_barrier_cv.notify_all();
        stop_barrier_cv.wait(lock, [] { return unblock_stop; });
    }
    return fake.fail_stop ? -1 : 0;
}
AX_S32 AX_VDEC_DisableChn(AX_VDEC_GRP, AX_VDEC_CHN) {
    return fake.fail_disable ? -1 : 0;
}
AX_S32 AX_VDEC_DestroyGrp(AX_VDEC_GRP group) {
    if (fake.fail_destroy)
        return -1;
    fake.in_flight.clear();
    fake.output.erase(group);
    return 0;
}
AX_S32 AX_VDEC_SendStream(AX_VDEC_GRP group, const AX_VDEC_STREAM_T* stream, AX_S32) {
    fake.in_flight[stream->u64PTS] = stream->pu8Addr;
    fake.output[group].push_back(stream->u64PTS);
    return fake.fail_send ? -1 : 0;
}
AX_S32 AX_VDEC_GetChnFrame(AX_VDEC_GRP group, AX_VDEC_CHN, AX_VIDEO_FRAME_INFO_T* info, AX_S32) {
    if (fake.fail_get)
        return -1;
    if (fake.output[group].empty())
        return AX_ERR_VDEC_BUF_EMPTY;
    const auto pts = fake.output[group].front();
    fake.output[group].pop_front();
    auto& frame                         = info->stVFrame;
    frame.u32Width                      = 144;
    frame.u32Height                     = 144;
    frame.u32PicStride[0]               = 144;
    frame.u32PicStride[1]               = 144;
    frame.u32FrameSize                  = 144 * 144 * 3 / 2;
    frame.enImgFormat                   = AX_FORMAT_YUV420_SEMIPLANAR;
    frame.stCompressInfo.enCompressMode = AX_COMPRESS_MODE_NONE;
    frame.u64PhyAddr[0]                 = reinterpret_cast<AX_U64>(fake.y.data());
    frame.u64PhyAddr[1]                 = reinterpret_cast<AX_U64>(fake.uv.data());
    if (fake.distinct_frames) {
        auto& y  = fake.frame_y.try_emplace(pts, 144 * 144, static_cast<AX_U8>(pts)).first->second;
        auto& uv = fake.frame_uv.try_emplace(pts, 144 * 144 / 2, static_cast<AX_U8>(128 + pts)).first->second;
        frame.u64PhyAddr[0] = reinterpret_cast<AX_U64>(y.data());
        frame.u64PhyAddr[1] = reinterpret_cast<AX_U64>(uv.data());
    }
    info->enModId      = AX_ID_VDEC;
    info->bEndOfStream = AX_FALSE;
    frame.u64PTS       = pts;
    if (fake.failure_mode == 1)
        frame.u64PTS += 1000;
    if (fake.failure_mode == 2)
        frame.enImgFormat = AX_FORMAT_YUV400;
    if (fake.failure_mode == 3) {
        frame.s16CropWidth  = 140;
        frame.s16CropHeight = 140;
        frame.s16CropX      = 1;
    }
    if (fake.failure_mode == 4)
        frame.u64PhyAddr[1] = 0;
    if (fake.failure_mode == 5)
        frame.u32FrameSize = 1;
    if (fake.failure_mode == 9)
        frame.enImgFormat = AX_FORMAT_YUV420_SEMIPLANAR_VU;
    if (fake.failure_mode == 10)
        frame.s16CropWidth = -1;
    if (fake.failure_mode == 11)
        frame.s16CropX = 2;
    if (fake.failure_mode == 12)
        frame.s16CropWidth = 140;
    if (fake.failure_mode == 13)
        frame.u64PhyAddr[1] = std::numeric_limits<AX_U64>::max() - 8;
    if (fake.failure_mode == 14)
        frame.u64PhyAddr[1] = frame.u64PhyAddr[0];
    if (fake.failure_mode == 15)
        frame.u64PTS = pts - 1;
    if (fake.failure_mode == 16 && pts > 1) {
        frame.u64PhyAddr[0] = reinterpret_cast<AX_U64>(fake.frame_y.at(1).data());
        frame.u64PhyAddr[1] = reinterpret_cast<AX_U64>(fake.frame_uv.at(1).data());
    }
    return 0;
}
AX_S32 AX_VDEC_ReleaseChnFrame(AX_VDEC_GRP, AX_VDEC_CHN, const AX_VIDEO_FRAME_INFO_T* info) {
    if (fake.fail_release)
        return -1;
    fake.in_flight.erase(info->stVFrame.u64PTS);
    ++fake.released_frames;
    fake.released_pts.push_back(info->stVFrame.u64PTS);
    return 0;
}
}  // extern "C"
