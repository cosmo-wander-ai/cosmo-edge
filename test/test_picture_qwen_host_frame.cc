#include "catch_amalgamated.hpp"

#if defined(COSMO_MEDIA_USE_CPU_BACKEND)
#include "flow/qwen3vl/PQwen3VLWorker.h"
#include "media/IOsdTextRenderer.h"
#include "mem/AllocatorCpu.h"
#include "mem/IDeviceContext.h"
#include "mem/MemoryPoolMng.h"
#include "service/ai/ILlmInferService.h"
#include "service/media/impl/VideoFrameServiceImpl.h"
#include "support/ScopedServiceOverride.h"

namespace {
class PictureDeviceContext final : public cosmo::mem::IDeviceContext {
public:
    void* GetMemoryHandle() override {
        return nullptr;
    }
    void* GetMediaHandle() override {
        return nullptr;
    }
};
class PictureTextRenderer final : public cosmo::media::IOsdTextRenderer {
public:
    bool Init(const std::string&) override {
        return true;
    }
    bool IsReady() const override {
        return false;
    }
    TextBitmap RenderString(const std::string&, float) const override {
        return {};
    }
    OutlinedTextBitmap RenderStringWithOutline(const std::string&, float) const override {
        return {};
    }
};
class PictureLlm final : public cosmo::service::ILlmInferService {
public:
    int calls{0};
    bool EnsureInit(const std::string&) override {
        return true;
    }
    bool IsInitialized() const override {
        return true;
    }
    cosmo::util::ErrorEnum Generate(const std::vector<VideoFramePtr>& images,
                                    const std::vector<std::string>& prompts,
                                    const cosmo::Qwen3VLGenerationParam&,
                                    std::vector<cosmo::Qwen3VLResult>& results) override {
        ++calls;
        REQUIRE(images.size() == 1);
        REQUIRE(prompts.size() == 1);
        REQUIRE(images.front()->GetData() != nullptr);
        cosmo::Qwen3VLResult result;
        result.text = "yes";
        results.push_back(result);
        return cosmo::util::ErrorEnum::Success;
    }
    cosmo::util::ErrorEnum GetMaxBatchSize(size_t& value) const override {
        value = 1;
        return cosmo::util::ErrorEnum::Success;
    }
    void Reset() override {}
    void NotifyWorkerStart() override {}
    void NotifyWorkerStop() override {}
};
class PictureTransform final : public cosmo::service::VideoFrameServiceImpl {
public:
    bool fail_transfer{false};
    bool EnsureHostData(VideoFramePtr frame) override {
        return !fail_transfer && VideoFrameServiceImpl::EnsureHostData(frame);
    }
};
}  // namespace
#endif

TEST_CASE("Picture Qwen accepts host-backed frames and rejects failed transfers", "[qwen][picture][host]") {
#if !defined(COSMO_MEDIA_USE_CPU_BACKEND)
    SKIP("CPU frame processor required");
#else
    PictureDeviceContext device;
    PictureTextRenderer renderer;
    cosmo::test::ScopedServiceOverride<cosmo::mem::IDeviceContext> device_registration(device);
    cosmo::test::ScopedServiceOverride<cosmo::media::IOsdTextRenderer> renderer_registration(renderer);
    cosmo::mem::MemoryPoolMng pool(std::make_unique<cosmo::mem::AllocatorCpu>(), {64 * 64 * 3});
    cosmo::mem::SetMemoryPoolContext(&pool);
    struct ContextReset {
        ~ContextReset() {
            cosmo::mem::SetMemoryPoolContext(nullptr);
        }
    } reset;
    PictureTransform transform;
    PictureLlm llm;
    cosmo::test::ScopedServiceOverride<cosmo::service::IVideoFrameTransform> transform_registration(
        transform);
    cosmo::test::ScopedServiceOverride<cosmo::service::ILlmInferService> llm_registration(llm);
    cosmo::ActionNode action{};
    cosmo::PQwen3VLWorker worker(action, "picture-host-regression");
    auto data = std::make_shared<cosmo::AlgData>();
    data->chanDataDec.frame =
        std::make_shared<cosmo::media::VideoFrame>(64, 64, cosmo::media::PixelFormat::PIXEL_BGR8);
    REQUIRE(data->chanDataDec.frame->Active());
    REQUIRE(data->chanDataDec.frame->GetData() != nullptr);
    REQUIRE(data->chanDataDec.frame->GetHostData() == nullptr);
    REQUIRE(transform.EnsureHostData(data->chanDataDec.frame));

    SECTION("CPU and RK frames reach inference without a separate host copy") {
        CHECK(worker.HandPic(data) == cosmo::util::ErrorEnum::Success);
        CHECK(llm.calls == 1);
        REQUIRE(data->chanDataDetect.detRet);
        REQUIRE(data->chanDataDetect.detRet->targets.size() == 1);
        CHECK(data->chanDataDetect.detRet->targets.front().classifyRst.front().label == "yes");
    }
    SECTION("A failed host transfer never falls through to inference") {
        transform.fail_transfer = true;
        CHECK(worker.HandPic(data) == cosmo::util::ErrorEnum::InvalidParam);
        CHECK(llm.calls == 0);
    }
    SECTION("An inactive frame is rejected") {
        data->chanDataDec.frame =
            std::make_shared<cosmo::media::VideoFrame>(0, 0, cosmo::media::PixelFormat::PIXEL_BGR8);
        REQUIRE_FALSE(data->chanDataDec.frame->Active());
        CHECK(worker.HandPic(data) == cosmo::util::ErrorEnum::InvalidParam);
        CHECK(llm.calls == 0);
    }
#endif
}
