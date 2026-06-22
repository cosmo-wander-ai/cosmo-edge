#pragma once

#include <map>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>

// Forward declarations — full definitions in flow/task/PTaskBase.h (included in .cc)
namespace cosmo {
class PTaskBase;
class PTaskElement;
using PTaskElementPtr = std::shared_ptr<PTaskElement>;
}  // namespace cosmo

#include "service/media/IPicTaskService.h"

namespace cosmo::service {

class PicTaskServiceImpl : public IPicTaskService {
public:
    PicTaskServiceImpl();
    ~PicTaskServiceImpl() override;

    cosmo::util::ErrorEnum TaskCreate(const std::string& taskId, cosmo::ActionAlgPtr actionAlg) override;
    cosmo::util::ErrorEnum TaskDelete(const std::string& taskId) override;
    bool TaskStart(const std::string& taskId) override;

    void TaskDeleteAll() override;
    void UpdateCheckSum(const std::string& nodeAlgorithmCheckSum) override;
    std::string GetCheckSum() override;

    cosmo::util::ErrorEnum DetectPic(const std::string& taskId, cosmo::MsgPTaskDetectPicRecv& data,
                                     cosmo::MsgPTaskDetectPicSend& retData) override;

    cosmo::MsgPTaskCreateSend ProcessPTaskCreate(cosmo::MsgPTaskCreateRecv& data,
                                                 std::error_condition& errc) override;
    cosmo::MsgPTaskCancleSend ProcessPTaskCancel(cosmo::MsgPTaskCancleRecv& data,
                                                 std::error_condition& errc) override;
    cosmo::MsgDetectSend ProcessDetectGroup(cosmo::MsgDetectRecv& data, std::error_condition& errc) override;

    std::vector<std::string> QueryTasks(bool started = false) override;
    std::vector<std::string> QueryRealTasks(bool started = false) override;

    bool SetTaskParam(const std::string& taskId, cosmo::MsgTaskConfig& param) override;
    bool GetTaskParam(const std::string& taskId, cosmo::MsgTaskConfig& param) override;

    std::vector<cosmo::PTaskStatus> GetTaskStatus(unsigned int durationSec = 30) override;

    bool TasksHaveChange(const std::string& nodeAlgorithmCheckSum) override;
    size_t TaskCount() override;

private:
    std::string GetRealTask(const std::string& taskId);

    std::shared_mutex mtx_;
    std::unique_ptr<cosmo::PTaskBase> task_base_;
    std::string node_algorithm_check_sum_;
    std::map<std::string, cosmo::PTaskElementPtr> tasks_;
};

}  // namespace cosmo::service
