// Sam2SegmenterParam.cc — Parameter management for Sam2Segmenter.
// Split from Sam2Segmenter.cc to reduce file size (DEBT-007).

// clang-format off
#include "flow/sam2/Sam2Segmenter.h"
// clang-format on

#include <unistd.h>

#include <algorithm>
#include <thread>

#include "flow/common/AlgDataRecord.h"
#include "media/VideoFrame.h"
#include "service/detail/ServiceRegistry.h"
#include "service/model/IModelPathMapping.h"
#include "service/model/IModelService.h"
#include "util/Log.h"
#include "util/TimingConstants.h"
#include "util/UuidUtil.h"
#include "util/dto/ActionCodes.h"

static constexpr const char* kTag      = "SAM2-SEGMENTER ";
static constexpr int kMaxBatchWaitIter = 100;

namespace cosmo {

bool Sam2Segmenter::RemoveTask(const std::string& channel_id, const std::string& task) {
    std::lock_guard<std::shared_mutex> lock(mtx);

    for (auto ch_it = channel_list_.begin(); ch_it != channel_list_.end(); ++ch_it) {
        if (ch_it->channel == channel_id) {
            for (auto task_it = ch_it->task_list.begin(); task_it != ch_it->task_list.end(); ++task_it) {
                if (*task_it == task) {
                    ch_it->task_list.erase(task_it);
                    LOG_INFO("{}[{} {}] Remove Task Channel:{} Task:{}", kTag, alg_code_, uuid, channel_id,
                             task);

                    if (ch_it->task_list.empty()) {
                        channel_list_.erase(ch_it);
                        LOG_INFO("{}[{} {}] Remove Channel:{}", kTag, alg_code_, uuid, channel_id);
                    }
                    return true;
                }
            }
        }
    }

    LOG_WARN("{}[{} {}] Remove Task Channel:{} Task:{} Not Found", kTag, alg_code_, uuid, channel_id, task);
    return false;
}

bool Sam2Segmenter::ChannelExist(const std::string& channel_id) {
    std::lock_guard<std::shared_mutex> lock(mtx);
    return std::any_of(channel_list_.begin(), channel_list_.end(),
                       [&channel_id](const auto& ch) { return ch.channel == channel_id; });
}

bool Sam2Segmenter::TaskExist(const std::string& channel_id, const std::string& task) {
    std::lock_guard<std::shared_mutex> lock(mtx);
    return std::any_of(channel_list_.begin(), channel_list_.end(), [&channel_id, &task](const auto& ch) {
        if (ch.channel != channel_id)
            return false;
        return std::any_of(ch.task_list.begin(), ch.task_list.end(),
                           [&task](const auto& t) { return t == task; });
    });
}

bool Sam2Segmenter::TaskIsFull() {
    std::lock_guard<std::shared_mutex> lock(mtx);
    return channel_list_.size() >= max_reuse_count_;
}

bool Sam2Segmenter::TaskIsEmpty() {
    std::lock_guard<std::shared_mutex> lock(mtx);
    return channel_list_.empty();
}

size_t Sam2Segmenter::ChannelCount() {
    std::lock_guard<std::shared_mutex> lock(mtx);
    return channel_list_.size();
}

size_t Sam2Segmenter::TaskCount() {
    std::lock_guard<std::shared_mutex> lock(mtx);
    size_t count = 0;
    for (auto& ch : channel_list_) {
        count += ch.task_list.size();
    }
    return count;
}

Sam2SegmenterParamEl Sam2Segmenter::FoundLocalParamByTask(const AlgTaskUnit& task) const {
    Sam2SegmenterParamEl param;
    param.task_id = task.task_id;
    return param;
}

Sam2SegmenterParamEl Sam2Segmenter::GetTaskParams(const std::string& tgt_task_id) {
    std::lock_guard<std::shared_mutex> lock(mtx);
    auto it = std::find_if(params_.param.begin(), params_.param.end(),
                           [&tgt_task_id](const auto& param) { return param.task_id == tgt_task_id; });
    if (it != params_.param.end()) {
        return *it;
    }
    if (params_.param.size() == 1)
        return params_.param[0];
    Sam2SegmenterParamEl empty_param;
    return empty_param;
}

void Sam2Segmenter::HandFrameBatch(const std::vector<AlgDataPtr>& alg_datas) {
    if (!segmenter_) {
        action_status = util::ErrorEnum::AI_INST_NOTCREATED;
        if (!Sam2SdkInit()) {
            LOG_WARN("{}[{} {}] Segmenter not initialized", kTag, alg_code_, uuid);
            return;
        }
    }

    // Collect valid image frames and prompts
    // SAM2 SDK requires images.size() == prompts.size() (1:1 mapping)
    // A frame may have N detection targets, need to generate a set of image+prompt for each target
    std::vector<VideoFramePtr> images;
    std::vector<Sam2PromptInput> prompts;

    // Record source information for each prompt, used for writing back mask
    struct PromptSource {
        AlgDataPtr data;
        size_t target_index;        // Index in detRet->targets, SIZE_MAX means no upstream target
        bool has_previous_targets;  // Whether there are upstream detection results
    };
    std::vector<PromptSource> prompt_sources;

    for (auto& data : alg_datas) {
        if (!data || !data->chanDataDec.frame || !data->chanDataDec.frame->Active())
            continue;

        std::string cur_task_id          = data->taskId;
        Sam2SegmenterParamEl task_param  = GetTaskParams(cur_task_id);
        Sam2InputType current_input_type = task_param.input_type;

        auto frame  = data->chanDataDec.frame;
        float img_w = static_cast<float>(frame->GetWidth());
        float img_h = static_cast<float>(frame->GetHeight());

        bool has_previous_targets =
            data->chanDataDetect.detRet && !data->chanDataDetect.detRet->targets.empty();

        if (has_previous_targets) {
            // Generate a set of image+prompt for each detection target
            auto& targets = data->chanDataDetect.detRet->targets;
            for (size_t i = 0; i < targets.size(); ++i) {
                const auto& tgt = targets[i];
                Sam2PromptInput prompt;
                prompt.input_type = current_input_type;

                if (current_input_type == Sam2InputType::BOX) {
                    // Use detection box coordinates as SAM2 box prompt
                    prompt.coords = {static_cast<float>(tgt.box.x), static_cast<float>(tgt.box.y),
                                     static_cast<float>(tgt.box.x + tgt.box.width),
                                     static_cast<float>(tgt.box.y + tgt.box.height)};
                } else {
                    // Point mode: use the center point of the detection box
                    prompt.coords = {
                        static_cast<float>(tgt.box.x) + static_cast<float>(tgt.box.width) / 2.0f,
                        static_cast<float>(tgt.box.y) + static_cast<float>(tgt.box.height) / 2.0f};
                    prompt.labels = {1.0};  // 1 = foreground point
                }
                images.push_back(frame);
                prompts.push_back(prompt);
                prompt_sources.push_back({data, i, true});
            }
        } else {
            // No upstream detection box, use the whole image as prompt
            Sam2PromptInput prompt;
            prompt.input_type = current_input_type;
            if (current_input_type == Sam2InputType::BOX) {
                prompt.coords = {0.0f, 0.0f, img_w, img_h};
            } else {
                prompt.coords = {img_w / 2.0f, img_h / 2.0f};
                prompt.labels = {1.0};
            }
            images.push_back(frame);
            prompts.push_back(prompt);
            prompt_sources.push_back({data, SIZE_MAX, false});
        }
    }

    if (images.empty()) {
        return;
    }

    // Batch segmentation
    std::vector<AiDetectRstEl> results;
    auto ret = segmenter_->Segment(images, prompts, results);
    if (util::ErrorEnum::Success != ret) {
        LOG_WARN("{}[{} {}] Segment Failed. Ret:{}", kTag, alg_code_, uuid, ret);
        return;
    }

    // Write segmentation results (mask) back to corresponding algData targets
    for (size_t i = 0; i < results.size() && i < prompt_sources.size(); ++i) {
        auto& src  = prompt_sources[i];
        auto& data = src.data;

        if (!data->chanDataDetect.detRet) {
            data->chanDataDetect.detRet = std::make_shared<DataDetTrackClassify>();
        }

        if (src.has_previous_targets && src.target_index < data->chanDataDetect.detRet->targets.size()) {
            // Has upstream detection result: write mask to corresponding target
            data->chanDataDetect.detRet->targets[src.target_index].mask = results[i].mask;
        } else if (!src.has_previous_targets) {
            // No upstream detection result: add segmentation result as new target
            data->chanDataDetect.detRet->targets.push_back(results[i]);
        }

        LOG_DEBUG("{}[{} {}] Segmented frame {}, targetIdx:{}, mask size: {}", kTag, alg_code_, uuid, i,
                  src.target_index, results[i].mask.data.size());
    }
}

void Sam2Segmenter::run() {
    LOG_INFO("{}[{} {}] Thread Start", kTag, alg_code_, uuid);
    int index = 0;
    while (running) {
        if ((data_queue->RestSize() >= batch_count_) ||
            ((index >= kMaxBatchWaitIter) && (data_queue->RestSize() > 0))) {
            size_t detnum         = data_queue->RestSize();
            auto input_fps_result = input_fps_calc.FpsWithFrame();
            handle_frame_cnt      = input_fps_result.first;
            in_fps                = input_fps_result.second;
            std::vector<AlgDataPtr> alg_datas;
            for (size_t i = 0; i < detnum && i < batch_count_; i++) {
                auto data = data_queue->Pop();
                if (data) {
                    alg_datas.push_back(data);
                }
            }
            if (!alg_datas.empty()) {
                duration_stat.BeginSample();
                HandFrameBatch(alg_datas);
                duration_stat.EndSample();
            }
            index = 0;
        } else {
            std::this_thread::sleep_for(timing::kFastPollInterval);
            index += 1;
        }
    }
    // Release model and VRAM on thread exit (will be reloaded by HandFrameBatch->Sam2SdkInit on next start)
    if (segmenter_) {
        LOG_INFO("{}[{} {}] Thread exiting, releasing SAM2 model to free VRAM", kTag, alg_code_, uuid);
        segmenter_.reset();
        is_detector_inst_init_ = false;
    }
    LOG_INFO("{}[{} {}] Thread Stop", kTag, alg_code_, uuid);
}

}  // namespace cosmo
