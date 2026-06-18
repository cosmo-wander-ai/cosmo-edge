// CameraTaskMngMonitor.cc — Monitor and state update for CameraTaskMng.
// Split from CameraTaskMng.cc to reduce file size (DEBT-007).

#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

#include "flow/channel/AlgChannel.h"
#include "flow/task/CameraTaskMng.h"
#include "flow/task/CameraTaskUnit.h"
#include "service/detail/ServiceRegistry.h"
#include "service/task/IScheduleService.h"
#include "service/task/ITaskChannel.h"
#include "service/task/ITaskLifecycle.h"
#include "service/task/ITaskQuery.h"
#include "util/Log.h"
#include "util/dto/ChannelStatusDto.h"

static constexpr const char* kTag = "CAMERA-TASK ";
namespace cosmo {

void CameraTaskMng::Monitor(bool isAuthed) {
    std::vector<CameraTaskPtr> snapshot;
    {
        std::shared_lock<std::shared_mutex> lock(mtx_);
        snapshot.assign(tasks_.begin(), tasks_.end());
    }
    for (auto& task : snapshot) {
        if (!task) {
            LOG_WARN("[{}] Monitor skipped null task", channel_id_);
            continue;
        }
        if (!task->task_ || !task->task_->IsReady()) {
            LOG_WARN("[{}/{}] Monitor skipped task because task unit is not ready", channel_id_,
                     task->task_id_);
            task->status_ = CameraTaskStatus::kAbnormal;
            continue;
        }
        task->task_->TaskEnableParam();
        bool taskRunningStatus =
            service::ServiceRegistry::Instance().Get<cosmo::service::ITaskLifecycle>().TaskIsStart(
                task->task_id_);
        // Task is currently running
        if (taskRunningStatus) {
            // Stop if task is disabled, outside schedule window, or unauthorized
            if ((!task->is_enabled_) ||
                (!service::ServiceRegistry::Instance().Get<service::IScheduleService>().InRunTime(
                     task->schedule_id_) ||
                 (!isAuthed))) {
                LOG_INFO("[{}/{}] Stop TaskEnable:{} AUTH:{} Schedule:{}/{}", channel_id_, task->task_id_,
                         task->is_enabled_, isAuthed, task->schedule_id_, task->schedule_name_);
                service::ServiceRegistry::Instance().Get<cosmo::service::ITaskLifecycle>().TaskStop(
                    task->task_id_);
                if (task->is_enabled_)                         // Switch is still on
                    task->status_ = CameraTaskStatus::kPause;  // Paused
                else
                    task->status_ = CameraTaskStatus::kStop;  // Stopped
            }
            // Auto-stop task and release GPU memory when offline/VOD video finishes.
            // Note: AlgDemuxClosed is a generic closed state (including initial state during Demux reopen),
            //       it cannot be used to determine "content finished". Only AlgDemuxReadEnd means truly done.
            else {
                MsgCameraAttr attr;
                if (service::ServiceRegistry::Instance().Get<cosmo::service::ITaskChannel>().GetChannelAttr(
                        channel_id_, attr)) {
                    bool isReadEnd = (attr.dataStatus ==
                                      static_cast<int>(service::camera::AlgDemuxStatus::AlgDemuxReadEnd));
                    // Channel has finished reading and no active data remains (queue fully consumed)
                    if (isReadEnd && !service::ServiceRegistry::Instance()
                                          .Get<cosmo::service::ITaskChannel>()
                                          .TaskDataActive(channel_id_)) {
                        LOG_INFO("[{}/{}] Offline video completed, auto-stopping task to release resources",
                                 channel_id_, task->task_id_);
                        service::ServiceRegistry::Instance().Get<cosmo::service::ITaskLifecycle>().TaskStop(
                            task->task_id_);
                        task->is_enabled_ = false;
                        task->status_     = CameraTaskStatus::kStop;
                        {
                            std::lock_guard<std::shared_mutex> lock(mtx_);
                            SaveTaskList();
                        }
                    }
                }
            }
        }
        // Task is not running
        else {
            if ((task->is_enabled_) &&
                (service::ServiceRegistry::Instance().Get<service::IScheduleService>().InRunTime(
                     task->schedule_id_) &&
                 (isAuthed))) {
                LOG_INFO("[{}/{}] Start", channel_id_, task->task_id_);

                MakeTaskOverviewParam(task);

                if (service::ServiceRegistry::Instance().Get<cosmo::service::ITaskLifecycle>().TaskStart(
                        channel_id_, task->task_id_)) {
                    task->status_ = CameraTaskStatus::kInService;
                } else {
                    task->status_ = CameraTaskStatus::kAbnormal;
                }
            }
        }
    }
    UpdateChannelTaskState();
    ProbeOnlineStatus();
}

void CameraTaskMng::UpdateChannelTaskState() {
    bool anyTaskRunning = false;
    std::vector<CameraTaskPtr> snapshot;
    {
        std::shared_lock<std::shared_mutex> lock(mtx_);
        snapshot.assign(tasks_.begin(), tasks_.end());
    }
    for (auto& task : snapshot) {
        if (service::ServiceRegistry::Instance().Get<cosmo::service::ITaskLifecycle>().TaskIsStart(
                task->task_id_)) {
            anyTaskRunning = true;
            break;
        }
    }

    bool channelRunning =
        service::ServiceRegistry::Instance().Get<cosmo::service::ITaskLifecycle>().TaskIsStart(channel_task_);
    if (anyTaskRunning && !channelRunning) {
        LOG_INFO("[{}] Auto-starting ChannelTask because active analysis tasks exist", channel_id_);
        service::ServiceRegistry::Instance().Get<cosmo::service::ITaskLifecycle>().TaskStart(channel_id_,
                                                                                             channel_task_);
    } else if (!anyTaskRunning && channelRunning && !is_capturing_image_.load()) {
        // Cache video attributes before stopping the channel
        MsgCameraAttr attr;
        if (service::ServiceRegistry::Instance().Get<cosmo::service::ITaskChannel>().GetChannelAttr(
                channel_id_, attr)) {
            if (attr.width > 0 && attr.height > 0) {
                std::lock_guard<std::mutex> lock(attr_mtx_);
                cached_attr_ = attr;
            }
        }
        LOG_INFO("[{}] Auto-stopping ChannelTask because no active analysis tasks exist", channel_id_);
        service::ServiceRegistry::Instance().Get<cosmo::service::ITaskLifecycle>().TaskStop(channel_task_);
        auto channel =
            service::ServiceRegistry::Instance().Get<cosmo::service::ITaskChannel>().GetChannelInst(
                channel_id_);
        if (channel)
            channel->Quit();
        probed_status_.store(ChannelStatus::ChannelStatusOffline);
    }
}

static bool CheckUrlConnectivity(const std::string& urlStr) {
    if (urlStr.empty())
        return false;

    if (urlStr.find("usb://") == 0) {
        auto startPos        = 6;
        auto questionMarkPos = urlStr.find("?");
        std::string indexStr;
        if (questionMarkPos != std::string::npos) {
            indexStr = urlStr.substr(startPos, questionMarkPos - startPos);
        } else {
            indexStr = urlStr.substr(startPos);
        }
        std::string devPath = "/dev/video" + indexStr;
        if (::access(devPath.c_str(), F_OK) == 0) {
            return true;
        }
        return false;
    }

    // For local files or dev nodes
    if (urlStr.find("rtsp://") == std::string::npos && urlStr.find("http://") == std::string::npos &&
        urlStr.find("https://") == std::string::npos) {
        if (::access(urlStr.c_str(), F_OK) == 0) {
            return true;
        }
        return false;
    }

    // Simplistic port check for RTSP/HTTP (fallback to format parsing)
    std::string ip;
    int port = -1;

    auto protoPos = urlStr.find("://");
    if (protoPos != std::string::npos) {
        std::string withoutProto = urlStr.substr(protoPos + 3);
        // Strip out auth part user:pass@
        auto atPos = withoutProto.find("@");
        if (atPos != std::string::npos) {
            withoutProto = withoutProto.substr(atPos + 1);
        }
        // Extract IP and Port
        auto slashPos        = withoutProto.find("/");
        std::string hostPort = withoutProto.substr(0, slashPos);

        auto colonPos = hostPort.find(":");
        if (colonPos != std::string::npos) {
            ip = hostPort.substr(0, colonPos);
            try {
                port = std::stoi(hostPort.substr(colonPos + 1));
            } catch (const std::exception& e) {
                LOG_WARN("Failed to parse port from '{}': {}", hostPort, e.what());
            }
        } else {
            ip = hostPort;
            if (urlStr.find("rtsp://") == 0)
                port = 554;
            else if (urlStr.find("https://") == 0)
                port = 443;
            else if (urlStr.find("http://") == 0)
                port = 80;
        }
    }

    if (ip.empty() || port <= 0)
        return false;

    // RAII wrapper: ensures socket fd is closed on all exit paths
    struct ScopedSocket {
        int fd;
        explicit ScopedSocket(int f) : fd(f) {}
        ~ScopedSocket() {
            if (fd >= 0)
                ::close(fd);
        }
        ScopedSocket(const ScopedSocket&)            = delete;
        ScopedSocket& operator=(const ScopedSocket&) = delete;
    };

    ScopedSocket sock(socket(AF_INET, SOCK_STREAM, 0));
    if (sock.fd < 0)
        return false;

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port   = htons(port);

    if (inet_pton(AF_INET, ip.c_str(), &serv_addr.sin_addr) <= 0) {
        return false;
    }

    // Set non-blocking
    int flags = fcntl(sock.fd, F_GETFL, 0);
    fcntl(sock.fd, F_SETFL, flags | O_NONBLOCK);

    int res = connect(sock.fd, reinterpret_cast<struct sockaddr*>(&serv_addr), sizeof(serv_addr));
    if (res == 0) {
        return true;
    }
    if (res < 0 && errno == EINPROGRESS) {
        struct timeval tv;
        tv.tv_sec  = 2;  // 2 sec timeout
        tv.tv_usec = 0;
        fd_set fdset;
        FD_ZERO(&fdset);
        FD_SET(sock.fd, &fdset);

        res = select(sock.fd + 1, nullptr, &fdset, nullptr, &tv);
        if (res > 0 && FD_ISSET(sock.fd, &fdset)) {
            int lon;
            socklen_t lonLen = sizeof(int);
            getsockopt(sock.fd, SOL_SOCKET, SO_ERROR, static_cast<void*>(&lon), &lonLen);
            if (lon == 0) {
                return true;
            }
        }
    }
    return false;
}

void CameraTaskMng::ProbeOnlineStatus() {
    // Only probe if the channel is actually stopped to save overhead when already active
    if (service::ServiceRegistry::Instance().Get<cosmo::service::ITaskLifecycle>().TaskIsStart(
            channel_task_)) {
        return;
    }

    bool isConnected = CheckUrlConnectivity(channel_url_);
    probed_status_.store(isConnected ? ChannelStatus::ChannelStatusOnline
                                     : ChannelStatus::ChannelStatusOffline);
}

void CameraTaskMng::ProbeOnlineStatusNow() {
    bool isConnected = CheckUrlConnectivity(channel_url_);
    probed_status_.store(isConnected ? ChannelStatus::ChannelStatusOnline
                                     : ChannelStatus::ChannelStatusOffline);
}

}  // namespace cosmo
