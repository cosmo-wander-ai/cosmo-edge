#if defined(COSMO_NN_USE_AXERA_BACKEND) && defined(COSMO_MEDIA_USE_AXERA_BACKEND) &&                         \
    defined(COSMO_AXERA_HELMET_ROUTE_CANDIDATE) && defined(COSMO_AXERA_SCENARIOBENCH_CANDIDATE)
#include <atomic>
#include <stdexcept>

#include "flow/classify/AiClassifier.h"
#include "flow/classify/BenchHelmetStage.h"
#include "flow/classify/NativeHelmetTicket.h"
#include "nn/device/axera/benchmark/BenchCentralHelmet.h"
#include "nn/device/axera/benchmark/BenchHelmetExecutor.h"
#include "util/Log.h"
#include "util/UuidUtil.h"

namespace cosmo {
bool AiClassifier::ConfigureBenchmarkHelmet(std::shared_ptr<const std::vector<std::uint8_t>> bytes,
                                            const std::string& route_task_id) {
    if (running || batch_count_ != 1 || alg_code_ != "7982161" || route_task_id.empty() || !bytes ||
        bytes->empty())
        return false;
    if (native_helmet_enabled_)
        return native_bench_helmet_enabled_ && native_bench_helmet_task_ == route_task_id &&
               native_helmet_bytes_ && *native_helmet_bytes_ == *bytes;
    native_helmet_bytes_         = std::move(bytes);
    native_bench_helmet_task_    = route_task_id;
    native_bench_helmet_enabled_ = true;
    native_helmet_enabled_       = true;
    return true;
}
bool AiClassifier::HandBenchmarkHelmet(AlgDataPtr data) {
    using namespace helmet_candidate;
    if (!data || !native_bench_helmet_enabled_ || !native_helmet_model_ || !data->native_helmet_ticket ||
        data->dataType != AlgDataType::TaskDataTrack || !data->native_helmet_ticket->ReadyForClassifier())
        return false;
    const auto verified_model =
        std::dynamic_pointer_cast<bench_ax::VerifiedHelmetModel>(native_helmet_model_);
    if (!verified_model)
        return false;
    const bool central = verified_model->IsCentral();
    auto input         = data->GetTaskResult(AlgDataType::TaskDataTrack);
    if (!input)
        return false;
    auto ticket = std::move(data->native_helmet_ticket);
    auto frame  = ticket->Claim();
    if (!frame || !frame->frame_witness || !frame->frame_witness->Matches(*frame) || input->frameIndex < 0 ||
        frame->frame_index != static_cast<std::uint64_t>(input->frameIndex) ||
        frame->source_timestamp != input->timestamp)
        return false;
    auto task  = ticket->TakeTaskPermit();
    auto model = ticket->TakeModelPermit();
    if (!task || !model)
        return false;
    std::shared_ptr<NativeHelmetTicket> lease(std::move(ticket));
    const FrameKey key{data->channelId, frame->stream_generation, frame->decoder_token, frame->frame_index,
                       frame->source_timestamp};
    std::vector<BoundTarget> targets;
    targets.reserve(input->targets.size());
    for (const auto& t : input->targets)
        targets.push_back({t, key});
    const auto selection = bench_ax::SelectHelmetTargets(targets, key, input->bHaveArea);
    if (!selection)
        return false;
    std::vector<std::array<float, 2>> scores;
    if (!selection->rois.empty()) {
        struct IoMarker final : IoOwner {};
        struct MapMarker final : MappingOwner {};
        auto pending         = std::make_unique<PendingResources>();
        pending->frame       = frame;
        pending->frame_lease = lease;
        pending->model       = native_helmet_model_;
        if (!central) {
            pending->handle       = native_helmet_model_->HandleForOwnerThread();
            pending->owner_thread = std::this_thread::get_id();
        }
        pending->task_permit  = task;
        pending->model_permit = model;
        pending->io_owner =
            central ? std::static_pointer_cast<IoOwner>(std::make_shared<bench_ax::HelmetIoOwner>())
                    : std::static_pointer_cast<IoOwner>(std::make_shared<IoMarker>());
        pending->mapping_owner = std::make_shared<MapMarker>();
        pending->sdk           = std::make_shared<HelmetSystemSdk>(
            [counters = execution_counters_](AX_ENGINE_HANDLE h, AX_ENGINE_IO_T* io) {
                AX_S32 rc = -1;
                counters->RunForward([&]() {
                    rc = AX_ENGINE_RunSync(h, io);
                    return rc == 0;
                });
                return rc;
            });
        auto result = central ? bench_ax::RunCentralHelmet(std::move(pending), selection->rois)
                              : bench_ax::RunHelmet(std::move(pending), selection->rois);
        if (central && result.result == GuardResult::Complete) {
            static std::atomic<std::uint64_t> groups{0};
            const auto count = ++groups;
            if (count == 1 || count % 256 == 0)
                LOG_INFO("AX_BENCH_CENTRAL_HELMET groups={}", count);
        }
        if (result.result == GuardResult::CompletionUnknown || result.result == GuardResult::Poisoned)
            throw std::runtime_error("benchmark helmet device completion unknown");
        if (result.result != GuardResult::Complete || result.scores.size() != selection->rois.size() ||
            !result.task_permit || !result.model_permit)
            return false;
        task   = std::move(result.task_permit);
        model  = std::move(result.model_permit);
        scores = std::move(result.scores);
    } else if (!task->permit.TryBegin() || !model->permit.TryBegin())
        return false;
    if (!task->permit.StillCurrent() || !model->permit.StillCurrent())
        return false;
    auto staged = bench_ax::MaterializeHelmet(targets, key, input->bHaveArea, *selection, scores,
                                              []() { return util::GenerateUUID(); });
    if (!staged)
        return false;
    TaskOutput sink;
    if (!CommitPrepared(task->permit, model->permit, sink, staged))
        return false;
    auto output = AlgDataCopy(data);
    if (!output)
        return false;
    output->dataType      = AlgDataType::TaskDataClassify;
    output->bHaveClassify = true;
    auto classify         = std::make_shared<DataDetTrackClassify>(*input);
    classify->targets.swap(sink);
    output->SetTaskResult(AlgDataType::TaskDataClassify, std::move(classify));
    RecordHistory(output);
    distributor->DistributorData(output);
    return true;
}
}  // namespace cosmo
#endif
