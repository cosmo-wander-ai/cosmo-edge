// HttpServerThreadPool — Http Server Thread Pool implementation.

#include "network/http/HttpServerThreadPool.h"

#include <limits>

#include "network/http/HttpCommon.h"
#include "network/http/HttpServerThread.h"
#include "util/Log.h"
#include "util/StringUtil.h"

namespace cosmo::network::http {

HttpServerThreadPool::HttpServerThreadPool() : thread_num_(4), cur_thread_idx_(-1) {}

HttpServerThreadPool::~HttpServerThreadPool() {
    Uninitialize();
}

bool HttpServerThreadPool::Initialize(int thread_num, HttpServer* server, DispatcherFactory factory) {
    if (thread_num <= 0) {
        return false;
    }
    thread_num_ = thread_num;
    msg_handler_threads_.resize(thread_num_);
    for (int idx = 0; idx < thread_num; ++idx) {
        char name[64] = {0};
        snprintf(name, sizeof(name), "MsgHanderThread_%d", idx);
        msg_handler_threads_[idx] = std::make_unique<MsgHanderThread>(name, server, factory());
    }

    for (int idx = 0; idx < thread_num; ++idx) {
        if (!msg_handler_threads_[idx]->start()) {
            LOG_ERRO("HttpServerThreadPool failed to start handler thread {}", idx);
            Uninitialize();
            return false;
        }
    }

    // Priority 0: restart, reset etc.
    prio0_interface_.push_back(cosmo::util::ToLower("dologin"));
    prio0_interface_.push_back(cosmo::util::ToLower("ResetSystem"));

    // Priority 1: non-blocking or debug
    prio1_interface_.push_back(cosmo::util::ToLower("ThreadDebugInfo"));
    prio1_interface_.push_back(cosmo::util::ToLower("QueryDeviceInfo"));
    return true;
}

void HttpServerThreadPool::Uninitialize() {
    if (!msg_handler_threads_.empty()) {
        for (int idx = 0; idx < thread_num_; ++idx) {
            msg_handler_threads_[idx]->Stop();
        }

        msg_handler_threads_.clear();
    }
}

int HttpServerThreadPool::MsgInPrioIndex(cosmo::MsgEnvelope& msg) {
    int nIdx    = -1;
    auto* ptask = static_cast<HttpReqTask*>(msg.GetData());
    if (!ptask) {
        return nIdx;
    }

    auto interface = cosmo::util::ToLower(ptask->interface);
    for (auto& prioInterface : prio0_interface_) {
        if (std::string::npos != interface.find(prioInterface)) {
            return 0;
        }
    }
    for (auto& prioInterface : prio1_interface_) {
        if (std::string::npos != interface.find(prioInterface)) {
            return 1;
        }
    }
    return nIdx;
}

int HttpServerThreadPool::PutMsg(cosmo::MsgEnvelope&& msg) {
    if (msg_handler_threads_.empty())
        return -1;

    size_t minMsgCount = std::numeric_limits<size_t>::max();
    int nIdx           = MsgInPrioIndex(msg);
    if (nIdx < 0) {
        for (int idx = 2; idx < thread_num_; ++idx) {
            auto msgCount = msg_handler_threads_[idx]->MsgCount();
            if (minMsgCount > msgCount) {
                minMsgCount = msgCount;
                nIdx        = idx;
            }

            if (0 == minMsgCount) {
                break;
            }
        }
    } else {
        minMsgCount = msg_handler_threads_[nIdx]->MsgCount();
    }

    LOG_INFO("PutMsg To Http Pool {}, This Pool Have {} Tasks in Queue", nIdx, minMsgCount);
    return msg_handler_threads_[nIdx]->Put(std::move(msg));
}

}  // namespace cosmo::network::http
