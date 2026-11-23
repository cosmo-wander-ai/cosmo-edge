// Standalone mock-SDK contract test. Compile against the target SDK headers;
// all AX functions used here are mocks and no NPU hardware is accessed.
#include <sys/wait.h>
#include <unistd.h>

#include <atomic>
#include <cassert>
#include <cmath>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <vector>

#include "ax_engine_api.h"
#include "ax_sys_api.h"
#include "media/AxeraCompressedPacketLease.h"
#include "nn/device/axera/axera_net_node.h"
#include "nn/device/axera/axera_physical_lease.h"
#include "nn/node/node_type_utils.h"

namespace {
int sys_init = 0, sys_deinit = 0, engine_init = 0, engine_deinit = 0, allocations = 0;
std::atomic<int> run_calls{0};
std::atomic<bool> block_next{false};
std::mutex run_mutex;
std::condition_variable run_cv;
bool run_entered = false, run_release = false;
std::atomic<bool> block_next_destroy{false};
std::mutex destroy_mutex;
std::condition_variable destroy_cv;
bool destroy_entered = false, destroy_release = false;
std::function<void()> destroy_callback, engine_deinit_callback, sys_deinit_callback;
int handles         = 0;
int destroy_calls   = 0;
int destroy_failure = 0, engine_deinit_failure = 0, sys_deinit_failure = 0, engine_init_failure = 0;
int create_failure = 0, context_failure = 0, query_failure = 0;
const void* retained_model_pointer = nullptr;
bool fail_context                  = false;
int fail_run                       = 0;
int allocation_calls               = 0;
int fail_allocation                = 0;
std::vector<unsigned char> observed_input;
AX_S32 input_shape[]      = {1, 3, 2, 2};
AX_S32 output_shape[]     = {1, 2};
AX_S32 non_dense_stride[] = {13, 4, 2, 1};
AX_ENGINE_IOMETA_T input{};
AX_ENGINE_IOMETA_T output{};
AX_ENGINE_IO_INFO_T info{};

void Metadata(AX_ENGINE_DATA_TYPE_T dtype, AX_ENGINE_TENSOR_LAYOUT_T layout) {
    input_shape[1]    = layout == AX_ENGINE_TENSOR_LAYOUT_NCHW ? 3 : 2;
    input_shape[2]    = 2;
    input_shape[3]    = layout == AX_ENGINE_TENSOR_LAYOUT_NCHW ? 2 : 3;
    input             = {};
    input.pShape      = input_shape;
    input.nShapeSize  = 4;
    input.eLayout     = layout;
    input.eDataType   = dtype;
    input.nSize       = 12 * (dtype == AX_ENGINE_DT_FLOAT32 ? sizeof(float) : sizeof(uint8_t));
    output            = {};
    output.pShape     = output_shape;
    output.nShapeSize = 2;
    output.eDataType  = AX_ENGINE_DT_FLOAT32;
    output.nSize      = 2 * sizeof(float);
    info              = {};
    info.pInputs      = &input;
    info.nInputSize   = 1;
    info.pOutputs     = &output;
    info.nOutputSize  = 1;
}

std::shared_ptr<cosmo::nn::Blob> BlobFor(std::vector<float>& storage, std::vector<int> shape) {
    cosmo::nn::BlobDesc desc;
    desc.data_type   = cosmo::nn::DATA_TYPE_FLOAT;
    desc.data_format = cosmo::nn::DATA_FORMAT_NCHW;
    desc.dims        = std::move(shape);
    cosmo::nn::BlobHandle handle;
    handle.base = storage.data();
    return std::make_shared<cosmo::nn::Blob>(desc, handle);
}

void Configure(cosmo::nn::AxeraNetNode& node) {
    node.SetNetworkInputNames({"images"});
    node.SetNetworkOutputNames({"output0"});
}
}  // namespace

extern "C" AX_S32 AX_SYS_Init() {
    ++sys_init;
    return 0;
}
extern "C" AX_S32 AX_SYS_Deinit() {
    ++sys_deinit;
    if (sys_deinit_callback)
        sys_deinit_callback();
    if (sys_deinit_failure == 2)
        throw std::runtime_error("mock SYS deinit exception");
    return sys_deinit_failure ? -71 : 0;
}
extern "C" AX_S32 AX_SYS_MemAlloc(AX_U64* phy, AX_VOID** virt, AX_U32 size, AX_U32, const AX_S8*) {
    ++allocation_calls;
    if (allocation_calls == fail_allocation)
        return -7;
    *virt = std::malloc(size);
    *phy  = static_cast<AX_U64>(reinterpret_cast<uintptr_t>(*virt));
    if (!*virt)
        return -1;
    ++allocations;
    return 0;
}
extern "C" AX_S32 AX_SYS_MemFree(AX_U64, AX_VOID* virt) {
    --allocations;
    std::free(virt);
    return 0;
}
extern "C" AX_S32 AX_ENGINE_Init(AX_ENGINE_NPU_ATTR_T*) {
    ++engine_init;
    if (engine_init_failure == 2)
        throw std::runtime_error("mock ENGINE init exception");
    return engine_init_failure ? -72 : 0;
}
extern "C" AX_S32 AX_ENGINE_Deinit() {
    ++engine_deinit;
    if (engine_deinit_callback)
        engine_deinit_callback();
    if (engine_deinit_failure == 2)
        throw std::runtime_error("mock ENGINE deinit exception");
    return engine_deinit_failure ? -73 : 0;
}
extern "C" AX_S32 AX_ENGINE_CreateHandle(AX_ENGINE_HANDLE* handle, const AX_VOID* model, AX_U32) {
    retained_model_pointer = model;
    if (create_failure == 1)
        return -75;
    *handle = reinterpret_cast<void*>(0x1234);
    ++handles;
    if (create_failure == 2)
        throw std::runtime_error("mock create exception");
    return 0;
}
extern "C" AX_S32 AX_ENGINE_DestroyHandle(AX_ENGINE_HANDLE) {
    ++destroy_calls;
    if (destroy_callback)
        destroy_callback();
    if (block_next_destroy.exchange(false)) {
        std::unique_lock<std::mutex> lock(destroy_mutex);
        destroy_entered = true;
        destroy_cv.notify_all();
        destroy_cv.wait(lock, [] { return destroy_release; });
    }
    if (destroy_failure == 2)
        throw std::runtime_error("mock destroy exception");
    if (destroy_failure)
        return -74;
    --handles;
    return 0;
}
extern "C" AX_S32 AX_ENGINE_CreateContext(AX_ENGINE_HANDLE) {
    if (context_failure == 2)
        throw std::runtime_error("mock context exception");
    return (fail_context || context_failure) ? -9 : 0;
}
extern "C" AX_S32 AX_ENGINE_GetIOInfo(AX_ENGINE_HANDLE, AX_ENGINE_IO_INFO_T** result) {
    if (query_failure == 2)
        throw std::runtime_error("mock metadata exception");
    if (query_failure)
        return -76;
    *result = &info;
    return 0;
}
extern "C" AX_S32 AX_ENGINE_RunSync(AX_ENGINE_HANDLE, AX_ENGINE_IO_T* io) {
    ++run_calls;
    assert(io->nInputSize == 1 && io->nOutputSize == 1);
    assert(io->pInputs[0].phyAddr == 0x10000 && io->pInputs[0].nSize == 12);
    assert(io->pOutputs[0].nSize == 8);
    if (block_next.exchange(false)) {
        std::unique_lock<std::mutex> lock(run_mutex);
        run_entered = true;
        run_cv.notify_all();
        run_cv.wait(lock, [] { return run_release; });
    }
    if (fail_run == 2)
        throw std::runtime_error("mock inference exception");
    return fail_run ? -11 : 0;
}

// The standalone test only needs the net-node name; the full project links
// the real NodeTypeUtils implementation.
namespace cosmo::nn {
#ifdef COSMO_AXERA_MOCK_VERIFIED_CONTRACT
void AxeraMockAbandonEngine() noexcept;
#endif
std::string NodeTypeUtils::NodeTypeToStr(NodeType) {
    return "net";
}
}  // namespace cosmo::nn

int main() {
    using namespace cosmo::nn;
    // Each sticky failure runs in a fresh process and must never be retried.
    for (int failure = 0; failure < 14; ++failure) {
        const pid_t child = fork();
        assert(child >= 0);
        if (child == 0) {
            Metadata(AX_ENGINE_DT_UINT8, AX_ENGINE_TENSOR_LAYOUT_NHWC);
            auto* node = new AxeraNetNode;
            Configure(*node);
            node->SetInputContract(kAxeraRgbUint8InputContract);
            const char bytes[] = "mock";
            if (failure == 6 || failure == 7)
                engine_init_failure = failure == 6 ? 1 : 2;
            if (failure == 8 || failure == 9)
                create_failure = failure == 8 ? 1 : 2;
            if (failure == 10 || failure == 11)
                context_failure = failure == 10 ? 1 : 2;
            if (failure == 12 || failure == 13)
                query_failure = failure == 12 ? 1 : 2;
            const bool loaded = bool(node->LoadWeight(bytes, sizeof(bytes)));
            if (failure >= 6) {
                assert(!loaded && engine_init == 1 && engine_deinit == 0 && sys_init == 1 && sys_deinit == 0);
                if (failure >= 8)
                    assert(retained_model_pointer &&
                           std::memcmp(retained_model_pointer, bytes, sizeof(bytes)) == 0);
                if (failure == 8)
                    assert(handles == 0);
                else if (failure >= 9)
                    assert(handles == 1);
            } else {
                assert(loaded && handles == 1);
                if (failure < 2)
                    destroy_failure = failure + 1;
                else if (failure < 4)
                    engine_deinit_failure = failure - 1;
                else
                    sys_deinit_failure = failure - 3;
                assert(!node->Close());
                if (failure < 2)
                    assert(destroy_calls == 1 && handles == 1 && engine_deinit == 0 && sys_deinit == 0);
                else if (failure < 4)
                    assert(destroy_calls == 1 && handles == 0 && engine_deinit == 1 && sys_deinit == 0);
                else
                    assert(destroy_calls == 1 && handles == 0 && engine_deinit == 1 && sys_deinit == 1);
            }
            destroy_failure = engine_deinit_failure = sys_deinit_failure = engine_init_failure =
                create_failure = context_failure = query_failure = 0;
            assert(!node->Close());
            delete node;
            if (failure >= 8)
                assert(retained_model_pointer &&
                       std::memcmp(retained_model_pointer, bytes, sizeof(bytes)) == 0);
            AxeraNetNode next;
            Configure(next);
            next.SetInputContract(kAxeraRgbUint8InputContract);
            assert(!next.LoadWeight(bytes, sizeof(bytes)));
            assert(engine_init == 1 && sys_init == 1);
            if (failure < 2)
                assert(destroy_calls == 1 && engine_deinit == 0 && sys_deinit == 0);
            else if (failure < 4)
                assert(destroy_calls == 1 && engine_deinit == 1 && sys_deinit == 0);
            else if (failure < 6)
                assert(destroy_calls == 1 && engine_deinit == 1 && sys_deinit == 1);
            else
                assert(destroy_calls == 0 && engine_deinit == 0 && sys_deinit == 0);
            _exit(0);
        }
        int status = 0;
        assert(waitpid(child, &status, 0) == child);
        assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    }

    // Node destruction after a failed last Engine Deinit retains SYS.
    {
        const pid_t child = fork();
        assert(child >= 0);
        if (child == 0) {
            Metadata(AX_ENGINE_DT_UINT8, AX_ENGINE_TENSOR_LAYOUT_NHWC);
            auto* node = new AxeraNetNode;
            Configure(*node);
            node->SetInputContract(kAxeraRgbUint8InputContract);
            const char bytes[] = "mock";
            assert(node->LoadWeight(bytes, sizeof(bytes)));
            engine_deinit_failure = 1;
            delete node;
            engine_deinit_failure = 0;
            assert(engine_init == 1 && engine_deinit == 1 && sys_init == 1 && sys_deinit == 0);
            AxeraNetNode next;
            Configure(next);
            next.SetInputContract(kAxeraRgbUint8InputContract);
            assert(!next.LoadWeight(bytes, sizeof(bytes)));
            assert(engine_deinit == 1 && sys_deinit == 0);
            _exit(0);
        }
        int status = 0;
        assert(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    }
    // A blocked last-handle failure serializes competing model construction;
    // the waiter observes sticky poison and never reaches SDK Init/Create.
    {
        const pid_t child = fork();
        assert(child >= 0);
        if (child == 0) {
            Metadata(AX_ENGINE_DT_UINT8, AX_ENGINE_TENSOR_LAYOUT_NHWC);
            auto* node = new AxeraNetNode;
            Configure(*node);
            node->SetInputContract(kAxeraRgbUint8InputContract);
            const char bytes[] = "mock";
            assert(node->LoadWeight(bytes, sizeof(bytes)));
            destroy_failure    = 1;
            block_next_destroy = true;
            std::atomic<bool> closed{true}, loaded{true};
            std::mutex start_mutex;
            std::condition_variable start_cv;
            bool started = false;
            std::thread closer([&] { closed = bool(node->Close()); });
            {
                std::unique_lock<std::mutex> lock(destroy_mutex);
                destroy_cv.wait(lock, [] { return destroy_entered; });
            }
            std::thread waiter([&] {
                {
                    std::lock_guard<std::mutex> lock(start_mutex);
                    started = true;
                }
                start_cv.notify_all();
                AxeraNetNode next;
                Configure(next);
                next.SetInputContract(kAxeraRgbUint8InputContract);
                loaded = bool(next.LoadWeight(bytes, sizeof(bytes)));
            });
            {
                std::unique_lock<std::mutex> lock(start_mutex);
                start_cv.wait(lock, [&] { return started; });
            }
            {
                std::lock_guard<std::mutex> lock(destroy_mutex);
                destroy_release = true;
            }
            destroy_cv.notify_all();
            closer.join();
            waiter.join();
            assert(!closed && !loaded && destroy_calls == 1 && handles == 1 && engine_init == 1 &&
                   engine_deinit == 0 && sys_init == 1 && sys_deinit == 0);
            delete node;
            assert(destroy_calls == 1);
            _exit(0);
        }
        int status = 0;
        assert(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    }
    // A destructor does not tear down a different runtime while the SDK is busy.
    {
        const pid_t child = fork();
        assert(child >= 0);
        if (child == 0) {
            Metadata(AX_ENGINE_DT_UINT8, AX_ENGINE_TENSOR_LAYOUT_NHWC);
            const char bytes[] = "mock";
            auto* first        = new AxeraNetNode;
            Configure(*first);
            first->SetInputContract(kAxeraRgbUint8InputContract);
            auto* second = new AxeraNetNode;
            Configure(*second);
            second->SetInputContract(kAxeraRgbUint8InputContract);
            auto* third = new AxeraNetNode;
            Configure(*third);
            third->SetInputContract(kAxeraRgbUint8InputContract);
            assert(first->LoadWeight(bytes, sizeof(bytes)) && second->LoadWeight(bytes, sizeof(bytes)) &&
                   third->LoadWeight(bytes, sizeof(bytes)));
            block_next_destroy = true;
            std::atomic<bool> closed{false};
            std::thread closer([&] { closed = bool(first->Close()); });
            {
                std::unique_lock<std::mutex> lock(destroy_mutex);
                destroy_cv.wait(lock, [] { return destroy_entered; });
            }
            delete second;
            assert(destroy_calls == 1 && handles == 3 && engine_deinit == 0 && sys_deinit == 0);
            {
                std::lock_guard<std::mutex> lock(destroy_mutex);
                destroy_release = true;
            }
            destroy_cv.notify_all();
            closer.join();
            assert(!closed && destroy_calls == 1 && handles == 2 && engine_deinit == 0 && sys_deinit == 0);
            assert(!third->Close() && destroy_calls == 1);
            delete third;
            assert(destroy_calls == 1 && handles == 2 && engine_deinit == 0 && sys_deinit == 0);
            for (int attempt = 0; attempt < 3; ++attempt) {
                AxeraNetNode next;
                Configure(next);
                next.SetInputContract(kAxeraRgbUint8InputContract);
                assert(!next.LoadWeight(bytes, sizeof(bytes)));
            }
            assert(destroy_calls == 1 && handles == 2 && engine_init == 1 && engine_deinit == 0 &&
                   sys_deinit == 0);
            delete first;
            _exit(0);
        }
        int status = 0;
        assert(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    }
#ifdef COSMO_AXERA_MOCK_VERIFIED_CONTRACT
    // Inject concurrent poison precisely inside last Engine/SYS deinit.
    // A phase already entered may finish; no later phase or new node may enter.
    for (int phase = 0; phase < 2; ++phase) {
        const pid_t child = fork();
        assert(child >= 0);
        if (child == 0) {
            Metadata(AX_ENGINE_DT_UINT8, AX_ENGINE_TENSOR_LAYOUT_NHWC);
            AxeraNetNode node;
            Configure(node);
            node.SetInputContract(kAxeraRgbUint8InputContract);
            const char bytes[] = "mock";
            assert(node.LoadWeight(bytes, sizeof(bytes)));
            if (phase == 0)
                engine_deinit_callback = [] { AxeraMockAbandonEngine(); };
            else
                sys_deinit_callback = [] { AxeraMockAbandonEngine(); };
            assert(!node.Close());
            assert(destroy_calls == 1 && engine_deinit == 1 && sys_deinit == phase);
            AxeraNetNode next;
            Configure(next);
            next.SetInputContract(kAxeraRgbUint8InputContract);
            assert(!next.LoadWeight(bytes, sizeof(bytes)));
            assert(destroy_calls == 1 && engine_deinit == 1 && sys_deinit == phase);
            _exit(0);
        }
        int status = 0;
        assert(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    }
#endif
    // Reentrant SDK callback is rejected before it can acquire the state lock.
    {
        const pid_t child = fork();
        assert(child >= 0);
        if (child == 0) {
            Metadata(AX_ENGINE_DT_UINT8, AX_ENGINE_TENSOR_LAYOUT_NHWC);
            AxeraNetNode node;
            Configure(node);
            node.SetInputContract(kAxeraRgbUint8InputContract);
            const char bytes[] = "mock";
            assert(node.LoadWeight(bytes, sizeof(bytes)));
            bool rejected    = false;
            destroy_callback = [&] {
                AxeraNetNode nested;
                Configure(nested);
                nested.SetInputContract(kAxeraRgbUint8InputContract);
                rejected = !nested.LoadWeight(bytes, sizeof(bytes));
            };
            assert(node.Close());
            assert(rejected && destroy_calls == 1 && engine_deinit == 1);
            _exit(0);
        }
        int status = 0;
        assert(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    }
    static_assert(std::is_const_v<std::remove_reference_t<decltype(std::declval<NativeTensor>().Desc())>>,
                  "tensor descriptor must be immutable after construction");
    const char model[] = "mock";
    Metadata(AX_ENGINE_DT_FLOAT32, AX_ENGINE_TENSOR_LAYOUT_NCHW);
    {
        AxeraNetNode node;
        Configure(node);
        assert(!node.LoadWeight(model, sizeof(model)));
    }
    Metadata(AX_ENGINE_DT_UINT8, AX_ENGINE_TENSOR_LAYOUT_NHWC);
    {
        AxeraNetNode node;
        Configure(node);
        assert(!node.LoadWeight(model, sizeof(model)));
        node.SetInputContract(kAxeraRgbUint8InputContract);
        assert(node.LoadWeight(model, sizeof(model)) && node.InferTopShapes());
        std::vector<unsigned char> input_memory(12), output_memory(8);
        auto desc = [](uint64_t physical, void* memory, size_t n, std::vector<int> shape,
                       std::vector<size_t> strides, NativeTensor::Element element,
                       NativeTensor::Layout layout) {
            NativeTensorDesc t;
            t.physical_base                 = physical;
            t.virtual_base                  = memory;
            t.allocation_bytes              = n;
            t.visible_bytes                 = n;
            t.required_alignment            = 1;
            t.shape                         = std::move(shape);
            t.byte_strides                  = std::move(strides);
            t.element                       = element;
            t.layout                        = layout;
            t.owner                         = std::make_shared<int>(1);
            t.contiguous_extent_verified    = true;
            t.lifetime_and_release_verified = true;
            t.device_visibility_verified    = true;
            t.completion_verified           = true;
            return t;
        };
        auto idesc = desc(0x10000, input_memory.data(), 12, {1, 2, 2, 3}, {12, 6, 3, 1},
                          NativeTensor::Element::UInt8, NativeTensor::Layout::NHWC);
        auto odesc = desc(0x20000, output_memory.data(), 8, {1, 2}, {8, 4}, NativeTensor::Element::Float32,
                          NativeTensor::Layout::Dense);
        idesc.input_contract             = kAxeraRgbUint8InputContract;
        odesc.hardware_consumer_verified = true;
        auto in  = std::make_shared<NativeTensor>(idesc, NativeTensor::State::DeviceReadable);
        auto out = std::make_shared<NativeTensor>(odesc, NativeTensor::State::DeviceWritable);
        BlobHandle ih, oh;
        ih.native_tensor = in;
        oh.native_tensor = out;
        BlobDesc id, od;
        id.dims = {1, 2, 2, 3};
        od.dims = {1, 2};
        auto ib = std::make_shared<Blob>(id, ih), ob = std::make_shared<Blob>(od, oh);
        std::vector<std::shared_ptr<Blob>> inputs{ib}, outputs{ob};
#ifndef COSMO_AXERA_MOCK_VERIFIED_CONTRACT
        assert(!node.Forward(inputs, outputs));
        assert(run_calls == 0);
#else
        auto invalid_input = [&](NativeTensorDesc d) {
            ih.native_tensor =
                std::make_shared<NativeTensor>(std::move(d), NativeTensor::State::DeviceReadable);
            ib->SetHandle(ih);
            assert(!node.Forward(inputs, outputs));
            ih.native_tensor = in;
            ib->SetHandle(ih);
        };
        auto bad = idesc;
        bad.owner.reset();
        invalid_input(bad);
        bad                            = idesc;
        bad.device_visibility_verified = false;
        invalid_input(bad);
        bad                 = idesc;
        bad.byte_strides[1] = 7;
        invalid_input(bad);
        bad              = idesc;
        bad.offset_bytes = 1;
        invalid_input(bad);
        auto invalid_output = [&](NativeTensorDesc d) {
            oh.native_tensor =
                std::make_shared<NativeTensor>(std::move(d), NativeTensor::State::DeviceWritable);
            ob->SetHandle(oh);
            assert(!node.Forward(inputs, outputs));
            oh.native_tensor = out;
            ob->SetHandle(oh);
        };
        bad                               = odesc;
        bad.lifetime_and_release_verified = false;
        invalid_output(bad);
        bad                            = odesc;
        bad.hardware_consumer_verified = false;
        invalid_output(bad);
        bad               = odesc;
        bad.physical_base = 0x10001;
        invalid_output(bad);
        int materialized      = 0;
        ih.host_data_provider = [&] {
            ++materialized;
            return input_memory.data();
        };
        ib->SetHandle(ih);
        assert(!node.Forward(inputs, outputs) && materialized == 0);
        ih.host_data_provider = {};
        ib->SetHandle(ih);
        {
            // IVPS holds the would-be NN input for writing.
            AxPhysicalLease ivps;
            assert(ivps.Acquire({{0x10000, 12, AxPhysicalAccess::Mode::Write}}, {idesc.owner}));
            assert(!node.Forward(inputs, outputs) && run_calls == 0);
        }
        {
            cosmo::media::AxPacketPool packet({{0x10000, input_memory.data(), 12, idesc.owner}});
            auto held = packet.Acquire();
            assert(held && !node.Forward(inputs, outputs) && run_calls == 0);
        }
        assert(node.Forward(inputs, outputs));
        assert(run_calls == 1 && out->GetState() == NativeTensor::State::DeviceReadable);
        assert(!node.Forward(inputs, outputs));
        out->MockSetState(NativeTensor::State::DeviceWritable);
        AxeraNetNode other;
        Configure(other);
        other.SetInputContract(kAxeraRgbUint8InputContract);
        assert(other.LoadWeight(model, sizeof(model)));
        auto in2  = std::make_shared<NativeTensor>(idesc, NativeTensor::State::DeviceReadable);
        auto out2 = std::make_shared<NativeTensor>(odesc, NativeTensor::State::DeviceWritable);
        BlobHandle ih2, oh2;
        ih2.native_tensor = in2;
        oh2.native_tensor = out2;
        auto ib2 = std::make_shared<Blob>(id, ih2), ob2 = std::make_shared<Blob>(od, oh2);
        std::vector<std::shared_ptr<Blob>> inputs2{ib2}, outputs2{ob2};
        // Deleting a third node during RunSync poisons admission immediately;
        // the in-flight output is failed and its physical lease is quarantined.
        {
            const pid_t child = fork();
            assert(child >= 0);
            if (child == 0) {
                auto* victim = new AxeraNetNode;
                Configure(*victim);
                victim->SetInputContract(kAxeraRgbUint8InputContract);
                assert(victim->LoadWeight(model, sizeof(model)));
                block_next = true;
                std::atomic<bool> running_ok{true};
                std::thread running([&] { running_ok = bool(node.Forward(inputs, outputs)); });
                {
                    std::unique_lock<std::mutex> lock(run_mutex);
                    run_cv.wait(lock, [] { return run_entered; });
                }
                const int before = destroy_calls;
                delete victim;
                assert(destroy_calls == before);
                AxeraNetNode during;
                Configure(during);
                during.SetInputContract(kAxeraRgbUint8InputContract);
                assert(!during.LoadWeight(model, sizeof(model)));
                {
                    std::lock_guard<std::mutex> lock(run_mutex);
                    run_release = true;
                }
                run_cv.notify_all();
                running.join();
                assert(!running_ok);
                assert(in->GetState() == NativeTensor::State::Failed &&
                       out->GetState() == NativeTensor::State::Failed);
                AxPhysicalLease blocked;
                assert(!blocked.Acquire({{0x10000, 12, AxPhysicalAccess::Mode::Read}}, {idesc.owner}));
                for (int i = 0; i < 3; ++i) {
                    AxeraNetNode next;
                    Configure(next);
                    next.SetInputContract(kAxeraRgbUint8InputContract);
                    assert(!next.LoadWeight(model, sizeof(model)));
                }
                _exit(0);
            }
            int status = 0;
            assert(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
        }
        block_next = true;
        std::atomic<bool> first_ok{false};
        std::thread worker([&] { first_ok = bool(node.Forward(inputs, outputs)); });
        {
            std::unique_lock<std::mutex> lock(run_mutex);
            run_cv.wait(lock, [] { return run_entered; });
        }
        assert(!other.Forward(inputs2, outputs2));  // output aliases across NN nodes
        {
            AxPhysicalLease ivps_write;
            assert(!ivps_write.Acquire({{0x10000, 12, AxPhysicalAccess::Mode::Write}}, {idesc.owner}));
            AxPhysicalLease ivps_read;
            assert(ivps_read.Acquire({{0x10000, 12, AxPhysicalAccess::Mode::Read}}, {idesc.owner}));
        }
        auto different          = odesc;
        different.physical_base = 0x30000;
        auto out3         = std::make_shared<NativeTensor>(different, NativeTensor::State::DeviceWritable);
        oh2.native_tensor = out3;
        ob2->SetHandle(oh2);
        assert(
            !other.Forward(inputs2, outputs2));  // SDK operations fail closed while another node is running
        const int destroy_before = destroy_calls;
        assert(!other.Close() && destroy_calls == destroy_before);  // no teardown during RunSync
        {
            std::lock_guard<std::mutex> lock(run_mutex);
            run_release = true;
        }
        run_cv.notify_all();
        worker.join();
        assert(first_ok);
        oh2.native_tensor = out2;
        ob2->SetHandle(oh2);
        assert(other.Forward(inputs2, outputs2));  // previous lease released
        out->MockSetState(NativeTensor::State::DeviceWritable);
        fail_run = std::getenv("AX_TEST_ENGINE_THROW") ? 2 : 1;
        assert(!node.Forward(inputs, outputs));
        assert(in->GetState() == NativeTensor::State::Failed &&
               out->GetState() == NativeTensor::State::Failed);
        fail_run = false;
        assert(!node.LoadWeight(model, sizeof(model)) && !node.Forward(inputs, outputs));
        AxeraNetNode another;
        Configure(another);
        another.SetInputContract(kAxeraRgbUint8InputContract);
        assert(!another.LoadWeight(model, sizeof(model)));  // unknown completion poisons all nodes
        in2->MockSetState(NativeTensor::State::DeviceReadable);
        out2->MockSetState(NativeTensor::State::DeviceWritable);
        assert(!other.Forward(inputs2, outputs2));
        AxPhysicalLease ivps_after_failure;
        assert(!ivps_after_failure.Acquire({{0x10000, 12, AxPhysicalAccess::Mode::Read}}, {idesc.owner}));
#endif
    }
#ifndef COSMO_AXERA_MOCK_VERIFIED_CONTRACT
    assert(allocations == 0 && handles == 0 && sys_init == sys_deinit && engine_init == engine_deinit);
#else
    // Failed RunSync permanently retains its handle, engine and system lease.
    assert(allocations == 0 && handles == 2 && sys_init == sys_deinit + 1 &&
           engine_init == engine_deinit + 1);
#endif
}
