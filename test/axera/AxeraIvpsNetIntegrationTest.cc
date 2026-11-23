// Real wrapper and real NN node, SDK calls mocked. Neither gate is enabled in production.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wreturn-type"
#define AX_SYS_Init AxNnUnusedSysInit
#define AX_SYS_Deinit AxNnUnusedSysDeinit
#define AX_SYS_MemAlloc AxNnUnusedMemAlloc
#define AX_SYS_MemFree AxNnUnusedMemFree
#define main axera_net_node_existing_test_main
#include "AxeraNetNodeTest.cc"
#undef main
#undef AX_SYS_Init
#undef AX_SYS_Deinit
#undef AX_SYS_MemAlloc
#undef AX_SYS_MemFree
#pragma GCC diagnostic pop
#include "AxeraVdecFixture.inc"
#include "media/AxeraIvpsPreprocess.h"
namespace {
struct BlockingBackend : cosmo::media::AxIvpsSynchronousBackend {
    std::mutex mutex;
    std::condition_variable cv;
    bool entered = false, release = false;
    int calls                   = 0;
    std::shared_ptr<void> owner = std::make_shared<int>(1);
    std::shared_ptr<void> RuntimeOwner() const noexcept override {
        return owner;
    }
    cosmo::media::AxIvpsCapabilities Capabilities(
        const cosmo::media::NativeVideoBuffer&, const cosmo::media::AxIvpsRequest&,
        const cosmo::media::AxIvpsDeviceBuffer&) const noexcept override {
        return {true, true, true, true, true, true, true};
    }
    cosmo::media::AxIvpsCompletion ProcessAndWait(const cosmo::media::NativeVideoBuffer&,
                                                  const cosmo::media::AxIvpsRequest&,
                                                  const cosmo::media::AxIvpsDeviceBuffer&) noexcept override {
        std::unique_lock<std::mutex> l(mutex);
        ++calls;
        entered = true;
        cv.notify_all();
        cv.wait(l, [&] { return release; });
        return cosmo::media::AxIvpsCompletion::CompletedOk;
    }
};
}  // namespace
int main() {
    using namespace cosmo::nn;
    using namespace cosmo::media;
    Metadata(AX_ENGINE_DT_UINT8, AX_ENGINE_TENSOR_LAYOUT_NHWC);
    AxeraNetNode node;
    Configure(node);
    node.SetInputContract(kAxeraRgbUint8InputContract);
    const char model[] = "mock";
    assert(node.LoadWeight(model, sizeof(model)) && node.InferTopShapes());
    std::vector<unsigned char> input_memory(12), output_memory(8);
    NativeTensorDesc i{};
    i.physical_base                 = 0x10000;
    i.virtual_base                  = input_memory.data();
    i.allocation_bytes              = 12;
    i.visible_bytes                 = 12;
    i.required_alignment            = 1;
    i.shape                         = {1, 2, 2, 3};
    i.byte_strides                  = {12, 6, 3, 1};
    i.element                       = NativeTensor::Element::UInt8;
    i.layout                        = NativeTensor::Layout::NHWC;
    i.owner                         = std::make_shared<int>(1);
    i.contiguous_extent_verified    = true;
    i.lifetime_and_release_verified = true;
    i.device_visibility_verified    = true;
    i.completion_verified           = true;
    i.input_contract                = kAxeraRgbUint8InputContract;
    NativeTensorDesc o{};
    o.physical_base                 = 0x20000;
    o.virtual_base                  = output_memory.data();
    o.allocation_bytes              = 8;
    o.visible_bytes                 = 8;
    o.required_alignment            = 1;
    o.shape                         = {1, 2};
    o.byte_strides                  = {8, 4};
    o.element                       = NativeTensor::Element::Float32;
    o.layout                        = NativeTensor::Layout::Dense;
    o.owner                         = std::make_shared<int>(2);
    o.contiguous_extent_verified    = true;
    o.lifetime_and_release_verified = true;
    o.device_visibility_verified    = true;
    o.completion_verified           = true;
    o.hardware_consumer_verified    = true;
    auto it                         = std::make_shared<NativeTensor>(i, NativeTensor::State::DeviceReadable),
         ot                         = std::make_shared<NativeTensor>(o, NativeTensor::State::DeviceWritable);
    BlobHandle ih, oh;
    ih.native_tensor = it;
    oh.native_tensor = ot;
    BlobDesc id, od;
    id.dims = {1, 2, 2, 3};
    od.dims = {1, 2};
    std::vector<std::shared_ptr<Blob>> ins{std::make_shared<Blob>(id, ih)},
        outs{std::make_shared<Blob>(od, oh)};
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
    AxIvpsSourceColor color{NativeVideoColorSpace::Bt709, NativeVideoColorRange::Limited};
    AxIvpsDeviceBuffer dst{0x10000, reinterpret_cast<uintptr_t>(input_memory.data()),
                           12,      6,
                           0,       2,
                           2,       AxIvpsOutputFormat::RgbU8Packed,
                           i.owner};
    AxIvpsRequest req{{0, 0, 144, 144}, 2, 2, AxIvpsOutputFormat::RgbU8Packed, false, 114};
    BlockingBackend backend;
    // IVPS holds destination write; actual NN Forward must reject before RunSync.
    AxIvpsResult result = AxIvpsResult::InvalidInput;
    std::thread ivps([&] { result = AxeraIvpsPreprocess(descriptor, color, req, dst, backend); });
    {
        std::unique_lock<std::mutex> l(backend.mutex);
        backend.cv.wait(l, [&] { return backend.entered; });
    }
    assert(!node.Forward(ins, outs) && run_calls == 0);
    {
        std::lock_guard<std::mutex> l(backend.mutex);
        backend.release = true;
    }
    backend.cv.notify_all();
    ivps.join();
    assert(result == AxIvpsResult::Ok && backend.calls == 1);
    // NN holds input read during real Forward; actual wrapper rejects before backend.
    block_next = true;
    std::atomic<bool> nn_ok{false};
    std::thread nn([&] { nn_ok = bool(node.Forward(ins, outs)); });
    {
        std::unique_lock<std::mutex> l(run_mutex);
        run_cv.wait(l, [] { return run_entered; });
    }
    assert(AxeraIvpsPreprocess(descriptor, color, req, dst, backend) ==
               AxIvpsResult::UnsupportedOrUnverified &&
           backend.calls == 1);
    {
        std::lock_guard<std::mutex> l(run_mutex);
        run_release = true;
    }
    run_cv.notify_all();
    nn.join();
    assert(nn_ok && run_calls == 1);
    assert(AxeraIvpsPreprocess(descriptor, color, req, dst, backend) == AxIvpsResult::Ok &&
           backend.calls == 2);
    descriptor.reset();
    actual.reset();
    assert(decoder.Close());
    std::puts("real IVPS wrapper + NN mock contention passed");
}
