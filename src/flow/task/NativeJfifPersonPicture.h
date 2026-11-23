#pragma once
#ifdef COSMO_NN_USE_AXERA_BACKEND
#include <cmath>
#include <fstream>
#include <iterator>

#include "flow/task/PTaskBase.h"
#include "nn/device/axera/axera_face_model_sdk_loader.h"
#include "nn/device/axera/axera_jfif_person_contract.h"
#include "nn/device/axera/axera_jfif_person_extractor.h"
#include "nn/device/axera/axera_route_identity.h"
#include "service/detail/ServiceRegistry.h"
#include "service/path/IFileService.h"
#include "util/CipherUtil.h"
#include "util/PathUtil.h"

namespace cosmo::native_jfif_person_picture {
inline util::ErrorEnum Detect(const PTaskElementPtr& task, MsgPictureInput& input,
                              const MsgPTaskDetectPicRecv& request, MsgPTaskDetectPicSend& response) {
    using namespace nn::axera;
    if (!task || !jfif_person_contract::Scene(task->GetAlgId()) || request.referenceImage.HasInput())
        return util::ErrorEnum::InvalidParam;
    const std::string label = "pedestrian";
    float threshold         = .63f;
    int min_side            = 60;
    auto apply              = [&](const MsgTaskConfig& config) {
        if (!config.facesetConfig.empty() || !config.areas.empty() || !config.shieldedAreas.empty())
            return false;
        for (const auto& p : config.params) {
            try {
                std::size_t pos = 0;
                const auto key = p.key.ToString(), value = p.value.ToString();
                if (key == "aiParam." + label + ".confidence") {
                    const auto v = std::stof(value, &pos);
                    if (pos != value.size() || !std::isfinite(v) || v < .1f || v > 1.f)
                        return false;
                    threshold = v;
                } else if (key == "filter." + label + ".side.min") {
                    const auto v = std::stoi(value, &pos);
                    if (pos != value.size() || v < 0 || v > 10000)
                        return false;
                    min_side = v;
                } else
                    return false;
            } catch (...) {
                return false;
            }
        }
        return true;
    };
    MsgTaskConfig task_config;
    {
        std::lock_guard<std::mutex> lock(task->configMutex);
        task_config = task->params;
    }
    if (!apply(task_config) || !apply(request.taskConfig))
        return util::ErrorEnum::InvalidParam;
    const int sources = int(!input.imageData.empty() || !input.uploadId.empty()) +
                        int(!input.imageBase64.empty()) + int(!input.imageUrl.empty());
    if (sources != 1 || (!input.uploadId.empty() && input.imageData.empty()))
        return util::ErrorEnum::InvalidParam;
    std::vector<std::uint8_t> bytes = std::move(input.imageData);
    if (!input.imageBase64.empty())
        bytes = util::DecBase64Vec(input.imageBase64);
    if (!input.imageUrl.empty() &&
        !service::ServiceRegistry::Instance().Get<service::IFileService>().DownloadFile(input.imageUrl,
                                                                                        bytes))
        return util::ErrorEnum::ImageDownloadFailed;
    if (bytes.empty() || bytes.size() > media::kVideoFrameMaxSize)
        return util::ErrorEnum::ImageContentSizeInvalid;
    if (!face_landmark_candidate::AdmitJpeg601Full(bytes.data(), bytes.size()))
        return util::ErrorEnum::ImageDecodeFailed;

    const auto& directory = cosmo::nn::axera::route_identity::Lookup("person_jfif.model_directory");
    if (directory.empty())
        return util::ErrorEnum::NotInit;
    const auto path = path::GetAppBaseDir() + "/resource/models/" + directory + "/model.axmodel";
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file || file.tellg() <= 0 || file.tellg() > 1024 * 1024 * 1024)
        return util::ErrorEnum::NotInit;
    std::vector<std::uint8_t> model(static_cast<std::size_t>(file.tellg()));
    file.seekg(0);
    if (!file.read(reinterpret_cast<char*>(model.data()), model.size()))
        return util::ErrorEnum::NotInit;
    person_binding::Contract c;
    c.model_sha256                 = jfif_person_contract::ModelSha256();
    c.authority_revision           = "jfif-person-native-v1";
    c.scene_id                     = task->GetAlgId();
    c.y_name                       = "prep_y_plane";
    c.uv_name                      = "prep_uv_plane";
    c.output_name                  = "detections_v1";
    c.result_capacity              = 64;
    c.required_capacity            = 1;
    c.graph_confidence             = .1f;
    c.graph_iou                    = .35f;
    c.semantic_contract            = jfif_person_contract::kSemantic;
    c.output_layout                = person_binding::Layout::Other;
    c.model_stride_rule            = person_binding::ModelStrideRule::AllowUnconstrainedWithExplicitRuntimeIo;
    c.expected_sdk_codes           = {{{1, 2, 0, 9, true}, {1, 2, 0, 9, true}, {3, 0, 0, 0, true}}};
    c.expected_quantization_values = {{0, 0, 0}};
    c.expected_max_batch_size      = 1;
    c.expected_dynamic_batch_size  = false;
    person_binding::Binding binding;
    face_binding::SdkLoader loader;
    auto capability = binding.Bind(model.data(), model.size(), c, loader);
    if (!capability)
        return util::ErrorEnum::NotInit;
    auto inferred = ExtractJfifPerson(bytes.data(), bytes.size(), capability, threshold);
    if (inferred.error != RawYuvError::None || inferred.person.error != PersonResultError::None)
        return util::ErrorEnum::ActionStop;
    auto& out         = response.resData;
    out.schemaVersion = request.resultMode == "legacy" ? 1 : 2;
    out.requestId     = request.requestId;
    out.algorithmCode = task->GetAlgId();
    out.status        = "completed";
    out.targetList.clear();
    out.outputs.clear();
    std::string detector_node =
        task->actions.empty() ? "native_detector" : task->actions.front().action.flowActionId;
    std::string output_node =
        task->actions.empty() ? "native_output" : task->actions.back().action.flowActionId;
    MsgPTaskDetectPicSend::Output result;
    result.nodeId = output_node;
    result.name   = task->GetAlgName();
    for (const auto& object : inferred.person.objects) {
        if (object.infos.size() != 1 || object.x2 - object.x1 < min_side || object.y2 - object.y1 < min_side)
            continue;
        MsgPTaskTarget t;
        t.targetId     = detector_node + ":" + std::to_string(out.targetList.size());
        t.sourceNodeId = detector_node;
        t.atomicCode   = "1001003";
        t.decision     = "matched";
        t.box.x        = object.x1;
        t.box.y        = object.y1;
        t.box.width    = object.x2 - object.x1;
        t.box.height   = object.y2 - object.y1;
        t.confidence.push_back({object.infos[0].class_name, object.infos[0].confidence, true});
        result.targetIds.push_back(t.targetId);
        out.targetList.push_back(std::move(t));
    }
    result.matchedCount = out.targetList.size();
    result.decision     = result.matchedCount ? "matched" : "not_matched";
    out.outputs.push_back(std::move(result));
    if (request.resultMode == "legacy") {
        MsgPTaskArea area;
        area.areaId     = "-1";
        area.areaName   = "default";
        area.targetList = out.targetList;
        area.bDetected  = !out.targetList.empty();
        out.areaList.push_back(std::move(area));
    }
    // JPEG bytes remain compressed; visual boxes are drawn by the web client.
    if (request.needRetImg)
        out.fullPicture = "data:image/jpeg;base64," + util::EncBase64Ex(bytes.data(), bytes.size());
    return util::ErrorEnum::Success;
}
}  // namespace cosmo::native_jfif_person_picture
#endif
