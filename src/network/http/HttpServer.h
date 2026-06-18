#pragma once

#include <atomic>
#include <list>
#include <mutex>

#include "event2/buffer.h"
#include "event2/event.h"
#include "event2/http.h"
#include "network/http/HttpCommon.h"
#include "network/http/HttpServerThreadPool.h"
#include "network/msg/MsgEnvelope.h"
#include "util/IRequestDispatcher.h"
#include "util/JsonStructUtil.h"
#include "util/Log.h"

namespace cosmo::network::http {

class HttpServer {
public:
    HttpServer();
    virtual ~HttpServer();

    void SetAppInfo(const std::string& app_key, const std::string& app_secret);

    using DispatcherFactory = std::function<std::unique_ptr<cosmo::IRequestDispatcher>()>;

    // Initialize the HTTP server
    bool Initialize(const std::string& host_ip, int port, DispatcherFactory factory,
                    HttpServerCallbacks callbacks);

    // Shutdown the HTTP server
    void UnInitialize();

    // Blocking event dispatch loop, call unInitialize() to stop
    void DispatchMsg();

    // Enqueue an ack result for the event loop to send
    template <typename T>
    void AckResult(HttpResponseCode hcode, struct evhttp_request* req, T& t, const std::string& req_id) {
        std::string strJsonRes;
        if (!cosmo::util::EncodeJson(t, strJsonRes)) {
            // log failed
        }
        LOG_DEBUG("{}", strJsonRes);
        cosmo::MsgEnvelope msg(
            static_cast<int>(InnerMsgId::kHttpAck),
            std::make_unique<HttpAckTask>(static_cast<int>(hcode), req, std::move(strJsonRes), req_id));

        this->Put(std::move(msg));
    }

    // Enqueue a message
    int Put(cosmo::MsgEnvelope&& msg);

protected:
    // Process response message list
    int HandleRespMsgList();

    // Add response headers
    bool AddHttpHeader(struct evhttp_request* ev_request, const std::string& req_id);
    bool AddHttpOctetHeader(struct evhttp_request* ev_request, const std::string& req_id,
                            const std::string& file_name, long fsize);

    // Common headers: Server, RequestId, AppKey, Nonce, CurTime, CheckSum
    bool AddCommonHeaders(struct evkeyvalq* ev_header, const std::string& req_id);

private:
    // libevent callbacks
    static void ComGenCb(struct evhttp_request* req, void* arg);
    void InsertHttpReqMsg(struct evhttp_request* req);

    void InsertHttpOctetMsg(struct evhttp_request* req);

    int DispatchJsonMsg(HttpAckTask* task);
    int DispatchFileMsg(HttpAckTask* task);

    HttpServerThreadPool thread_pool_;
    std::atomic<bool> is_running_{false};
    struct event_base* event_base_{nullptr};
    struct evhttp* event_http_{nullptr};

    // Periodic tick event to wake the event loop for response queue processing
    struct event* tick_event_{nullptr};

    // Response message queue
    std::list<cosmo::MsgEnvelope> msg_list_;
    std::mutex msg_mutex_;

    std::string app_key_;
    std::string app_secret_;

    DispatcherFactory dispatcher_factory_;
    HttpServerCallbacks callbacks_;

public:
    /// Access callbacks for path resolution (used by MsgHanderThread)
    const HttpServerCallbacks& callbacks() const {
        return callbacks_;
    }
};

}  // namespace cosmo::network::http
