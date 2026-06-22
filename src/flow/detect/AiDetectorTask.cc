// AiDetectorTask.cc — Channel/Task lifecycle management for AiDetector.
// Split from AiDetector.cc to reduce file size (DEBT-007).

#include "flow/detect/AiDetector.h"
#include "util/Log.h"

static constexpr const char* kTag = "AI-DETECTER ";
namespace cosmo {

bool AiDetector::ChannelExist(const std::string& channel_id) {
    auto iterFind =
        std::find_if(m_channelList.begin(), m_channelList.end(),
                     [&](const AiDetectorChannel& channelNode) { return channelNode.channel == channel_id; });

    if (iterFind != m_channelList.end()) {
        return true;
    }

    return false;
}

bool AiDetector::TaskExist(const std::string& channel_id, const std::string& task) {
    for (auto& channelNode : m_channelList) {
        if (channel_id == channelNode.channel) {
            return std::any_of(channelNode.tasks.begin(), channelNode.tasks.end(),
                               [&task](const auto& t) { return task == t; });
        }
    }
    return false;
}

bool AiDetector::TaskIsFull() {
    if (m_channelList.size() >= m_maxReuseCount) {
        return true;
    }

    return false;
}

bool AiDetector::TaskIsEmpty() {
    return m_channelList.empty();
}

size_t AiDetector::ChannelCount() {
    return m_channelList.size();
}

size_t AiDetector::TaskCount() {
    size_t taskCount = 0;
    for (auto& channelNode : m_channelList) {
        taskCount += channelNode.tasks.size();
    }
    return taskCount;
}

bool AiDetector::AddTask(const std::string& channel_id, const std::string& task) {
    for (auto& channelNode : m_channelList) {
        if (channel_id == channelNode.channel) {
            if (std::any_of(channelNode.tasks.begin(), channelNode.tasks.end(),
                            [&task](const auto& t) { return task == t; })) {
                LOG_WARN("{}[{} {}] [{} {}] Add Task. But Task Is Exist", kTag, m_name, uuid, channel_id,
                         task);
                return true;
            }
            channelNode.tasks.push_back(task);
            AddOverviewTask(task);
            {
                std::lock_guard<std::shared_mutex> lock(mtx);
                m_taskHistorys[task];
            }
            LOG_INFO("{}[{} {}] Add Task[{}] To Channel:{}. ", kTag, m_name, uuid, task, channel_id);
            return true;
        }
    }

    AiDetectorChannel newChannel;
    newChannel.channel = channel_id;
    newChannel.tasks.push_back(task);
    m_channelList.push_back(newChannel);

    AddOverviewTask(task);
    {
        std::lock_guard<std::shared_mutex> lock(mtx);
        m_taskHistorys[task];
    }

    LOG_INFO("{}[{} {}] Add New Task[{}] To Channel:{}.", kTag, m_name, uuid, task, channel_id);
    return true;
}

bool AiDetector::RemoveTask(const std::string& channel_id, const std::string& task) {
    for (auto chIt = m_channelList.begin(); chIt != m_channelList.end(); ++chIt) {
        if (chIt->channel == channel_id) {
            for (auto taskIt = chIt->tasks.begin(); taskIt != chIt->tasks.end(); ++taskIt) {
                if (*taskIt == task) {
                    chIt->tasks.erase(taskIt);
                    LOG_INFO("{}[{} {}] Remove Task Channel:{} Task:{} left:{}", kTag, m_name, uuid,
                             channel_id, task, chIt->tasks.size());

                    if (chIt->tasks.empty()) {
                        m_channelList.erase(chIt);
                        LOG_INFO("{}[{} {}] Remove Channel:{}", kTag, m_name, uuid, channel_id);
                    }
                    {
                        std::lock_guard<std::shared_mutex> lock(mtx);
                        m_taskHistorys.erase(task);
                        m_taskAreas.erase(task);
                        m_overviewRecInsts.erase(task);
                    }
                    return true;
                }
            }
            LOG_WARN("{}[{} {}] [{} {}] Remove Task. But Task Is Not Exist", kTag, m_name, uuid, channel_id,
                     task);
            return false;
        }
    }

    LOG_WARN("{}[{} {}] [{} {}] Remove Task. But Channel Is Not Exist", kTag, m_name, uuid, channel_id, task);
    return false;
}

void AiDetector::SetProcQueSize() {
    size_t queMaxSize  = 64;
    size_t queSizeBase = 10;
    auto queInFps      = data_queue->GetInFps();
    if (queInFps <= 5.5f) {
        queMaxSize = queSizeBase * 2 * media::kQueueSizeCoefficient;
    } else if (queInFps <= 10.5f) {
        queMaxSize = queSizeBase * 5 * media::kQueueSizeCoefficient;
    } else {
        queMaxSize = queSizeBase * 10 * media::kQueueSizeCoefficient;
    }

    data_queue->SetMaxSize(queMaxSize);
}

}  // namespace cosmo
