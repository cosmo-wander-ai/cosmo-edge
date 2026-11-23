// EventNotifierImpl — Concrete IEventNotifier implementation — owns the uWebSocket

#include "service/event/impl/EventNotifierImpl.h"

#include <future>

#include "App.h"
#include "util/JsonStructUtil.h"
#include "util/Log.h"
#include "util/RuntimeIsolation.h"
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
    StopServer();
    LOG_INFO("{}", "EventNotifierImpl Destroy");
}

// ---------------------------------------------------------------------------
// WebSocket server lifecycle (migrated from CHttpuWebSockets)
// ---------------------------------------------------------------------------

bool EventNotifierImpl::InitializeWebSocket(const std::string& hostIp, int port) {
    {
        std::lock_guard<std::mutex> lock(server_mtx_);
        if (server_state_ != ServerState::kStopped || server_thread_.joinable()) {
            LOG_WARN("{}", "uWebSockets server is already initialized");
            return false;
        }
        server_state_ = ServerState::kStarting;
    }

    ws_event_que_.SetProcessor([this](cosmo::CMsgOnEventsReq&& data) {
        std::string json_str;
        if (cosmo::util::EncodeJson(data, json_str)) {
            SendWebSocketMsg(kWebSocketUrl, json_str);
        } else {
            LOG_ERRO("{}", "cosmo::util::EncodeJson StructToJson failed");
        }
    });
    server_thread_ = std::thread([this, hostIp, port]() { StartServer(hostIp, port); });
    if (runtime::IsIsolatedRuntime()) {
        std::unique_lock<std::mutex> lock(server_mtx_);
        server_state_cv_.wait(lock, [this]() { return server_state_ != ServerState::kStarting; });
        return server_state_ == ServerState::kRunning;
    }
    return true;
}

void EventNotifierImpl::ShutdownWebSocket() {
    StopServer();
}

bool EventNotifierImpl::StartServer(const std::string& ip, int port) {
    LOG_INFO("{}", "uWebSockets server start.");
    {
        std::lock_guard<std::mutex> lock(server_mtx_);
        loop_ = uWS::Loop::get();
    }
    http_app_ = std::make_unique<uWS::TemplatedApp<false>>();
    http_app_
        ->any("/*",
              [](auto* res, auto* /*req*/) {
                  // uWebSockets requires every ordinary HTTP handler to either
                  // respond or install an abort handler. Leaving this route
                  // empty terminates the whole engine when a browser or health
                  // probe reaches the WebSocket port without an Upgrade header.
                  res->writeStatus("404 Not Found")->end("Not Found");
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
        {
            std::lock_guard<std::mutex> lock(server_mtx_);
            server_state_ = ServerState::kRunning;
        }
        server_state_cv_.notify_all();
        // Construct, run, and destroy must happen on the same thread
        http_app_->run();
        http_app_.reset();
        {
            std::lock_guard<std::mutex> lock(server_mtx_);
            loop_         = nullptr;
            server_state_ = ServerState::kStopped;
        }
        server_state_cv_.notify_all();
        LOG_INFO("uWebSockets server stopped, {}:{}", ip, port);
        return true;
    }
    http_app_.reset();
    {
        std::lock_guard<std::mutex> lock(server_mtx_);
        loop_         = nullptr;
        server_state_ = ServerState::kStopped;
    }
    server_state_cv_.notify_all();
    LOG_ERRO("Start uWebSockets server failed, {}:{}", ip, port);
    return false;
}

void EventNotifierImpl::InitWebSocketServer(const std::function<void(std::string_view)>& onMessage) {
    http_app_->ws<int>(
        "/*",
        {uWS::DISABLED, kMaxPayloadLength, kIdleTimeoutSec, kMaxBackpressure,
         // open
         [this](auto ws, auto req) {
             LOG_INFO("Recv a websocket request, host: {}", req->getHeader("host"));
             ws_connections_[cosmo::util::ToLower(req->getUrl())].push_back(ws);
             ++ws_connection_count_;
             has_ws_connection_.store(true, std::memory_order_release);
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
             for (auto& it_map : ws_connections_) {
                 auto it_vec = find(it_map.second.begin(), it_map.second.end(), closed_ws);
                 if (it_vec != it_map.second.end()) {
                     it_map.second.erase(it_vec);
                     --ws_connection_count_;
                     has_ws_connection_.store(ws_connection_count_ != 0, std::memory_order_release);
                     LOG_INFO("{}", "Remove a websocket connection");
                     break;
                 }
             }
         }});
}

void EventNotifierImpl::StopServer() {
    std::lock_guard<std::mutex> shutdown_lock(shutdown_mtx_);
    uWS::Loop* loop = nullptr;
    {
        std::unique_lock<std::mutex> lock(server_mtx_);
        server_state_cv_.wait(lock, [this]() { return server_state_ != ServerState::kStarting; });
        if (server_state_ == ServerState::kRunning) {
            server_state_ = ServerState::kStopping;
            loop          = loop_;
        }
    }

    if (loop) {
        std::promise<void> close_completed;
        auto close_future = close_completed.get_future();
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnull-dereference"
#endif
        // GCC reports a false positive while inlining function2's std::align-based small-object storage.
        loop->defer([this, close_completed = std::move(close_completed)]() mutable {
            CloseWebSocketServerOnLoop();
            close_completed.set_value();
        });
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif
        close_future.wait();
    }
    if (server_thread_.joinable()) {
        server_thread_.join();
    }
}

void EventNotifierImpl::SendWebSocketMsg(std::string_view url, std::string_view msg) {
    std::lock_guard<std::mutex> lock(server_mtx_);
    if (server_state_ != ServerState::kRunning) {
        return;
    }
    auto url_copy = std::string(url);
    auto msg_copy = std::string(msg);
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnull-dereference"
#endif
    // GCC reports a false positive while inlining function2's std::align-based small-object storage.
    loop_->defer(
        [this, url = std::move(url_copy), msg = std::move(msg_copy)]() { SendWebSocketMsgOnLoop(url, msg); });
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif
}

void EventNotifierImpl::SendWebSocketMsgOnLoop(std::string_view url, std::string_view msg) {
    auto it_map = ws_connections_.find(cosmo::util::ToLower(url));
    if (it_map != ws_connections_.end()) {
        for (auto& ws : it_map->second) {
            if (!ws->send(msg, uWS::OpCode::TEXT)) {
                LOG_ERRO("Send a websocket msg failed, ws point address [{}]", static_cast<const void*>(ws));
            }
        }
    }
}

void EventNotifierImpl::CloseWebSocketServerOnLoop() {
    auto connections     = std::move(ws_connections_);
    ws_connection_count_ = 0;
    has_ws_connection_.store(false, std::memory_order_release);
    if (!connections.empty()) {
        LOG_INFO("{}", "Closing websocket...");
    }
    for (auto& it_map : connections) {
        for (auto* ws : it_map.second) {
            ws->end(0);
        }
    }
    if (http_socket_) {
        us_listen_socket_close(0, http_socket_);
        http_socket_ = nullptr;
    }
}

bool EventNotifierImpl::HasWebSocketConnect(std::string_view url) {
    return url == kWebSocketUrl && has_ws_connection_.load(std::memory_order_acquire);
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
    std::lock_guard<std::mutex> lock(post_queue_mtx_);
    event_que_ = &que;
}

void EventNotifierImpl::ClearEventPostQue(const cosmo::AsyncQueue<cosmo::CMsgOnEventsReq>& que) {
    std::lock_guard<std::mutex> lock(post_queue_mtx_);
    if (event_que_ == &que) {
        event_que_ = nullptr;
    }
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
    std::lock_guard<std::mutex> lock(post_queue_mtx_);
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
