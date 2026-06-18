#pragma once

#include <event2/keyvalq_struct.h>

#include <atomic>
#include <thread>

#include "network/http/HttpCommon.h"
#include "network/msg/MsgThread.h"
#include "util/IRequestDispatcher.h"

namespace cosmo::network::http {
class HttpServer;

// HTTP request handler thread
class MsgHanderThread : public cosmo::MsgThread {
public:
    explicit MsgHanderThread(const std::string& name, HttpServer* server,
                             std::unique_ptr<cosmo::IRequestDispatcher> dispatcher, size_t maxCount = 0xffff);
    virtual ~MsgHanderThread();

    size_t MsgCount() {
        return ((is_thread_busy_.load(std::memory_order_relaxed) ? 1 : 0) + MsgSize());
    };

protected:
    void HandleMsg(cosmo::MsgEnvelope& msg) override;

    void ProcessHttpReqTask(HttpReqTask& task);
    void ProcessHttpOctetReqTask(HttpReqTask& task);

    std::string GetMtk(struct evkeyvalq* ev);

    std::map<std::string, std::string> ParseMultipartFields(HttpReqTask& task, const char* boundary);
    void DelTmpPath(const HttpReqTask& task);

private:
    bool ExtractMultipartBody(HttpReqTask& task, const char* contentType, const char* uri, std::string& body);
    void SendHttpAck(HttpResponseCode code, struct evhttp_request* req, std::string response,
                     std::string request_id);

    std::atomic<bool> is_thread_busy_{false};
    std::unique_ptr<cosmo::IRequestDispatcher> handler_;
    HttpServer* server_{nullptr};
};

}  // namespace cosmo::network::http
