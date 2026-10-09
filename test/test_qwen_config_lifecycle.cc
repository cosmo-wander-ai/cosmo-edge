#include "catch_amalgamated.hpp"

#if defined(COSMO_MEDIA_USE_CPU_BACKEND)
#include <condition_variable>
#include <map>
#include <mutex>

#include "flow/qwen3vl/Qwen3VLWorker.h"
#include "media/IOsdTextRenderer.h"
#include "mem/AllocatorCpu.h"
#include "mem/IDeviceContext.h"
#include "mem/MemoryPoolMng.h"
#include "service/ai/ILlmInferService.h"
#include "service/media/impl/VideoFrameServiceImpl.h"
#include "support/ScopedServiceOverride.h"

namespace {
class ConfigDevice final : public cosmo::mem::IDeviceContext {
public:
    void* GetMemoryHandle() override {
        return nullptr;
    }
    void* GetMediaHandle() override {
        return nullptr;
    }
};
class ConfigText final : public cosmo::media::IOsdTextRenderer {
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
class ConfigFrames final : public cosmo::service::VideoFrameServiceImpl {
public:
    VideoFramePtr CopyJpegSrcFrame(VideoFramePtr frame) override {
        return frame;
    }
};
class ConfigLlm final : public cosmo::service::ILlmInferService {
public:
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
        std::lock_guard<std::mutex> lock(mutex);
        for (size_t i = 0; i < images.size() && i < prompts.size(); ++i) {
            seen[images[i]->GetFrameIndex()] = prompts[i];
            cosmo::Qwen3VLResult result;
            result.text = "no";
            results.push_back(result);
        }
        ready.notify_all();
        return cosmo::util::ErrorEnum::Success;
    }
    cosmo::util::ErrorEnum GetMaxBatchSize(size_t& count) const override {
        count = 1;
        return cosmo::util::ErrorEnum::Success;
    }
    void Reset() override {}
    void NotifyWorkerStart() override {}
    void NotifyWorkerStop() override {}
    std::string Wait(int64_t index) {
        std::unique_lock<std::mutex> lock(mutex);
        if (!ready.wait_for(lock, std::chrono::seconds(3), [&] { return seen.count(index) != 0; }))
            return "INFERENCE_NOT_OBSERVED";
        return seen.at(index);
    }

private:
    std::mutex mutex;
    std::condition_variable ready;
    std::map<int64_t, std::string> seen;
};
cosmo::MsgDynamicKeyValue Param(const std::string& key, const std::string& value) {
    cosmo::MsgDynamicKeyValue result;
    result.key   = key;
    result.value = value;
    result.keys  = {key};
    return result;
}
}  // namespace
#endif

TEST_CASE("Video VLM configuration replacement preserves other tasks and honors explicit clears",
          "[qwen][config-lifecycle]") {
#if !defined(COSMO_MEDIA_USE_CPU_BACKEND)
    SKIP("CPU frame processor required");
#else
    ConfigDevice device;
    ConfigText text;
    cosmo::test::ScopedServiceOverride<cosmo::mem::IDeviceContext> device_registration(device);
    cosmo::test::ScopedServiceOverride<cosmo::media::IOsdTextRenderer> text_registration(text);
    cosmo::mem::MemoryPoolMng pool(std::make_unique<cosmo::mem::AllocatorCpu>(), {64 * 64 * 3});
    cosmo::mem::SetMemoryPoolContext(&pool);
    struct ResetPool {
        ~ResetPool() {
            cosmo::mem::SetMemoryPoolContext(nullptr);
        }
    } reset;
    ConfigFrames frames;
    ConfigLlm llm;
    cosmo::test::ScopedServiceOverride<cosmo::service::IVideoFrameTransform> transform_registration(frames);
    cosmo::test::ScopedServiceOverride<cosmo::service::IVideoFrameOSD> osd_registration(frames);
    cosmo::test::ScopedServiceOverride<cosmo::service::ILlmInferService> llm_registration(llm);
    cosmo::ActionNode action{};
    cosmo::Qwen3VLWorker worker(action);
    REQUIRE(worker.AddTask("camera-a", "task-a"));
    REQUIRE(worker.AddTask("camera-b", "task-b"));
    auto first =
        std::vector<cosmo::MsgDynamicKeyValue>{Param("keywords", "helmet"), Param("advanced_mode", "true")};
    auto second =
        std::vector<cosmo::MsgDynamicKeyValue>{Param("keywords", "smoke"), Param("advanced_mode", "false")};
    REQUIRE(worker.ModifyParam("camera-a", "task-a", first));
    REQUIRE(worker.ModifyParam("camera-b", "task-b", second));
    REQUIRE(worker.Start());
    int64_t sequence = 0;
    auto infer       = [&](const std::string& task, const std::string& camera) {
        auto data       = std::make_shared<cosmo::AlgData>();
        data->taskId    = task;
        data->channelId = camera;
        data->chanDataDec.frame =
            std::make_shared<cosmo::media::VideoFrame>(64, 64, cosmo::media::PixelFormat::PIXEL_BGR8);
        data->chanDataDec.frame->SetFrameIndex(++sequence);
        REQUIRE(data->chanDataDec.frame->Active());
        REQUIRE(worker.GetQueue()->Insert(data));
        return llm.Wait(sequence);
    };
    const std::string suffix = "，回答是或者否,不要换行，不要其他内容。";
    CHECK(infer("task-a", "camera-a") == "helmet" + suffix);
    CHECK(infer("task-b", "camera-b") == "判断图片中是否存在【smoke】目标" + suffix);

    SECTION("Explicit empty and false replace old nonempty and true") {
        auto clear =
            std::vector<cosmo::MsgDynamicKeyValue>{Param("keywords", ""), Param("advanced_mode", "false")};
        REQUIRE(worker.SetParam("camera-a", "task-a", clear));
        CHECK(infer("task-a", "camera-a") == "判断图片中是否存在【目标】目标" + suffix);
        CHECK(infer("task-b", "camera-b") == "判断图片中是否存在【smoke】目标" + suffix);
    }
    SECTION("Full replacement resets omitted fields on only the named task") {
        auto replace = std::vector<cosmo::MsgDynamicKeyValue>{Param("keywords", "fire")};
        REQUIRE(worker.SetParam("camera-a", "task-a", replace));
        CHECK(infer("task-a", "camera-a") == "判断图片中是否存在【fire】目标" + suffix);
        CHECK(infer("task-b", "camera-b") == "判断图片中是否存在【smoke】目标" + suffix);
    }
    SECTION("Partial edits retain omitted fields") {
        auto patch = std::vector<cosmo::MsgDynamicKeyValue>{Param("keywords", "fire")};
        REQUIRE(worker.ModifyParam("camera-a", "task-a", patch));
        CHECK(infer("task-a", "camera-a") == "fire" + suffix);
        CHECK(infer("task-b", "camera-b") == "判断图片中是否存在【smoke】目标" + suffix);
    }
    SECTION("Removed task cannot inherit its previous configuration when re-added") {
        REQUIRE(worker.RemoveTask("camera-a", "task-a"));
        REQUIRE(worker.AddTask("camera-a", "task-a"));
        CHECK(infer("task-a", "camera-a") == "判断图片中是否存在【目标】目标" + suffix);
        CHECK(infer("task-b", "camera-b") == "判断图片中是否存在【smoke】目标" + suffix);
    }
    worker.Stop();
#endif
}
