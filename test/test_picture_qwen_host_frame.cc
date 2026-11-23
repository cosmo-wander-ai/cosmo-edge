#include "catch_amalgamated.hpp"

#if defined(COSMO_MEDIA_USE_CPU_BACKEND)
#include <algorithm>

#include "flow/qwen3vl/PQwen3VLWorker.h"
#include "media/IOsdTextRenderer.h"
#include "mem/AllocatorCpu.h"
#include "mem/IDeviceContext.h"
#include "mem/MemoryPoolMng.h"
#include "service/ai/ILlmInferService.h"
#include "service/media/impl/VideoFrameServiceImpl.h"
#include "service/path/impl/FileServiceImpl.h"
#include "support/ScopedServiceOverride.h"

#define private public
#include "flow/task/PTaskBase.h"
#undef private

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
    std::string reply{"是"};
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
        result.text = reply;
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

class PictureOverlay final : public cosmo::service::VideoFrameServiceImpl {
public:
    VideoFramePtr CopyJpegSrcFrame(VideoFramePtr frame) override {
        ++copies;
        return copies == 1 ? frame : synced_frame;
    }
    VideoFramePtr DrawLines(VideoFramePtr frame,
                            std::vector<std::pair<cosmo::util::Point, cosmo::util::Point>>,
                            const cosmo::media::Color& color, int) override {
        draw_colors.push_back(color);
        if (color.red == 255)
            data->chanDataDetect.detRet = next_detections;
        return frame;
    }
    bool EnsureHostData(VideoFramePtr) override {
        ++host_transfers;
        return true;
    }
    std::vector<u_char> EncodeJpeg(const VideoFramePtr frame, int) override {
        encoded_frame = frame;
        return {1, 2, 3};
    }

    cosmo::AlgDataPtr data;
    cosmo::DataDetTrackClassifyPtr next_detections;
    VideoFramePtr synced_frame, encoded_frame;
    std::vector<cosmo::media::Color> draw_colors;
    int copies{0};
    int host_transfers{0};
};
}  // namespace

TEST_CASE("Full picture overlays reread detections before the next drawing stage", "[picture][host]") {
    using namespace cosmo;

    PictureDeviceContext device;
    PictureTextRenderer renderer;
    test::ScopedServiceOverride<mem::IDeviceContext> device_registration(device);
    test::ScopedServiceOverride<media::IOsdTextRenderer> renderer_registration(renderer);
    mem::MemoryPoolMng pool(std::make_unique<mem::AllocatorCpu>(), {64 * 64 * 3});
    mem::SetMemoryPoolContext(&pool);
    struct ContextReset {
        ~ContextReset() {
            mem::SetMemoryPoolContext(nullptr);
        }
    } reset;

    const bool i420   = GENERATE(false, true);
    const bool empty  = GENERATE(false, true);
    const auto format = i420 ? media::PixelFormat::PIXEL_I420 : media::PixelFormat::PIXEL_BGR8;
    auto original     = std::make_shared<media::VideoFrame>(64, 64, format);
    REQUIRE(original->Active());
    const auto data_size = i420 ? 64 * 64 * 3 / 2 : 64 * 64 * 3;
    std::fill_n(original->GetData(), data_size, 0);

    PictureOverlay overlay;
    overlay.data                    = std::make_shared<AlgData>();
    overlay.data->chanDataDec.frame = original;
    if (!empty) {
        overlay.data->chanDataDetect.detRet = std::make_shared<DataDetTrackClassify>();
        overlay.data->chanDataDetect.detRet->targets.emplace_back().box = {4, 4, 20, 20};
        overlay.next_detections = std::make_shared<DataDetTrackClassify>();
        auto& target            = overlay.next_detections->targets.emplace_back();
        target.landmark.landmark.emplace_back(10, 10);
        target.mask = {std::vector<uint8_t>(64 * 64, 255), 64, 64};
    }
    overlay.synced_frame = std::make_shared<media::VideoFrame>(64, 64, format);
    REQUIRE(overlay.synced_frame->Active());
    service::FileServiceImpl files;
    test::ScopedServiceOverride<service::IFileService> file_registration(files);
    test::ScopedServiceOverride<service::IVideoFrameOSD> osd_registration(overlay);
    test::ScopedServiceOverride<service::IVideoFrameTransform> transform_registration(overlay);
    test::ScopedServiceOverride<service::IVideoFrameCodec> codec_registration(overlay);

    PTaskBase task;
    MsgPTaskDetectPicRecv request;
    MsgPTaskDetectPicSend response;
    task.DetTargetHandFullPicture(overlay.data, request, response);

    CHECK(response.resData.fullPicture == "data:image/jpeg;base64,AQID");
    if (empty) {
        CHECK(overlay.draw_colors.empty());
    } else {
        REQUIRE(overlay.draw_colors.size() == 2);
        CHECK(overlay.draw_colors[0].red == 255);
        CHECK(overlay.draw_colors[1].green == 255);
        CHECK(original->GetData()[i420 ? 0 : 1] != 0);
    }
    const bool synced = i420 && !empty;
    CHECK(overlay.host_transfers == (synced ? 1 : 0));
    CHECK(overlay.copies == (synced ? 2 : 1));
    CHECK(overlay.encoded_frame == (synced ? overlay.synced_frame : original));
}
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
    action.flowActionId = "vlm";
    action.atomicCode   = "picture-model";
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
        CHECK(data->chanDataDetect.detRet->targets.front().classifyRst.front().label == "是");
        REQUIRE(data->vlmResults.size() == 1);
        CHECK(data->vlmResults.front().answer == "是");
        CHECK(data->vlmResults.front().nodeId == "vlm");
        CHECK(data->vlmResults.front().modelCode == "picture-model");
        CHECK(data->vlmResults.front().durationMs >= 0);
    }
    SECTION("Negative and ambiguous replies are not positive decisions") {
        llm.reply = "否";
        REQUIRE(worker.HandPic(data) == cosmo::util::ErrorEnum::Success);
        CHECK(data->pictureDecision == "not_matched");
        CHECK_FALSE(data->chanDataDetect.detRet->targets.front().bLogicResult);
        REQUIRE(data->vlmResults.size() == 1);
        CHECK(data->vlmResults.front().answer == "否");
        CHECK(data->vlmResults.front().decision == "not_matched");
        llm.reply = "uncertain";
        REQUIRE(worker.HandPic(data) == cosmo::util::ErrorEnum::Success);
        CHECK(data->pictureDecision == "unknown");
        CHECK(data->vlmResults.back().answer == "uncertain");
        CHECK(data->vlmResults.back().decision == "unknown");
        CHECK_FALSE(data->chanDataDetect.detRet->targets.front().bLogicResult);
    }
    SECTION("Target crops preserve original target identity") {
        cosmo::MsgDynamicKeyValue param;
        param.key   = "inputType";
        param.value = "targets";
        std::vector<cosmo::MsgDynamicKeyValue> params{param};
        REQUIRE(worker.SetParam("picture-host-regression", params));
        data->chanDataDetect.detRet = std::make_shared<cosmo::DataDetTrackClassify>();
        cosmo::AiDetectRstEl target;
        target.targetId = "detector:0";
        target.box      = cosmo::util::Box(8, 8, 32, 32);
        data->chanDataDetect.detRet->targets.push_back(target);
        REQUIRE(worker.HandPic(data) == cosmo::util::ErrorEnum::Success);
        CHECK(llm.calls == 1);
        CHECK(data->chanDataDetect.detRet->targets.front().targetId == "detector:0");
        CHECK(data->pictureDecisions.at("detector:0") == "matched");
        CHECK(data->pictureDecision.empty());
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
