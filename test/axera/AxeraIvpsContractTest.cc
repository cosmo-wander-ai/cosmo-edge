#include <cassert>
#include <functional>
#include <iostream>
#include <type_traits>

#include "axera/AxeraVdecFixture.inc"
#include "media/AxeraCompressedPacketLease.h"
#include "media/AxeraIvpsPreprocess.h"
#include "nn/device/axera/axera_physical_lease.h"
using namespace cosmo::media;
struct Mock : AxIvpsSynchronousBackend {
    AxIvpsCapabilities caps{true, true, true, true, true, true, true};
    std::shared_ptr<void> runtime = std::make_shared<int>(3);
    int calls                     = 0;
    AxIvpsCompletion result       = AxIvpsCompletion::CompletedOk;
    uint64_t y = 0, uv = 0, index = 0;
    std::weak_ptr<void> expected;
    std::shared_ptr<void> RuntimeOwner() const noexcept override {
        return runtime;
    }
    AxIvpsCapabilities Capabilities(const NativeVideoBuffer&, const AxIvpsRequest&,
                                    const AxIvpsDeviceBuffer&) const noexcept override {
        return caps;
    }
    AxIvpsCompletion ProcessAndWait(const NativeVideoBuffer& in, const AxIvpsRequest&,
                                    const AxIvpsDeviceBuffer& out) noexcept override {
        ++calls;
        assert(in.owner && out.owner && !expected.expired());
        assert(in.planes[0].physical_address == y && in.planes[1].physical_address == uv &&
               in.frame_index == index);
        return result;
    }
};
struct ReentryBackend : AxIvpsSynchronousBackend {
    struct Runtime {
        std::function<void()> on_destroy;
        ~Runtime() {
            on_destroy();
        }
    };
    std::function<AxIvpsResult()> reenter;
    mutable bool in_caps   = false;
    mutable bool destroyed = false;
    std::shared_ptr<void> RuntimeOwner() const noexcept override {
        auto p        = std::make_shared<Runtime>();
        p->on_destroy = [this] {
            destroyed = true;
            assert(reenter() == AxIvpsResult::UnsupportedOrUnverified);
        };
        return p;
    }
    AxIvpsCapabilities Capabilities(const NativeVideoBuffer&, const AxIvpsRequest&,
                                    const AxIvpsDeviceBuffer&) const noexcept override {
        in_caps = true;
        assert(reenter() == AxIvpsResult::UnsupportedOrUnverified);
        return {true, true, true, true, true, true, true};
    }
    AxIvpsCompletion ProcessAndWait(const NativeVideoBuffer&, const AxIvpsRequest&,
                                    const AxIvpsDeviceBuffer&) noexcept override {
        return AxIvpsCompletion::CompletedOk;
    }
};
struct BlockingUnknown : Mock {
    std::mutex mutex;
    std::condition_variable cv;
    bool entered = false, release = false;
    AxIvpsCompletion ProcessAndWait(const NativeVideoBuffer& in, const AxIvpsRequest&,
                                    const AxIvpsDeviceBuffer& out) noexcept override {
        std::unique_lock<std::mutex> lock(mutex);
        ++calls;
        assert(in.owner && out.owner);
        entered = true;
        cv.notify_all();
        cv.wait(lock, [&] { return release; });
        return AxIvpsCompletion::CompletionUnknown;
    }
};
int main() {
    static_assert(!std::is_constructible_v<AxeraNativeFrameDescriptor, const AX_VIDEO_FRAME_INFO_T&, uint64_t,
                                           std::shared_ptr<void>>);
    static_assert(!std::is_copy_constructible_v<AxeraNativeFrameDescriptor> &&
                  !std::is_copy_assignable_v<AxeraNativeFrameDescriptor>);
    static_assert(!std::is_move_constructible_v<AxeraNativeFrameDescriptor> &&
                  !std::is_move_assignable_v<AxeraNativeFrameDescriptor>);
    fake = FakeVdec{};
    std::vector<uint8_t> packet{1, 2, 3, 4};
    VideoDecoderAxera decoder(7);
    decoder.SetCodecType(VideoCodecType::kH264, 144, 144);
    assert(decoder.Open());
    assert(decoder.SendPacket(packet.data(), packet.size(), 42));
    auto actual = decoder.GetNativeFrame();
    assert(actual);
    auto descriptor = AxeraFrameDescriptor(*actual);
    assert(descriptor);
    auto alias = std::make_shared<NativeVideoBuffer>(*actual);
    AxIvpsSourceColor color{NativeVideoColorSpace::Bt709, NativeVideoColorRange::Limited};
    AxIvpsDeviceBuffer d{0x30000000,
                         0x40000000,
                         640 * 640 * 3,
                         640 * 3,
                         0,
                         640,
                         640,
                         AxIvpsOutputFormat::RgbU8Packed,
                         std::make_shared<int>(2)};
    AxIvpsRequest r{{0, 0, 144, 144}, 640, 640, AxIvpsOutputFormat::RgbU8Packed, true, 114};
    Mock m;
    m.expected = actual->owner;
    m.y        = actual->planes[0].physical_address;
    m.uv       = actual->planes[1].physical_address;
    m.index    = actual->frame_index;
    auto run   = [&] { return AxeraIvpsPreprocess(descriptor, color, r, d, m); };
    {
        ReentryBackend re;
        re.reenter = [&] { return AxeraIvpsPreprocess(descriptor, color, r, d, re); };
        assert(AxeraIvpsPreprocess(descriptor, color, r, d, re) == AxIvpsResult::Ok);
        assert(re.in_caps && re.destroyed);
    }
    {
        unsigned char bytes[128]{};
        AxPacketPool p({{m.y, bytes, sizeof(bytes), std::make_shared<int>(4)}});
        assert(p.Valid());
        auto hold = p.Acquire();
        assert(hold);
        assert(run() == AxIvpsResult::UnsupportedOrUnverified && m.calls == 0);
    }
    {
        unsigned char bytes[128]{};
        AxPacketPool p({{d.physical_address, bytes, sizeof(bytes), std::make_shared<int>(5)}});
        assert(p.Valid());
        auto hold = p.Acquire();
        assert(hold);
        assert(run() == AxIvpsResult::UnsupportedOrUnverified && m.calls == 0);
    }
    {
        cosmo::nn::AxPhysicalLease reader;
        assert(reader.Acquire({{d.physical_address, d.bytes, cosmo::nn::AxPhysicalAccess::Mode::Read}},
                              {d.owner}));
        assert(run() == AxIvpsResult::UnsupportedOrUnverified && m.calls == 0);
    }
    assert(AxeraIvpsPreprocess({}, color, r, d, m) == AxIvpsResult::InvalidInput && m.calls == 0);
    auto badcolor  = color;
    badcolor.space = NativeVideoColorSpace::Unspecified;
    assert(AxeraIvpsPreprocess(descriptor, badcolor, r, d, m) == AxIvpsResult::InvalidInput && m.calls == 0);
    badcolor.space = static_cast<NativeVideoColorSpace>(99);
    assert(AxeraIvpsPreprocess(descriptor, badcolor, r, d, m) == AxIvpsResult::InvalidInput && m.calls == 0);
    badcolor       = color;
    badcolor.range = static_cast<NativeVideoColorRange>(99);
    assert(AxeraIvpsPreprocess(descriptor, badcolor, r, d, m) == AxIvpsResult::InvalidInput && m.calls == 0);
    // Mutating a separate generic-buffer alias cannot affect the trusted immutable descriptor.
    alias->frame_index++;
    alias->planes[0].physical_address += 128;
    alias->planes[1].block_id++;
    alias->format = NativeVideoBufferFormat::NV21;
    assert(run() == AxIvpsResult::Ok && m.calls == 1);
    m.caps.letterbox_verified = false;
    assert(run() == AxIvpsResult::UnsupportedOrUnverified && m.calls == 1);
    m.caps.letterbox_verified              = true;
    m.caps.synchronous_completion_verified = false;
    assert(run() == AxIvpsResult::UnsupportedOrUnverified && m.calls == 1);
    m.caps.synchronous_completion_verified = true;
    r.roi.width                            = 143;
    assert(run() == AxIvpsResult::InvalidInput && m.calls == 1);
    r.roi.width        = 144;
    d.physical_address = m.y;
    assert(run() == AxIvpsResult::InvalidInput && m.calls == 1);
    d.physical_address = 0x30000000;
    d.virtual_address  = 0;
    assert(run() == AxIvpsResult::InvalidInput && m.calls == 1);
    d.virtual_address = UINTPTR_MAX - 1;
    assert(run() == AxIvpsResult::InvalidInput && m.calls == 1);
    d.virtual_address = 0x40000000;
    d.row_stride      = 640 * 3 + 16;
    assert(run() == AxIvpsResult::InvalidInput && m.calls == 1);
    d.row_stride = 640 * 3;
    m.result     = AxIvpsCompletion::CompletedError;
    assert(run() == AxIvpsResult::BackendFailure && m.calls == 2);
    BlockingUnknown unknown;
    unknown.expected  = actual->owner;
    unknown.y         = m.y;
    unknown.uv        = m.uv;
    unknown.index     = m.index;
    auto source_lease = std::weak_ptr<void>(actual->owner), destination_lease = std::weak_ptr<void>(d.owner),
         runtime_lease = std::weak_ptr<void>(unknown.runtime);
    AxIvpsResult first = AxIvpsResult::InvalidInput;
    std::thread worker([&] { first = AxeraIvpsPreprocess(descriptor, color, r, d, unknown); });
    {
        std::unique_lock<std::mutex> lock(unknown.mutex);
        unknown.cv.wait(lock, [&] { return unknown.entered; });
    }
    assert(AxeraIvpsPreprocess(descriptor, color, r, d, unknown) == AxIvpsResult::UnsupportedOrUnverified &&
           unknown.calls == 1);
    {
        std::lock_guard<std::mutex> lock(unknown.mutex);
        unknown.release = true;
    }
    unknown.cv.notify_all();
    worker.join();
    assert(first == AxIvpsResult::CompletionUnknown);
    assert(AxeraIvpsPreprocess(descriptor, color, r, d, unknown) == AxIvpsResult::CompletionUnknown &&
           unknown.calls == 1);
    descriptor.reset();
    actual.reset();
    alias.reset();
    d.owner.reset();
    unknown.runtime.reset();
    assert(!source_lease.expired() && !destination_lease.expired() && !runtime_lease.expired());
    std::cout << "AX IVPS immutable contract mock passed\n";
}
