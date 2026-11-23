#pragma once
#ifdef COSMO_NN_USE_AXERA_BACKEND
#include <ax_engine_api.h>
#include <ax_sys_api.h>

#include <array>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "media/NativeVideoBuffer.h"
#include "nn/device/axera/axera_face_model_sdk_loader.h"
#include "nn/device/axera/axera_native_execution_state.h"
#include "nn/device/axera/benchmark/BenchHistoryNv12.h"
#include "nn/device/axera/benchmark/postprocess.h"
namespace cosmo::nn::axera::benchmark {
bool VerifiedPersonModelBytes(const std::vector<std::uint8_t>& bytes);
class PersonExecutor {
    struct Buffer {
        AX_U64 physical{};
        void* host{};
        AX_U32 size{};
    };
    struct Pending {
        std::shared_ptr<face_binding::SdkLoadedModel> model;
        media::NativeVideoBufferPtr source;
        std::array<Buffer, 3> buffers{};
        AX_ENGINE_IO_BUFFER_T input{};
        std::array<AX_ENGINE_IO_BUFFER_T, 2> output{};
        AX_ENGINE_IO_T io{};
    };
    std::shared_ptr<face_binding::SdkLoadedModel> model_;
    std::unique_ptr<BenchHistoryNv12> history_;
    std::string history_path_;
    std::thread::id owner_;
    static bool same(const AX_ENGINE_IOMETA_T& m, const char* name, AX_ENGINE_DATA_TYPE_T type,
                     std::initializer_list<int> dims, unsigned bytes) {
        if (!m.pName || std::strcmp(m.pName, name) || m.eDataType != type || m.nSize != bytes ||
            m.nShapeSize != dims.size() || !m.pShape)
            return false;
        std::size_t i = 0;
        for (int d : dims)
            if (m.pShape[i++] != d)
                return false;
        return true;
    }
    static bool metadata(AX_ENGINE_HANDLE h) {
        AX_ENGINE_IO_INFO_T* m = nullptr;
        return AX_ENGINE_GetIOInfo(h, &m) == 0 && m && m->nInputSize == 1 && m->nOutputSize == 2 &&
               m->pInputs && m->pOutputs && m->pInputs[0].eLayout == AX_ENGINE_TENSOR_LAYOUT_NHWC &&
               same(m->pInputs[0], "images", AX_ENGINE_DT_UINT8, {1, 640, 640, 3}, 640 * 640 * 3) &&
               same(m->pOutputs[0], "/model.22/Mul_2_output_0", AX_ENGINE_DT_FLOAT32, {1, 4, 8400},
                    4 * 8400 * 4) &&
               same(m->pOutputs[1], "/model.22/Sigmoid_output_0", AX_ENGINE_DT_FLOAT32, {1, 80, 8400},
                    80 * 8400 * 4);
    }

public:
    static std::unique_ptr<PersonExecutor> Load(std::vector<std::uint8_t> bytes) {
        if (!VerifiedPersonModelBytes(bytes))
            return {};
        auto owned = std::make_shared<const std::vector<std::uint8_t>>(std::move(bytes));
        face_binding::SdkLoader loader;
        auto model = std::dynamic_pointer_cast<face_binding::SdkLoadedModel>(loader.Load(owned));
        if (!model)
            return {};
        auto handle = model->HandleForOwnerThread();
        auto& state = detail::NativeState();
        {
            std::lock_guard<std::mutex> lock(state.mutex);
            if (state.poisoned || !handle || !metadata(handle))
                return {};
        }
        auto out    = std::unique_ptr<PersonExecutor>(new PersonExecutor);
        out->model_ = std::move(model);
        out->owner_ = std::this_thread::get_id();
        return out;
    }
    BenchHistoryNv12* Historical(const char* path) {
        if (!path || !*path)
            return nullptr;
        if (history_path_.empty()) {
            history_path_ = path;
            history_      = BenchHistoryNv12::Load(path);
        }
        return history_path_ == path ? history_.get() : nullptr;
    }
    bool HistoricalWasLoaded() const {
        return !history_path_.empty();
    }
    bool CanReuse() const {
        if (!model_ || owner_ != std::this_thread::get_id())
            return false;
        return model_->HandleForOwnerThread() != nullptr;
    }
    bool Run(media::NativeVideoBufferPtr source, const std::vector<std::uint8_t>& rgb,
             std::vector<::benchmark_ax::Detection>& result) {
        if (!source || !source->Valid() || !source->frame_witness ||
            !source->frame_witness->Matches(*source) ||
            source->color_space != media::NativeVideoColorSpace::Bt709 ||
            source->color_range != media::NativeVideoColorRange::Limited || rgb.size() != 640 * 640 * 3 ||
            owner_ != std::this_thread::get_id())
            return false;
        if (!model_)
            return false;
        auto handle = model_->HandleForOwnerThread();
        if (!handle)
            return false;
        auto pending             = std::make_shared<Pending>();
        pending->model           = model_;
        pending->source          = std::move(source);
        pending->buffers[0].size = 640 * 640 * 3;
        pending->buffers[1].size = 4 * 8400 * 4;
        pending->buffers[2].size = 80 * 8400 * 4;
        auto& state              = detail::NativeState();
        std::lock_guard<std::mutex> lock(state.mutex);
        if (state.poisoned || !model_)
            return false;
        auto quarantine = [&]() {
            state.poisoned        = true;
            static auto* retained = new std::vector<std::shared_ptr<Pending>>;
            retained->push_back(pending);
        };
        for (std::size_t i = 0; i < pending->buffers.size(); ++i) {
            auto& b       = pending->buffers[i];
            const auto rc = i == 0 ? AX_SYS_MemAlloc(&b.physical, &b.host, b.size, 128,
                                                     reinterpret_cast<const AX_S8*>("cosmo-bench"))
                                   : AX_SYS_MemAllocCached(&b.physical, &b.host, b.size, 128,
                                                           reinterpret_cast<const AX_S8*>("cosmo-bench"));
            if (rc || !b.physical || !b.host) {
                if (b.physical || b.host) {
                    quarantine();
                    return false;
                }
                for (auto& a : pending->buffers)
                    if (a.physical && AX_SYS_MemFree(a.physical, a.host)) {
                        quarantine();
                        return false;
                    }
                return false;
            }
        }
        std::memcpy(pending->buffers[0].host, rgb.data(), rgb.size());
        auto& in    = pending->input;
        auto& out   = pending->output;
        in.phyAddr  = pending->buffers[0].physical;
        in.pVirAddr = pending->buffers[0].host;
        in.nSize    = pending->buffers[0].size;
        for (int i = 0; i < 2; ++i) {
            out[i].phyAddr  = pending->buffers[i + 1].physical;
            out[i].pVirAddr = pending->buffers[i + 1].host;
            out[i].nSize    = pending->buffers[i + 1].size;
        }
        auto& io       = pending->io;
        io.pInputs     = &in;
        io.nInputSize  = 1;
        io.pOutputs    = out.data();
        io.nOutputSize = 2;
        if (AX_ENGINE_RunSync(handle, &io) != 0) {
            quarantine();
            return false;
        }
        for (std::size_t i = 1; i < pending->buffers.size(); ++i) {
            auto& b = pending->buffers[i];
            if (AX_SYS_MinvalidateCache(b.physical, b.host, b.size) != 0) {
                quarantine();
                return false;
            }
        }
        bool parsed = true;
        try {
            std::vector<float> box(4 * 8400), scores(80 * 8400);
            std::memcpy(box.data(), out[0].pVirAddr, out[0].nSize);
            std::memcpy(scores.data(), out[1].pVirAddr, out[1].nSize);
            result = ::benchmark_ax::person(box.data(), box.size(), scores.data(), scores.size(),
                                            pending->source->width, pending->source->height);
        } catch (...) {
            result.clear();
            parsed = false;
        }
        bool ok = true;
        for (auto& b : pending->buffers)
            if (AX_SYS_MemFree(b.physical, b.host) != 0)
                ok = false;
        if (!ok) {
            quarantine();
            return false;
        }
        return parsed;
    }
};
}  // namespace cosmo::nn::axera::benchmark
#endif
