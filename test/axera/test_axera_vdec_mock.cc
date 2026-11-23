#include <sys/wait.h>
#include <unistd.h>

#include <cstdlib>
#include <cstring>
#include <deque>
#include <limits>
#include <map>
#include <thread>
#include <vector>

#include "ax_sys_api.h"
#include "ax_vdec_api.h"
#include "catch_amalgamated.hpp"
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
    int failure_mode{0};
    int released_frames{0};
    int mmap_calls{0};
    AX_VDEC_GRP next_group{1};
    std::map<AX_VDEC_GRP, std::deque<AX_U64>> output;
    std::map<AX_U64, const AX_U8*> in_flight;
    std::vector<AX_U8> y  = std::vector<AX_U8>(144 * 144, 32);
    std::vector<AX_U8> uv = std::vector<AX_U8>(144 * 144 / 2, 128);
} fake;
}  // namespace

namespace cosmo::log {
size_t LogFormatArg(const char*, const char*, int, int, int, fmt::string_view, fmt::format_args) {
    return 0;
}
}  // namespace cosmo::log

namespace cosmo::media {
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
size_t VideoFrame::GetWidth() const {
    return width_;
}
size_t VideoFrame::GetHeight() const {
    return height_;
}
PixelFormat VideoFrame::GetPixelFormat() const {
    return pixel_fmt_;
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
AX_S32 AX_SYS_MemAllocCached(AX_U64* physical, AX_VOID** vir, AX_U32 bytes, AX_U32 align,
                             const AX_S8* token) {
    return AX_SYS_MemAlloc(physical, vir, bytes, align, token);
}
AX_S32 AX_SYS_MflushCache(AX_U64, AX_VOID*, AX_U32) {
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
    frame.u64PTS                        = pts;
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
        frame.u64PhyAddr[0] = std::numeric_limits<AX_U64>::max() - 144 * 144 + 2;
    if (fake.failure_mode == 11)
        frame.u64PhyAddr[1] = std::numeric_limits<AX_U64>::max() - 144 * 144 / 2 + 2;
    if (fake.failure_mode == 12)
        frame.u64PhyAddr[1] = frame.u64PhyAddr[0] + 1;
    if (fake.failure_mode == 13)
        frame.u64PhyAddr[0] = frame.u64PhyAddr[1] + 1;
    if (fake.failure_mode == 14)
        frame.u64PhyAddr[1] = frame.u64PhyAddr[0] + 144 * 144;
    if (fake.failure_mode == 15)
        frame.u64PhyAddr[0] = frame.u64PhyAddr[1] + 144 * 144 / 2;
    if (fake.failure_mode == 16)
        frame.u64PhyAddr[0] = std::numeric_limits<AX_U64>::max() - 144 * 144;
    if (fake.failure_mode == 17)
        frame.u64PhyAddr[1] = std::numeric_limits<AX_U64>::max() - 144 * 144 / 2;
    return 0;
}
AX_S32 AX_VDEC_ReleaseChnFrame(AX_VDEC_GRP, AX_VDEC_CHN, const AX_VIDEO_FRAME_INFO_T* info) {
    if (fake.fail_release)
        return -1;
    fake.in_flight.erase(info->stVFrame.u64PTS);
    ++fake.released_frames;
    return 0;
}
}  // extern "C"

TEST_CASE("AX native frame stays borrowed until the final consumer releases it", "[media][axera][mock]") {
    fake = FakeVdec{};
    std::vector<uint8_t> packet{1, 2, 3, 4};
    cosmo::media::VideoDecoderAxera decoder(7);
    decoder.SetCodecType(cosmo::media::VideoCodecType::kH264, 144, 144);
    REQUIRE(decoder.Open());
    REQUIRE(decoder.SendPacket(packet.data(), packet.size(), 42));
    auto native = decoder.GetNativeFrame();
    REQUIRE(native);
    CHECK(native->Valid());
    CHECK(native->memory_kind == cosmo::media::NativeVideoMemoryKind::AxeraPhysical);
    CHECK(native->frame_index == 42);
    CHECK(native->format == cosmo::media::NativeVideoBufferFormat::NV12);
    CHECK(native->planes[0].physical_address == reinterpret_cast<AX_U64>(fake.y.data()));
    CHECK(native->planes[1].physical_address == reinterpret_cast<AX_U64>(fake.uv.data()));
    CHECK(native->backend_frame != nullptr);
    CHECK(fake.mmap_calls == 0);
    CHECK_FALSE(decoder.GetNativeFrame());
    CHECK_FALSE(decoder.Close());
    CHECK_THROWS(decoder.GetFrame());
    auto consumer = native;
    native.reset();
    CHECK(fake.released_frames == 0);
    CHECK_FALSE(decoder.Close());
    std::thread worker([frame = std::move(consumer)]() mutable { frame.reset(); });
    worker.join();
    CHECK(fake.released_frames == 0);  // Consumer did not call into the SDK.
    bool wrong_thread_closed = true;
    std::thread wrong_thread([&] { wrong_thread_closed = decoder.Close(); });
    wrong_thread.join();
    CHECK_FALSE(wrong_thread_closed);
    CHECK(fake.released_frames == 0);
    REQUIRE(decoder.Close());
    CHECK(fake.released_frames == 1);
    CHECK(fake.allocations == fake.frees);
    CHECK(fake.unsafe_frees == 0);
    CHECK(decoder.Close());
    CHECK(fake.released_frames == 1);
}

TEST_CASE("AX native frame rejects unknown layouts without mapping pixels", "[media][axera][mock]") {
    for (int mode : {1, 2, 3, 4, 5}) {
        fake              = FakeVdec{};
        fake.failure_mode = mode;
        std::vector<uint8_t> packet{1, 2, 3};
        cosmo::media::VideoDecoderAxera decoder(8);
        decoder.SetCodecType(cosmo::media::VideoCodecType::kH264, 144, 144);
        REQUIRE(decoder.Open());
        REQUIRE(decoder.SendPacket(packet.data(), packet.size(), 1));
        CHECK_THROWS(decoder.GetNativeFrame());
        CHECK(fake.released_frames == 1);
        CHECK(fake.mmap_calls == 0);
        CHECK(decoder.Close());
        CHECK(fake.allocations == fake.frees);
    }
}

TEST_CASE("AX native frame checks half-open physical plane ranges", "[media][axera][mock]") {
    for (int mode : {10, 11, 12, 13, 14, 15, 16, 17}) {
        fake              = FakeVdec{};
        fake.failure_mode = mode;
        std::vector<uint8_t> packet{1, 2, 3};
        cosmo::media::VideoDecoderAxera decoder(18);
        decoder.SetCodecType(cosmo::media::VideoCodecType::kH264, 144, 144);
        REQUIRE(decoder.Open());
        REQUIRE(decoder.SendPacket(packet.data(), packet.size(), 1));
        if (mode <= 13) {
            CHECK_THROWS(decoder.GetNativeFrame());
            CHECK(fake.released_frames == 1);
        } else {
            auto frame = decoder.GetNativeFrame();
            REQUIRE(frame);
            CHECK(frame->Valid());
            frame.reset();
        }
        CHECK(fake.mmap_calls == 0);
        REQUIRE(decoder.Close());
        CHECK(fake.released_frames == 1);
        CHECK(fake.allocations == fake.frees);
        CHECK(fake.unsafe_frees == 0);
    }
}

TEST_CASE("AX native release failure quarantines a frame with uncertain ownership", "[media][axera][mock]") {
    // Quarantine deliberately retains the process-wide SDK lease. Exercise that
    // failure in a child so successful decoder cases start with their own SYS state.
    const auto child = fork();
    REQUIRE(child >= 0);
    if (child == 0) {
        fake              = FakeVdec{};
        fake.failure_mode = 9;
        const std::vector<uint8_t> packet{1, 2, 3};
        {
            cosmo::media::VideoDecoderAxera decoder(9);
            decoder.SetCodecType(cosmo::media::VideoCodecType::kH264, 144, 144);
            if (!decoder.Open() || !decoder.SendPacket(packet.data(), packet.size(), 7))
                std::_Exit(2);
            auto frame = decoder.GetNativeFrame();
            if (!frame || frame->format != cosmo::media::NativeVideoBufferFormat::NV21)
                std::_Exit(3);
            frame.reset();
            fake.fail_release = true;
            if (decoder.Close() || fake.released_frames || fake.frees)
                std::_Exit(4);
            fake.fail_release = false;
            if (decoder.Close() || fake.released_frames || fake.frees)
                std::_Exit(5);
        }
        std::_Exit(fake.released_frames || fake.frees || fake.deinit ? 6 : 0);
    }
    int status = 0;
    REQUIRE(waitpid(child, &status, 0) == child);
    REQUIRE(WIFEXITED(status));
    CHECK(WEXITSTATUS(status) == 0);
}

TEST_CASE("AX native lease outlives decoder without SDK calls on consumer thread", "[media][axera][mock]") {
    const auto child = fork();
    REQUIRE(child >= 0);
    if (child == 0) {
        fake = FakeVdec{};
        std::vector<uint8_t> packet{1, 2, 3};
        auto decoder = std::make_unique<cosmo::media::VideoDecoderAxera>(10);
        decoder->SetCodecType(cosmo::media::VideoCodecType::kH264, 144, 144);
        if (!decoder->Open() || !decoder->SendPacket(packet.data(), packet.size(), 3))
            std::_Exit(2);
        auto frame = decoder->GetNativeFrame();
        if (!frame || !frame->Valid())
            std::_Exit(3);
        decoder.reset();
        if (!frame->Valid() || fake.released_frames || fake.frees || fake.deinit)
            std::_Exit(4);
        std::thread worker([frame = std::move(frame)]() mutable { frame.reset(); });
        worker.join();
        std::_Exit(fake.released_frames || fake.frees || fake.deinit ? 5 : 0);
    }
    int status = 0;
    REQUIRE(waitpid(child, &status, 0) == child);
    CHECK(WIFEXITED(status));
    CHECK(WEXITSTATUS(status) == 0);
}

TEST_CASE("AX VDEC preserves submitted memory and original indexes until output", "[media][axera][mock]") {
    fake = FakeVdec{};
    std::vector<uint8_t> a{1, 2, 3, 4};
    std::vector<uint8_t> b{9, 8, 7, 6};
    {
        cosmo::media::VideoDecoderAxera decoder(0);
        decoder.SetCodecType(cosmo::media::VideoCodecType::kH264, 144, 144);
        REQUIRE(decoder.Open());
        REQUIRE(decoder.SendPacket(a.data(), a.size(), 42));
        REQUIRE(decoder.SendPacket(b.data(), b.size(), 42));
        REQUIRE(fake.in_flight.size() == 2);
        auto first  = fake.in_flight.begin();
        auto second = std::next(first);
        CHECK(first->second != second->second);
        CHECK(std::memcmp(first->second, a.data(), a.size()) == 0);
        CHECK(std::memcmp(second->second, b.data(), b.size()) == 0);
        CHECK(fake.frees == 0);
        auto out1 = decoder.GetFrame();
        REQUIRE(out1);
        CHECK(out1->GetFrameIndex() == 42);
        CHECK(out1->GetData()[0] == 32);
        auto out2 = decoder.GetFrame();
        REQUIRE(out2);
        CHECK(out2->GetFrameIndex() == 42);
        CHECK(fake.frees == 2);
        CHECK(fake.unsafe_frees == 0);
    }
    CHECK(fake.init == 1);
    CHECK(fake.deinit == 1);
}

TEST_CASE("AX VDEC retains uncertain submissions if group destruction fails", "[media][axera][mock]") {
    fake = FakeVdec{};
    std::vector<uint8_t> packet{1, 2, 3, 4};
    {
        cosmo::media::VideoDecoderAxera decoder(0);
        decoder.SetCodecType(cosmo::media::VideoCodecType::kH265, 144, 144);
        REQUIRE(decoder.Open());
        fake.fail_send = true;
        CHECK_FALSE(decoder.SendPacket(packet.data(), packet.size(), 9));
        fake.fail_destroy = true;
        CHECK_FALSE(decoder.Close());
        CHECK(fake.frees == 0);
        CHECK(fake.deinit == 0);
        fake.fail_destroy = false;
        CHECK(decoder.Close());
        CHECK(fake.frees == 1);
        CHECK(fake.unsafe_frees == 0);
    }
    CHECK(fake.deinit == 1);
}

TEST_CASE("AX VDEC frame and teardown failures retain state for retry", "[media][axera][mock]") {
    fake = FakeVdec{};
    std::vector<uint8_t> packet{1, 2, 3, 4};
    cosmo::media::VideoDecoderAxera decoder(0);
    decoder.SetCodecType(cosmo::media::VideoCodecType::kH264, 144, 144);
    REQUIRE(decoder.Open());
    REQUIRE(decoder.SendPacket(packet.data(), packet.size(), 1));
    fake.fail_release = true;
    CHECK_THROWS(decoder.GetFrame());
    CHECK_FALSE(decoder.IsOpened());
    CHECK_FALSE(decoder.Close());
    CHECK(fake.frees == 0);
    fake.fail_release = false;
    fake.fail_stop    = true;
    CHECK_FALSE(decoder.Close());
    CHECK(fake.frees == 1);
    CHECK(fake.deinit == 0);
    fake.fail_stop    = false;
    fake.fail_disable = true;
    CHECK_FALSE(decoder.Close());
    CHECK(fake.deinit == 0);
    fake.fail_disable = false;
    CHECK(decoder.Close());
    CHECK(fake.deinit == 1);
}

TEST_CASE("AX VDEC fatal retrieval and pending saturation are observable and resettable",
          "[media][axera][mock]") {
    fake = FakeVdec{};
    std::vector<uint8_t> packet{1, 2, 3, 4};
    cosmo::media::VideoDecoderAxera decoder(0);
    decoder.SetCodecType(cosmo::media::VideoCodecType::kH264, 144, 144);
    REQUIRE(decoder.Open());
    CHECK_FALSE(decoder.GetFrame());
    fake.fail_get = true;
    CHECK_THROWS(decoder.GetFrame());
    CHECK_FALSE(decoder.IsOpened());
    fake.fail_get = false;
    REQUIRE(decoder.Close());
    REQUIRE(decoder.Open());
    for (int i = 0; i < 128; ++i)
        REQUIRE(decoder.SendPacket(packet.data(), packet.size(), i));
    CHECK_FALSE(decoder.SendPacket(packet.data(), packet.size(), 129));
    CHECK_FALSE(decoder.IsOpened());
    REQUIRE(decoder.Close());
    CHECK(fake.allocations == fake.frees);
    CHECK(fake.unsafe_frees == 0);
}

TEST_CASE("AX VDEC malformed acquired frames fault and release before retry", "[media][axera][mock]") {
    for (int mode = 1; mode <= 8; ++mode) {
        fake              = FakeVdec{};
        fake.failure_mode = mode;
        cosmo::media::VideoDecoderAxera decoder(0);
        decoder.SetCodecType(cosmo::media::VideoCodecType::kH264, 144, 144);
        REQUIRE(decoder.Open());
        const std::vector<uint8_t> packet{1, 2, 3, 4};
        REQUIRE(decoder.SendPacket(packet.data(), packet.size(), 1));
        CHECK_THROWS(decoder.GetFrame());
        CHECK(fake.released_frames == 1);
        CHECK_FALSE(decoder.IsOpened());
        CHECK_THROWS(decoder.GetFrame());
        CHECK(fake.released_frames == 1);
        REQUIRE(decoder.Close());
        CHECK(fake.allocations == fake.frees);
        CHECK(fake.unsafe_frees == 0);
    }
}
