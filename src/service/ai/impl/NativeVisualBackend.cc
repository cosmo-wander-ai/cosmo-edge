#include "service/ai/impl/NativeVisualBackend.h"

#include <algorithm>
#include <cstdlib>
#include <memory>
#include <stdexcept>

#include "stb/stb_image.h"

namespace cosmo::service {
namespace {
    using visual::Json;
    std::string Hash(const std::string& text) {
        return visual::Sha256(reinterpret_cast<const uint8_t*>(text.data()), text.size());
    }
    VideoFramePtr Decode(const VisualDecisionImage& image) {
        int width = 0, height = 0, channels = 0;
        if (!stbi_info_from_memory(image.jpeg.data(), int(image.jpeg.size()), &width, &height, &channels) ||
            width != image.width || height != image.height)
            throw std::runtime_error("invalid_roi_image");
        std::unique_ptr<unsigned char, decltype(&stbi_image_free)> pixels(
            stbi_load_from_memory(image.jpeg.data(), int(image.jpeg.size()), &width, &height, &channels, 3),
            stbi_image_free);
        if (!pixels)
            throw std::runtime_error("invalid_roi_image");
        auto frame =
            std::make_shared<cosmo::media::VideoFrame>(width, height, cosmo::media::PixelFormat::PIXEL_BGR8);
        if (!VideoFrameValid(frame))
            throw std::runtime_error("roi_allocation_failed");
        auto* host = static_cast<uint8_t*>(std::malloc(size_t(width) * height * 3));
        if (!host)
            throw std::bad_alloc();
        frame->SetHostData(host);
        for (size_t i = 0; i < size_t(width) * height; ++i) {
            host[i * 3]     = pixels.get()[i * 3 + 2];
            host[i * 3 + 1] = pixels.get()[i * 3 + 1];
            host[i * 3 + 2] = pixels.get()[i * 3];
        }
        return frame;
    }
}  // namespace

VisualQuestionServiceImpl::NativeCompiler NativeVisualCompiler(ILlmInferService& llm) {
    return [&llm](const auto& specs, const auto& run) {
        VisualQuestionPreparation result;
        if (run->atomicCode.empty()) {
            result.reason = "model_not_bound";
            return result;
        }
        if (!run->AcquireModel([&llm] { llm.NotifyWorkerStart(); }, [&llm] { llm.NotifyWorkerStop(); })) {
            result.reason = "stale_task_run";
            return result;
        }
        Json request{{"questions", Json::array()}};
        for (const auto& spec : specs)
            request["questions"].push_back({{"question", spec.question}, {"text_state", spec.textState}});
        const auto input = request.dump();
        std::string output;
        if (!llm.PrepareText(run->atomicCode, input, output, result.manifestSha256)) {
            result.reason = "native_model_prepare_failed";
            return result;
        }
        auto prepared = Json::parse(output);
        if (prepared.at("backend") != "laya_v" || prepared.at("questions").size() != specs.size())
            throw std::runtime_error("invalid_native_preparation");
        for (size_t i = 0; i < specs.size(); ++i) {
            const auto& spec = specs[i];
            const auto& row  = prepared.at("questions")[i];
            const Json nativeSpec{{"question", spec.question}, {"text_state", spec.textState}};
            // A ref binds the exact question, token sequence and imported model assets.
            auto hash =
                Hash(Json{{"model", result.manifestSha256}, {"spec", nativeSpec}, {"compiled", row}}.dump());
            VisualQuestionRef ref{
                spec.itemId,          spec.question.at("id"),    spec.question.at("version"), hash,
                row.at("qtype"),      row.at("ordered_options"), row.at("temperature"),       nativeSpec,
                result.manifestSha256};
            if (!visual::ValidQuestion(ref))
                throw std::runtime_error("invalid_native_preparation");
            result.questions.push_back(std::move(ref));
        }
        result.inputSha256 = Hash(input);
        result.ready       = true;
        return result;
    };
}

VisualDecisionServiceImpl::NativeInference NativeVisualInference(ILlmInferService& llm) {
    return [&llm](const auto& identity, const auto& questions, const auto& run, const auto& image,
                  auto deadline) {
        auto frame = Decode(image);
        if (visual::Clock::now() >= deadline)
            return visual::Failure(identity, "deadline_exceeded");
        Json request{{"laya_questions", Json::array()}};
        for (const auto& question : questions)
            request["laya_questions"].push_back(question.nativeSpec);
        std::vector<Qwen3VLResult> results;
        const auto& model = questions.front().modelIdentity;
        if (llm.GenerateBound(run->atomicCode, model, {frame}, {request.dump()}, results) !=
                util::ErrorEnum::Success ||
            results.size() != 1)
            return visual::Failure(identity, "native_inference_failed");
        const auto output = Json::parse(results.front().text);
        if (output.at("backend") != "laya_v" || output.at("items").size() != questions.size())
            return visual::Failure(identity, "invalid_native_response");
        auto response         = identity;
        response["backend"]   = "laya_v";
        response["execution"] = "in_engine";
        response["status"]    = "completed";
        response["items"]     = output.at("items");
        for (size_t i = 0; i < questions.size(); ++i) {
            auto& row = response["items"][i];
            if (row.at("question_id") != questions[i].questionId ||
                row.at("question_version") != questions[i].questionVersion)
                return visual::Failure(identity, "native_question_mismatch");
            row["item_id"]         = questions[i].itemId;
            row["compiled_sha256"] = questions[i].compiledSha256;
        }
        const auto imageSha = visual::Sha256(image.jpeg.data(), image.jpeg.size());
        const visual::Release release{model, {{"native_assets_sha256", model}}};
        response["model_hashes"] = release.modelHashes;
        response["image_sha256"] = imageSha;
        return visual::ValidateResponse(identity, questions, release, imageSha, response, true);
    };
}
}  // namespace cosmo::service
