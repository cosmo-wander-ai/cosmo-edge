#pragma once

#include <algorithm>

#include "flow/action/ActionInstMngBase.h"
#include "flow/qwen3vl/Qwen3VLWorker.h"
#include "service/ai/ILlmInferService.h"
#include "service/detail/ServiceRegistry.h"

namespace cosmo {
class Qwen3VLMng : public MultiChannelActionMng<Qwen3VLWorker> {
public:
    Qwen3VLMng() : MultiChannelActionMng("Qwen3VLMng") {}

    bool DeleteInst(InstPtr inst, const std::string& channel_id, const std::string& task) {
        bool result = MultiChannelActionMng<Qwen3VLWorker>::DeleteInst(inst, channel_id, task);
        if (result) {
            // When the last Qwen3VL instance is deleted, release the model memory
            std::shared_lock<std::shared_mutex> lock(m_mtx);
            bool all_empty = std::all_of(m_insts.begin(), m_insts.end(),
                                         [](const auto& pair) { return pair.second.empty(); });
            if (all_empty) {
                lock.unlock();
                service::ServiceRegistry::Instance().Get<service::ILlmInferService>().Reset();
            }
        }
        return result;
    }
};
}  // namespace cosmo
