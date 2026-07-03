// EventNotifierImpl — Concrete IEventNotifier implementation — owns the uWebSocket

#include "service/event/impl/EventNotifierImpl.h"

#include "App.h"
#include "util/JsonStructUtil.h"
#include "util/Log.h"
#include "util/StringUtil.h"

namespace cosmo::service {

namespace {
    constexpr int kMaxPayloadLength = 65535;
    constexpr int kIdleTimeoutSec   = 120;
    constexpr int kMaxBackpressure  = 1 * 1024 * 1024;  // 1MB
}  // namespace

static const std::string kWebSocketUrl{"/wsInterface/requestEventResult"};

// ---------------------------------------------------------------------------
// Construction / Destruction
// ---------------------------------------------------------------------------

EventNotifierImpl::EventNotifierImpl() : ws_event_que_("EventNotifier WS QUE", 100) {
    LOG_INFO("{}", "EventNotifierImpl Create");
}

EventNotifierImpl::~EventNotifierImpl() {
    ws_event_que_.Stop();
    LOG_INFO("{}", "EventNotifierImpl Destroy");
}

// ---------------------------------------------------------------------------
// WebSocket server lifecycle (migrated from CHttpuWebSockets)
// ---------------------------------------------------------------------------

bool EventNotifierImpl::InitializeWebSocket(const std::string& hostIp, int port) {
    server_thread_ = std::thread([this, hostIp, port]() { StartServer(hostIp, port); });

    ws_event_que_.SetProcessor([this](cosmo::CMsgOnEventsReq&& data) {
        std::string json_str;
        if (cosmo::util::EncodeJson(data, json_str)) {
            SendWebSocketMsg(kWebSocketUrl, json_str);
        } else {
            LOG_ERRO("{}", "cosmo::util::EncodeJson StructToJson failed");
        }
    });
    return true;
}

void EventNotifierImpl::ShutdownWebSocket() {
    StopServer();
}

bool EventNotifierImpl::StartServer(const std::string& ip, int port) {
    LOG_INFO("{}", "uWebSockets server start.");
    http_app_ = std::make_unique<uWS::TemplatedApp<false>>();
    http_app_
        ->any("/*",
              [](auto* /*res*/, auto* /*req*/) {
                  // placeholder – receive handler intentionally empty
              })
        .options("/*",
                 [](auto* res, auto* /*req*/) {
                     LOG_INFO("{}", "Recv a option request");
                     res->end("OK");
                 })
        .listen(ip, port, [this](auto* token) {
            if (token) {
                http_socket_ = token;
            }
        });
    InitWebSocketServer([](std::string_view /*message*/) {
        // receive handler intentionally empty
    });
    if (http_socket_) {
        LOG_INFO("Start uWebSockets server, {}:{}", ip, port);
        // Construct, run, and destroy must happen on the same thread
        http_app_->run();
        http_app_.reset();
        LOG_INFO("uWebSockets server stopped, {}:{}", ip, port);
        return true;
    }
    LOG_ERRO("Start uWebSockets server failed, {}:{}", ip, port);
    return false;
}

void EventNotifierImpl::InitWebSocketServer(const std::function<void(std::string_view)>& onMessage) {
    http_app_->ws<int>(
        "/*",
        {uWS::DISABLED, kMaxPayloadLength, kIdleTimeoutSec, kMaxBackpressure,
         // open
         [this](auto ws, auto req) {
             LOG_INFO("Recv a websocket request, url: {}, host: {}", req->getUrl(), req->getHeader("host"));
             std::lock_guard<std::mutex> lock(ws_mtx_);
             ws_connections_[cosmo::util::ToLower(req->getUrl())].push_back(ws);
         },
         // message
         [onMessage](auto* /*ws*/, std::string_view message, uWS::OpCode /*opCode*/) { onMessage(message); },
         // drain
         [](auto*) { LOG_INFO("{}", "Websocket message - DRAIN"); },
         // ping
         [](auto*) { LOG_INFO("{}", "Websocket message - PING"); },
         // pong
         [](auto*) { LOG_INFO("{}", "Websocket message - PONG"); },
         // close
         [this](auto* closed_ws, int code, std::string_view message) {
             LOG_INFO("A websocket disconnect, code:{}, message: \"{}\"", code,
                      message.data() ? message : std::string_view{});
             std::lock_guard<std::mutex> lock(ws_mtx_);
             for (auto& it_map : ws_connections_) {
                 auto it_vec = find(it_map.second.begin(), it_map.second.end(), closed_ws);
                 if (it_vec != it_map.second.end()) {
                     it_map.second.erase(it_vec);
                     LOG_INFO("Remove a websocket connection, Url: {}", it_map.first);
                 }
             }
         }});
}

void EventNotifierImpl::StopServer() {
    // Stop and clean up websocket server resources
    {
        // close() is called synchronously, so move data first
        std::unique_lock<std::mutex> lock(ws_mtx_);
        auto tmp = std::move(ws_connections_);
        lock.unlock();
        // Send close to all clients
        if (!tmp.empty()) {
            LOG_INFO("{}", "Closing websocket...");
        }
        for (auto& it_map : tmp) {
            for (auto& it_vec : it_map.second) {
                it_vec->end(0);
            }
        }
        if (http_socket_) {
            us_listen_socket_close(0, http_socket_);
            http_socket_ = nullptr;
        }
    }

    if (server_thread_.joinable()) {
        server_thread_.join();
    }
}

void EventNotifierImpl::SendWebSocketMsg(std::string_view url, std::string_view msg) {
    std::lock_guard<std::mutex> lock(ws_mtx_);
    auto it_map = ws_connections_.find(cosmo::util::ToLower(url));
    if (it_map != ws_connections_.end()) {
        for (auto& ws : it_map->second) {
            if (!ws->send(msg, uWS::OpCode::TEXT)) {
                LOG_ERRO("Send a websocket msg failed, ws point address [{}]", static_cast<const void*>(ws));
            }
        }
    }
}

bool EventNotifierImpl::HasWebSocketConnect(std::string_view url) {
    std::lock_guard<std::mutex> lock(ws_mtx_);
    auto it_map = ws_connections_.find(cosmo::util::ToLower(url));
    return it_map != ws_connections_.end() && !it_map->second.empty();
}

// ---------------------------------------------------------------------------
// IEventNotifier — WebSocket event push
// ---------------------------------------------------------------------------

void EventNotifierImpl::WebSocketEventPush(cosmo::CMsgOnEventsReq& eventData) {
    if (HasWebSocketConnect(kWebSocketUrl)) {
        ws_event_que_.Insert(eventData);
    }
}

// ---------------------------------------------------------------------------
// IEventNotifier — HTTP notifications (Submit callback never registered)
// ---------------------------------------------------------------------------

bool EventNotifierImpl::NotifyComplete(cosmo::CMsgOnCompleteReq& /*req*/, cosmo::CMsgOnCompleteRsp& /*rsp*/) {
    LOG_WARN("{}", "Msg:onComplete But No Cb");
    return false;
}

bool EventNotifierImpl::NotifyInfo(cosmo::CMsgOnInfoReq& /*req*/, cosmo::CMsgonInfoRsp& /*rsp*/) {
    LOG_WARN("{}", "Msg:onInfo But No Cb");
    return false;
}

bool EventNotifierImpl::GetVideoPlayUrl(cosmo::CMsgGetVideoPlayReq& /*req*/,
                                        cosmo::CMsgGetVideoPlayRsp& /*rsp*/) {
    LOG_WARN("{}", "Msg:getVideoPlay But No Cb");
    return false;
}

// ---------------------------------------------------------------------------
// IEventNotifier — queue management (migrated from TaskClientMsgHandle)
// ---------------------------------------------------------------------------

void EventNotifierImpl::SetEventPostQue(cosmo::AsyncQueue<cosmo::CMsgOnEventsReq>& que) {
    LOG_INFO("{}", "Set Event Post Queue");
    event_que_ = &que;
}

void EventNotifierImpl::SetCollectPostQue(cosmo::AsyncQueue<cosmo::CMsgCollectRptReq>& que) {
    LOG_INFO("{}", "Set Collect Post Queue");
    collect_que_ = &que;
}

void EventNotifierImpl::SetFaceEventPostQue(cosmo::AsyncQueue<cosmo::CMsgFaceEventReq>& que) {
    LOG_INFO("{}", "Set FaceEvent Post Queue");
    face_event_que_ = &que;
}

void EventNotifierImpl::EventPush(cosmo::CMsgOnEventsReq& msg) {
    if (event_que_) {
        event_que_->Insert(msg);
    }
}

void EventNotifierImpl::FaceEventPush(cosmo::CMsgFaceEventReq& msg) {
    if (face_event_que_) {
        face_event_que_->Insert(msg);
    } else {
        LOG_WARN("{}/{} Have Not Push. [PUSH QUEUE HAVE NOT REGIST]", msg.recordId, msg.messageId);
    }
}

void EventNotifierImpl::CollectPush(cosmo::CMsgCollectRptReq& msg) {
    if (collect_que_) {
        collect_que_->Insert(msg);
    } else {
        LOG_WARN("{} Have Not Push. [PUSH QUEUE HAVE NOT REGIST]", msg.messageId);
    }
}

}  // namespace cosmo::service
