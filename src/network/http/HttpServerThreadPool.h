#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "network/msg/MsgEnvelope.h"
#include "util/IRequestDispatcher.h"

namespace cosmo::network::http {
class MsgHanderThread;
class HttpServer;

class HttpServerThreadPool {
public:
    HttpServerThreadPool();
    virtual ~HttpServerThreadPool();

    using DispatcherFactory = std::function<std::unique_ptr<cosmo::IRequestDispatcher>()>;

    // Initialize thread pool
    bool Initialize(int thread_num, HttpServer* server, DispatcherFactory factory);

    // Shutdown thread pool
    void Uninitialize();

    // Dispatch message to appropriate thread
    int PutMsg(cosmo::MsgEnvelope&& msg);

private:
    int MsgInPrioIndex(cosmo::MsgEnvelope& msg);
    std::vector<std::unique_ptr<MsgHanderThread>> msg_handler_threads_;
    int thread_num_     = 4;
    int cur_thread_idx_ = -1;
    std::vector<std::string> prio0_interface_;
    std::vector<std::string> prio1_interface_;
};

}  // namespace cosmo::network::http
