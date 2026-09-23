// HttpServer — Http Server implementation.

#include "network/http/HttpServer.h"

#include <event2/keyvalq_struct.h>
#include <event2/util.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <charconv>
#include <cstdio>
#include <cstring>
#include <exception>
#include <filesystem>
#include <limits>
#include <memory>
#include <optional>
#include <string_view>
#include <utility>

#include "event2/http.h"
#include "network/http/HttpCommon.h"
#include "network/http/MultipartStreamParser.h"
#include "util/CipherUtil.h"
#include "util/ErrorCode.h"
#include "util/FileUtil.h"
#include "util/JsonStructUtil.h"
#include "util/Log.h"
#include "util/MsgBaseTypes.h"
#include "util/UuidUtil.h"

namespace chrono = std::chrono;

namespace cosmo::network::http {

HttpAckTask::~HttpAckTask() {
    if (!delete_file_after_send || file_path.empty()) {
        return;
    }
    std::error_code error;
    std::filesystem::remove(file_path, error);
    if (error) {
        LOG_WARN("Failed to clean temporary download {}: {}", file_path, error.message());
    }
}

namespace {

    constexpr auto kShutdownDrainTimeout         = chrono::seconds(5);
    constexpr std::size_t kMaxRequestTargetBytes = 2048;
    constexpr std::size_t kMaxJsonBodyBytes      = 1 * 1024 * 1024;
    constexpr std::size_t kMaxHttpBodyBytes      = 10 * 1024 * 1024;
    // Keep libevent's non-customizable emergency backstop above every
    // application boundary. Requests that cross the JSON/multipart limits
    // reach ComGenCb and receive the structured, actionable error contract;
    // only clearly abusive bodies fall through to libevent's built-in 413.
    constexpr std::size_t kMaxHttpTransportBodyBytes             = 12 * 1024 * 1024;
    constexpr std::size_t kMaxHttpHeaderBytes                    = 32 * 1024;
    constexpr std::size_t kMaxMultipartSpoolRequests             = 8;
    constexpr std::size_t kMaxMultipartSpoolRequestsPerPrincipal = 2;
    constexpr std::uint64_t kMaxMultipartSpoolBytes = kMaxMultipartSpoolRequests * kMaxHttpBodyBytes;
    constexpr std::uint64_t kMaxMultipartSpoolBytesPerPrincipal =
        kMaxMultipartSpoolRequestsPerPrincipal * kMaxHttpBodyBytes;
    constexpr std::string_view kLogPrefix{"/logs/"};

    std::string_view Trim(std::string_view value) {
        const auto begin = value.find_first_not_of(" \t");
        if (begin == std::string_view::npos) {
            return {};
        }
        const auto end = value.find_last_not_of(" \t");
        return value.substr(begin, end - begin + 1);
    }

    std::string ToLower(std::string_view value) {
        std::string result(value);
        std::transform(result.begin(), result.end(), result.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return result;
    }

    enum class RangeResult {
        kFull,
        kPartial,
        kInvalid,
    };

    struct ByteRange {
        std::uint64_t first{0};
        std::uint64_t last{0};
    };

    bool ParseUnsigned(std::string_view value, std::uint64_t& result) {
        if (value.empty()) {
            return false;
        }
        const auto* begin = value.data();
        const auto* end   = begin + value.size();
        const auto parsed = std::from_chars(begin, end, result);
        return parsed.ec == std::errc{} && parsed.ptr == end;
    }

    RangeResult ParseRangeHeader(std::string_view header, std::uint64_t file_size, ByteRange& result) {
        header = Trim(header);
        if (header.empty()) {
            if (file_size > 0) {
                result.last = file_size - 1;
            }
            return RangeResult::kFull;
        }

        constexpr std::string_view kPrefix{"bytes="};
        if (header.size() <= kPrefix.size() || ToLower(header.substr(0, kPrefix.size())) != kPrefix ||
            header.find(',') != std::string_view::npos || file_size == 0) {
            return RangeResult::kInvalid;
        }

        const auto value = Trim(header.substr(kPrefix.size()));
        const auto dash  = value.find('-');
        if (dash == std::string_view::npos || value.find('-', dash + 1) != std::string_view::npos) {
            return RangeResult::kInvalid;
        }
        const auto first_text = Trim(value.substr(0, dash));
        const auto last_text  = Trim(value.substr(dash + 1));

        if (first_text.empty()) {
            std::uint64_t suffix{0};
            if (!ParseUnsigned(last_text, suffix) || suffix == 0) {
                return RangeResult::kInvalid;
            }
            const auto length = std::min(suffix, file_size);
            result.first      = file_size - length;
            result.last       = file_size - 1;
            return RangeResult::kPartial;
        }

        if (!ParseUnsigned(first_text, result.first) || result.first >= file_size) {
            return RangeResult::kInvalid;
        }
        if (last_text.empty()) {
            result.last = file_size - 1;
            return RangeResult::kPartial;
        }
        if (!ParseUnsigned(last_text, result.last) || result.last < result.first) {
            return RangeResult::kInvalid;
        }
        result.last = std::min(result.last, file_size - 1);
        return RangeResult::kPartial;
    }

    const char* FindHeader(struct evkeyvalq* headers, const char* name) {
        if (headers == nullptr) {
            return nullptr;
        }
        return evhttp_find_header(headers, name);
    }

    int HexValue(unsigned char value) {
        if (value >= '0' && value <= '9') {
            return value - '0';
        }
        value = static_cast<unsigned char>(std::tolower(value));
        return value >= 'a' && value <= 'f' ? value - 'a' + 10 : -1;
    }

    bool DecodeRequestPath(const std::string& request_target, std::string& decoded_path) {
        decoded_path.clear();
        if (request_target.empty() || request_target.size() > kMaxRequestTargetBytes ||
            request_target.front() != '/' || request_target.find('#') != std::string::npos) {
            return false;
        }

        const auto query_pos = request_target.find('?');
        const auto path_size = query_pos == std::string::npos ? request_target.size() : query_pos;
        decoded_path.reserve(path_size);
        for (std::size_t i = 0; i < path_size; ++i) {
            unsigned char value = static_cast<unsigned char>(request_target[i]);
            if (value == '%') {
                if (i + 2 >= path_size) {
                    return false;
                }
                const int high = HexValue(static_cast<unsigned char>(request_target[i + 1]));
                const int low  = HexValue(static_cast<unsigned char>(request_target[i + 2]));
                if (high < 0 || low < 0) {
                    return false;
                }
                value = static_cast<unsigned char>((high << 4) | low);
                i += 2;
            }
            if (value == 0 || value < 0x20 || value == 0x7f) {
                return false;
            }
            decoded_path.push_back(static_cast<char>(value));
        }
        return !decoded_path.empty();
    }

    bool IsMultipartContentType(const char* content_type) {
        if (content_type == nullptr) {
            return false;
        }
        const std::string_view value(content_type);
        const auto separator = value.find(';');
        return ToLower(Trim(value.substr(0, separator))) == "multipart/form-data";
    }

    std::optional<std::string> ParseMultipartBoundary(std::string_view content_type) {
        const auto first_separator = content_type.find(';');
        if (ToLower(Trim(content_type.substr(0, first_separator))) != "multipart/form-data" ||
            first_separator == std::string_view::npos) {
            return std::nullopt;
        }

        std::optional<std::string> boundary;
        std::size_t begin = first_separator + 1;
        while (begin <= content_type.size()) {
            auto end = content_type.find(';', begin);
            if (end == std::string_view::npos) {
                end = content_type.size();
            }
            const auto parameter = Trim(content_type.substr(begin, end - begin));
            const auto equals    = parameter.find('=');
            if (equals != std::string_view::npos &&
                ToLower(Trim(parameter.substr(0, equals))) == "boundary") {
                if (boundary) {
                    return std::nullopt;
                }
                auto raw = Trim(parameter.substr(equals + 1));
                if (raw.size() >= 2 && raw.front() == '"' && raw.back() == '"') {
                    raw = raw.substr(1, raw.size() - 2);
                }
                if (raw.empty() || raw.size() > 200 || raw.front() == ' ' || raw.back() == ' ' ||
                    std::any_of(raw.begin(), raw.end(), [](unsigned char c) {
                        return c < 0x20 || c > 0x7e || c == '"' || c == '\\' || c == ';';
                    })) {
                    return std::nullopt;
                }
                boundary = std::string(raw);
            }
            if (end == content_type.size()) {
                break;
            }
            begin = end + 1;
        }
        return boundary;
    }

}  // namespace

HttpServer::~HttpServer() {
    UnInitialize();
}

HttpServer::MultipartSpoolReservation::~MultipartSpoolReservation() {
    if (active && server != nullptr) {
        server->ReleaseMultipartSpool(owner, bytes);
    }
}

void HttpServer::ComGenCb(struct evhttp_request* req, void* arg) {
    if (!req) {
        LOG_WARN("{}", "ComGenCb Get Null Request");
        return;
    }
    auto* http_server = static_cast<HttpServer*>(arg);
    if (http_server == nullptr) {
        return;
    }
    // Libevent has already generated the response when its emergency
    // transport limit is exceeded. It clears the URI before invoking the
    // generic callback, so leave that response untouched rather than trying to
    // send a second reply through an already-finalized request.
    if (evhttp_request_get_response_code(req) == static_cast<int>(HttpResponseCode::kPayloadTooLarge)) {
        LOG_WARN("{}", "Reject HTTP request at libevent emergency body limit");
        return;
    }
    if (!http_server->is_accepting_requests_) {
        http_server->SendImmediateError(req, HttpResponseCode::kServiceUnavailable);
        return;
    }

    RequestDispatchContext context;
    bool is_log_request = false;
    if (!http_server->PrepareRequestContext(req, context, is_log_request)) {
        http_server->SendImmediateError(req, HttpResponseCode::kBadRequest);
        return;
    }

    const auto admission = http_server->request_inspector_->InspectRequest(context, !is_log_request);
    if (admission == RequestAdmission::kUnauthorized) {
        http_server->SendImmediateAuthError(req);
        return;
    }
    if (admission == RequestAdmission::kRouteNotFound) {
        http_server->SendImmediateError(req, HttpResponseCode::kBadRequest);
        return;
    }
    if (!http_server->IsBodySizeAllowed(req)) {
        http_server->SendImmediateError(req, HttpResponseCode::kPayloadTooLarge);
        return;
    }

    if (is_log_request) {
        http_server->InsertHttpOctetMsg(req, context);
    } else {
        http_server->InsertHttpReqMsg(req, context);
    }
}

void HttpServer::RequestCompleteCb(struct evhttp_request* req, void* arg) {
    auto* context = static_cast<RequestContext*>(arg);
    if (context == nullptr) {
        return;
    }
    auto* server = context->server;
    auto token   = context->token;
    server->CompleteRequest(token, req);
}

void HttpServer::ConnectionCloseCb(struct evhttp_connection* connection, void* arg) {
    auto* context = static_cast<RequestContext*>(arg);
    if (context == nullptr) {
        return;
    }
    auto* server = context->server;
    auto token   = context->token;
    server->CloseRequest(token, connection);
}

void HttpServer::InsertHttpReqMsg(struct evhttp_request* req, const RequestDispatchContext& context) {
    InsertHttpMsg(req, InnerMsgId::kHttpReq, context);
}

void HttpServer::InsertHttpOctetMsg(struct evhttp_request* req, const RequestDispatchContext& context) {
    InsertHttpMsg(req, InnerMsgId::kHttpOctetReq, context);
}

void HttpServer::InsertHttpMsg(struct evhttp_request* req, InnerMsgId msg_id,
                               const RequestDispatchContext& context) {
    std::shared_ptr<void> spool_reservation;
    const auto* content_type = FindHeader(evhttp_request_get_input_headers(req), "Content-Type");
    if (IsMultipartContentType(content_type)) {
        spool_reservation =
            TryReserveMultipartSpool(context, evbuffer_get_length(evhttp_request_get_input_buffer(req)));
        if (!spool_reservation) {
            LOG_WARN("Reject multipart request for {} because the spool quota is full", context.uri);
            SendImmediateError(req, HttpResponseCode::kServiceUnavailable);
            return;
        }
    }

    auto error_code = HttpResponseCode::kBadRequest;
    auto task       = BuildHttpReqTask(req, context, error_code);
    if (!task) {
        SendImmediateError(req, error_code);
        return;
    }
    task->multipart_spool_reservation = std::move(spool_reservation);

    auto token = RegisterRequest(req);
    if (token == kInvalidHttpRequestToken) {
        if (task->has_tmp_path) {
            cosmo::util::RemovePath(task->tmp_file_path);
        }
        SendImmediateError(req, HttpResponseCode::kInternalError);
        return;
    }
    task->request_token = token;

    auto tmp_file_path = task->tmp_file_path;
    auto has_tmp_path  = task->has_tmp_path;
    cosmo::MsgEnvelope msg(static_cast<int>(msg_id), std::move(task));
    if (thread_pool_.PutMsg(std::move(msg)) < 0) {
        if (has_tmp_path) {
            cosmo::util::RemovePath(tmp_file_path);
        }
        HttpAckTask response_task(static_cast<int>(HttpResponseCode::kServiceUnavailable), token, "{}", "");
        DispatchJsonMsg(&response_task);
        return;
    }
    LOG_DEBUG("Put request {} token {} to HTTP pool", static_cast<int>(msg_id), token);
}

std::unique_ptr<HttpReqTask> HttpServer::BuildHttpReqTask(struct evhttp_request* req,
                                                          const RequestDispatchContext& context,
                                                          HttpResponseCode& error_code) {
    error_code         = HttpResponseCode::kBadRequest;
    auto task          = std::make_unique<HttpReqTask>();
    task->request_time = chrono::steady_clock::now();
    task->interface    = context.uri;
    task->mtk          = context.credential;
    task->principal    = context.principal;
    switch (evhttp_request_get_command(req)) {
        case EVHTTP_REQ_POST:
            task->http_method = "POST";
            break;
        case EVHTTP_REQ_PUT:
            task->http_method = "PUT";
            break;
        default:
            task->http_method = "OTHER";
            break;
    }
    if (const auto* id = FindHeader(evhttp_request_get_input_headers(req), "X-Upload-Id")) {
        task->upload_id = id;
    }
    if (const auto* offset = FindHeader(evhttp_request_get_input_headers(req), "X-Upload-Offset")) {
        task->upload_offset = offset;
    }

    auto* input_headers = evhttp_request_get_input_headers(req);
    if (const auto* forwarded_for = FindHeader(input_headers, "x-forwarded-for")) {
        task->x_forwarded_for = forwarded_for;
    }
    if (const auto* range = FindHeader(input_headers, "Range")) {
        task->range_request = range;
    }
    const auto* content_type = FindHeader(input_headers, "Content-Type");
    if (IsMultipartContentType(content_type)) {
        if (!ExtractMultipartBody(task.get(), req, content_type, error_code)) {
            return nullptr;
        }
        return task;
    }

    auto* input_buffer = evhttp_request_get_input_buffer(req);
    auto body_length   = evbuffer_get_length(input_buffer);
    if (body_length == 0) {
        return task;
    }

    auto* body_data = evbuffer_pullup(input_buffer, -1);
    if (body_data == nullptr) {
        LOG_ERRO("Failed to copy request body for URI {}", task->interface);
        error_code = HttpResponseCode::kInternalError;
        return nullptr;
    }
    task->body.resize(body_length);
    std::memcpy(task->body.data(), body_data, body_length);
    return task;
}

bool HttpServer::PrepareRequestContext(struct evhttp_request* req, RequestDispatchContext& context,
                                       bool& is_log_request) const {
    context                    = {};
    is_log_request             = false;
    const auto* request_target = evhttp_request_get_uri(req);
    if (request_target == nullptr || !DecodeRequestPath(request_target, context.uri)) {
        return false;
    }

    context.transport = RequestTransport::kHttp;
    auto* headers     = evhttp_request_get_input_headers(req);
    if (const auto* mtk = FindHeader(headers, "mtk")) {
        context.credential = mtk;
    }
    is_log_request = context.uri.compare(0, kLogPrefix.size(), kLogPrefix) == 0;
    return true;
}

bool HttpServer::IsBodySizeAllowed(struct evhttp_request* req) const {
    const auto* content_type = FindHeader(evhttp_request_get_input_headers(req), "Content-Type");
    const auto limit         = IsMultipartContentType(content_type) ? kMaxHttpBodyBytes : kMaxJsonBodyBytes;
    return evbuffer_get_length(evhttp_request_get_input_buffer(req)) <= limit;
}

std::shared_ptr<void> HttpServer::TryReserveMultipartSpool(const RequestDispatchContext& context,
                                                           std::uint64_t bytes) {
    const std::string owner = context.principal.empty() ? std::string("<anonymous>") : context.principal;
    auto reservation        = std::make_shared<MultipartSpoolReservation>();
    reservation->server     = this;
    reservation->owner      = owner;
    reservation->bytes      = bytes;

    std::lock_guard<std::mutex> lock(multipart_spool_mutex_);
    const auto owner_requests_it = multipart_spool_requests_by_owner_.find(owner);
    const auto owner_bytes_it    = multipart_spool_bytes_by_owner_.find(owner);
    const auto owner_requests =
        owner_requests_it == multipart_spool_requests_by_owner_.end() ? 0 : owner_requests_it->second;
    const auto owner_bytes =
        owner_bytes_it == multipart_spool_bytes_by_owner_.end() ? 0 : owner_bytes_it->second;
    if (multipart_spool_request_count_ >= kMaxMultipartSpoolRequests ||
        owner_requests >= kMaxMultipartSpoolRequestsPerPrincipal ||
        bytes > kMaxMultipartSpoolBytes - multipart_spool_bytes_ ||
        bytes > kMaxMultipartSpoolBytesPerPrincipal - owner_bytes) {
        return {};
    }

    ++multipart_spool_request_count_;
    ++multipart_spool_requests_by_owner_[owner];
    multipart_spool_bytes_ += bytes;
    multipart_spool_bytes_by_owner_[owner] += bytes;
    reservation->active = true;
    return reservation;
}

void HttpServer::ReleaseMultipartSpool(const std::string& owner, std::uint64_t bytes) {
    std::lock_guard<std::mutex> lock(multipart_spool_mutex_);
    const auto requests    = multipart_spool_requests_by_owner_.find(owner);
    const auto owner_bytes = multipart_spool_bytes_by_owner_.find(owner);
    if (multipart_spool_request_count_ == 0 || multipart_spool_bytes_ < bytes ||
        requests == multipart_spool_requests_by_owner_.end() || requests->second == 0 ||
        owner_bytes == multipart_spool_bytes_by_owner_.end() || owner_bytes->second < bytes) {
        LOG_ERRO("{}", "Multipart spool accounting underflow for authenticated owner");
        return;
    }

    --multipart_spool_request_count_;
    multipart_spool_bytes_ -= bytes;
    if (--requests->second == 0) {
        multipart_spool_requests_by_owner_.erase(requests);
    }
    owner_bytes->second -= bytes;
    if (owner_bytes->second == 0) {
        multipart_spool_bytes_by_owner_.erase(owner_bytes);
    }
}

bool HttpServer::ExtractMultipartBody(HttpReqTask* task, struct evhttp_request* req,
                                      const std::string& content_type, HttpResponseCode& error_code) {
    auto* input_headers = evhttp_request_get_input_headers(req);
    LOG_INFO("Receive uri:{}, handle multipart/form-data", task->interface);
    if (const auto* content_length = FindHeader(input_headers, "Content-Length")) {
        LOG_INFO("Content-Length:{}", content_length);
    }

    auto boundary = ParseMultipartBoundary(content_type);
    if (!boundary) {
        LOG_WARN("Receive uri:{}, multipart boundary is missing", task->interface);
        return false;
    }

    if (!callbacks_.get_upload_tmp_path) {
        LOG_ERRO("{}", "Upload temporary path callback is not configured");
        error_code = HttpResponseCode::kInternalError;
        return false;
    }
    task->tmp_file_path = callbacks_.get_upload_tmp_path();
    std::error_code path_error;
    if (task->tmp_file_path.empty() || !std::filesystem::is_directory(task->tmp_file_path, path_error) ||
        path_error) {
        LOG_ERRO("Upload temporary directory is unavailable: {}", task->tmp_file_path);
        task->tmp_file_path.clear();
        error_code = HttpResponseCode::kInternalError;
        return false;
    }
    task->has_tmp_path = true;

    MultipartStreamParser parser(std::move(*boundary));
    auto result = parser.ParseToFile(evhttp_request_get_input_buffer(req), [&]() {
        return (std::filesystem::path(task->tmp_file_path) / cosmo::util::GenerateUUID()).string();
    });
    if (!result.ok) {
        LOG_ERRO("Parse multipart request failed: {}", result.err);
        cosmo::util::RemovePath(task->tmp_file_path);
        task->has_tmp_path = false;
        task->tmp_file_path.clear();
        if (result.error == MultipartParseError::kPayloadTooLarge) {
            error_code = HttpResponseCode::kPayloadTooLarge;
        } else if (result.error == MultipartParseError::kIo) {
            error_code = HttpResponseCode::kInternalError;
        }
        return false;
    }
    if (!cosmo::util::EncodeJson(result.fields, task->body)) {
        LOG_ERRO("{}", "Encode multipart fields failed");
        cosmo::util::RemovePath(task->tmp_file_path);
        task->has_tmp_path = false;
        task->tmp_file_path.clear();
        error_code = HttpResponseCode::kInternalError;
        return false;
    }
    task->multipart_file_path = std::move(result.uploaded_path);
    task->multipart_file_name = std::move(result.uploaded_name);
    task->multipart_file_size = result.uploaded_size;
    return true;
}

void HttpServer::SetAppInfo(const std::string& app_key, const std::string& app_secret) {
    app_key_    = app_key;
    app_secret_ = app_secret;
}

bool HttpServer::Initialize(const std::string& host_ip, int port, DispatcherFactory factory,
                            HttpServerCallbacks callbacks) {
    {
        std::lock_guard<std::mutex> lock(lifecycle_mutex_);
        if (lifecycle_state_ != LifecycleState::kStopped) {
            LOG_ERRO("{}", "HTTP server is already initialized");
            return false;
        }
    }

    dispatcher_factory_ = std::move(factory);
    callbacks_          = std::move(callbacks);
    if (!dispatcher_factory_) {
        LOG_ERRO("{}", "HTTP request dispatcher factory is not configured");
        return false;
    }
    try {
        request_inspector_ = dispatcher_factory_();
    } catch (const std::exception& e) {
        LOG_ERRO("Cannot create HTTP request inspector: {}", e.what());
        return false;
    }
    if (!request_inspector_) {
        LOG_ERRO("{}", "HTTP request inspector factory returned null");
        return false;
    }
    if (!(event_base_ = event_base_new())) {
        LOG_ERRO("{}", "event_base_new return NULL");
        return false;
    }

    if (!(event_http_ = evhttp_new(event_base_))) {
        LOG_ERRO("{}", "evhttp_new return NULL");
        CleanupEventResources();
        return false;
    }
    // The application limits below this value produce structured errors.
    // This higher finite boundary is only an emergency memory-safety backstop
    // while libevent is receiving a request.
    evhttp_set_max_body_size(event_http_, static_cast<ev_ssize_t>(kMaxHttpTransportBodyBytes));
    evhttp_set_max_headers_size(event_http_, static_cast<ev_ssize_t>(kMaxHttpHeaderBytes));

    // 5ms tick: ensures max response latency is bounded, while blocking to yield CPU when idle
    if (!tick_event_) {
        tick_event_ = event_new(
            event_base_, -1, EV_PERSIST,
            [](evutil_socket_t /*fd*/, short /*events*/, void* /*arg*/) {
                // Only used to periodically wake the event loop so dispatchMsg() can process the response
                // queue, avoiding busy-waiting.
            },
            this);
        if (!tick_event_) {
            LOG_ERRO("{}", "event_new tick failed");
            CleanupEventResources();
            return false;
        }
        struct timeval tv;
        tv.tv_sec  = 0;
        tv.tv_usec = 5000;  // 5ms
        if (event_add(tick_event_, &tv) != 0) {
            LOG_ERRO("{}", "event_add tick failed");
            event_free(tick_event_);
            tick_event_ = nullptr;
            CleanupEventResources();
            return false;
        }
    }

    // generic cb
    evhttp_set_gencb(event_http_, ComGenCb, this);

    // allow post method
    evhttp_set_allowed_methods(event_http_,
                               EVHTTP_REQ_POST | EVHTTP_REQ_GET | EVHTTP_REQ_HEAD | EVHTTP_REQ_PUT);

    bound_socket_ = evhttp_bind_socket_with_handle(event_http_, host_ip.c_str(), static_cast<uint16_t>(port));
    if (bound_socket_ == nullptr) {
        LOG_ERRO("evhttp_bind_socket ip:[{}], port:[{}] failed", host_ip, port);
        CleanupEventResources();
        return false;
    }

    constexpr int kThreadNum        = 4;
    bool is_thread_pool_initialized = false;
    try {
        is_thread_pool_initialized = thread_pool_.Initialize(kThreadNum, this, dispatcher_factory_);
    } catch (const std::exception& e) {
        LOG_ERRO("Cannot initialize HTTP request thread pool: {}", e.what());
    } catch (...) {
        LOG_ERRO("{}", "Cannot initialize HTTP request thread pool: unknown exception");
    }
    if (!is_thread_pool_initialized) {
        CleanupEventResources();
        return false;
    }

    next_request_token_    = 1;
    is_accepting_requests_ = true;
    is_running_            = true;
    {
        std::lock_guard<std::mutex> lock(lifecycle_mutex_);
        lifecycle_state_ = LifecycleState::kInitialized;
    }

    LOG_INFO("libevent http server ip[{}]-port[{}] start success", host_ip, port);

    return true;
}

void HttpServer::UnInitialize() {
    RequestStop();

    std::unique_lock<std::mutex> lock(lifecycle_mutex_);
    if (lifecycle_state_ == LifecycleState::kStopped) {
        return;
    }

    if (lifecycle_state_ == LifecycleState::kDispatching || lifecycle_state_ == LifecycleState::kStopping) {
        if (event_thread_id_ == std::this_thread::get_id()) {
            return;
        }
        lifecycle_cv_.wait(lock, [this]() { return lifecycle_state_ == LifecycleState::kStopped; });
        return;
    }

    lifecycle_state_ = LifecycleState::kStopping;
    event_thread_id_ = std::this_thread::get_id();
    lock.unlock();
    ShutdownOnEventThread();
    MarkStopped();
}

void HttpServer::RequestStop() noexcept {
    is_accepting_requests_.store(false, std::memory_order_release);
    is_running_.store(false, std::memory_order_release);
}

void HttpServer::StopAccepting() {
    is_accepting_requests_ = false;
    if (event_http_ != nullptr && bound_socket_ != nullptr) {
        evhttp_del_accept_socket(event_http_, bound_socket_);
        bound_socket_ = nullptr;
    }
}

void HttpServer::ShutdownOnEventThread() {
    StopAccepting();

    // No new request tasks can enter after StopAccepting. Drain every task that
    // was accepted before releasing any request or connection owned by libevent.
    thread_pool_.Uninitialize();
    HandleRespMsgList();

    auto deadline = chrono::steady_clock::now() + kShutdownDrainTimeout;
    while (event_base_ != nullptr && !active_requests_.empty() && chrono::steady_clock::now() < deadline) {
        auto rc = event_base_loop(event_base_, EVLOOP_ONCE);
        if (rc < 0) {
            LOG_ERRO("{}", "event_base_loop failed while draining HTTP responses");
            break;
        }
        HandleRespMsgList();
    }
    if (!active_requests_.empty()) {
        LOG_WARN("Force closing {} HTTP request(s) after shutdown drain timeout", active_requests_.size());
    }

    CleanupEventResources();
    {
        std::lock_guard<std::mutex> lock(msg_mutex_);
        msg_list_.clear();
    }
}

void HttpServer::CleanupEventResources() {
    if (tick_event_ != nullptr) {
        event_free(tick_event_);
        tick_event_ = nullptr;
    }
    if (event_http_ != nullptr) {
        evhttp_free(event_http_);
        event_http_   = nullptr;
        bound_socket_ = nullptr;
    }
    active_requests_.clear();
    if (event_base_ != nullptr) {
        event_base_free(event_base_);
        event_base_ = nullptr;
    }
    request_inspector_.reset();
}

void HttpServer::MarkStopped() {
    {
        std::lock_guard<std::mutex> lock(lifecycle_mutex_);
        lifecycle_state_ = LifecycleState::kStopped;
        event_thread_id_ = std::thread::id{};
    }
    lifecycle_cv_.notify_all();
}

HttpRequestToken HttpServer::RegisterRequest(struct evhttp_request* req) {
    auto* connection = evhttp_request_get_connection(req);
    if (connection == nullptr) {
        LOG_ERRO("{}", "HTTP request has no connection");
        return kInvalidHttpRequestToken;
    }

    auto token = next_request_token_++;
    while (token == kInvalidHttpRequestToken || active_requests_.find(token) != active_requests_.end()) {
        token = next_request_token_++;
    }
    auto context      = std::make_unique<RequestContext>(RequestContext{this, token, req, connection});
    auto* context_ptr = context.get();
    active_requests_.emplace(token, std::move(context));
    evhttp_request_set_on_complete_cb(req, RequestCompleteCb, context_ptr);
    evhttp_connection_set_closecb(connection, ConnectionCloseCb, context_ptr);
    return token;
}

struct evhttp_request* HttpServer::FindRequest(HttpRequestToken token) const {
    auto it = active_requests_.find(token);
    if (it == active_requests_.end()) {
        return nullptr;
    }
    return it->second->request;
}

void HttpServer::CompleteRequest(HttpRequestToken token, const struct evhttp_request* req) {
    auto it = active_requests_.find(token);
    if (it == active_requests_.end() || it->second->request != req) {
        return;
    }
    evhttp_connection_set_closecb(it->second->connection, nullptr, nullptr);
    active_requests_.erase(it);
}

void HttpServer::CloseRequest(HttpRequestToken token, struct evhttp_connection* connection) {
    auto it = active_requests_.find(token);
    if (it == active_requests_.end() || it->second->connection != connection) {
        return;
    }
    evhttp_connection_set_closecb(connection, nullptr, nullptr);
    active_requests_.erase(it);
}

void HttpServer::SendImmediateAuthError(struct evhttp_request* req) {
    MsgSendHead response;
    response.resCode = kServerRspFailed;

    MsgResBase message;
    message.msgCode    = std::to_string(static_cast<int>(util::ErrorEnum::AuthFailed));
    message.messageKey = "api.error." + util::ErrorEnumName(util::ErrorEnum::AuthFailed);
    message.msgText    = "Auth Failed";
    response.resMsg.push_back(std::move(message));

    std::string response_body;
    if (!cosmo::util::EncodeJson(response, response_body)) {
        SendImmediateError(req, HttpResponseCode::kNeedAuthenticate);
        return;
    }

    struct evbuffer* buffer = evbuffer_new();
    if (buffer == nullptr) {
        SendImmediateError(req, HttpResponseCode::kNeedAuthenticate);
        return;
    }
    if (!AddHttpHeader(req, "")) {
        evbuffer_free(buffer);
        SendImmediateError(req, HttpResponseCode::kNeedAuthenticate);
        return;
    }
    evbuffer_add(buffer, response_body.data(), response_body.size());

    const auto& code_messages = GetHttpResCodeMsg();
    const auto code_value     = static_cast<int>(HttpResponseCode::kNeedAuthenticate);
    const auto it             = code_messages.find(code_value);
    const char* reason        = it == code_messages.end() ? "NEED AUTHENTICATE" : it->second.c_str();
    evhttp_send_reply(req, code_value, reason, buffer);
    evbuffer_free(buffer);
}

void HttpServer::SendImmediateError(struct evhttp_request* req, HttpResponseCode code) {
    const auto& code_messages = GetHttpResCodeMsg();
    auto code_value           = static_cast<int>(code);
    auto it                   = code_messages.find(code_value);
    const char* message       = it == code_messages.end() ? "ERROR" : it->second.c_str();
    if (code != HttpResponseCode::kPayloadTooLarge && code != HttpResponseCode::kServiceUnavailable) {
        evhttp_send_error(req, code_value, message);
        return;
    }

    MsgSendHead response;
    response.resCode = kServerRspFailed;
    MsgResBase error;
    if (code == HttpResponseCode::kPayloadTooLarge) {
        const auto* content_type = FindHeader(evhttp_request_get_input_headers(req), "Content-Type");
        const auto limit     = IsMultipartContentType(content_type) ? kMaxHttpBodyBytes : kMaxJsonBodyBytes;
        std::uint64_t actual = evbuffer_get_length(evhttp_request_get_input_buffer(req));
        if (const auto* content_length =
                FindHeader(evhttp_request_get_input_headers(req), "Content-Length")) {
            std::uint64_t declared{0};
            const std::string_view value(content_length);
            if (ParseUnsigned(value, declared)) {
                actual = std::max(actual, declared);
            }
        }
        error.msgCode             = "HTTP_BODY_TOO_LARGE";
        error.messageKey          = "api.error.httpBodyTooLarge";
        error.msgText             = "HTTP request body exceeds the request boundary";
        error.details.actualBytes = actual;
        error.details.limitBytes  = limit;
        error.recommendedAction =
            IsMultipartContentType(content_type) ? "USE_CHUNKED_UPLOAD" : "REDUCE_REQUEST_BODY";
    } else {
        error.msgCode           = "HTTP_SERVICE_BUSY";
        error.messageKey        = "api.error.httpServiceBusy";
        error.msgText           = "HTTP transfer capacity is temporarily busy";
        error.retryable         = true;
        error.recommendedAction = "RETRY";
        error.retryAfterSeconds = 1;
    }
    response.resMsg.push_back(std::move(error));

    std::string response_body;
    struct evbuffer* buffer = nullptr;
    if (!cosmo::util::EncodeJson(response, response_body) || (buffer = evbuffer_new()) == nullptr ||
        !AddHttpHeader(req, "")) {
        if (buffer != nullptr) {
            evbuffer_free(buffer);
        }
        evhttp_send_error(req, code_value, message);
        return;
    }
    evbuffer_add(buffer, response_body.data(), response_body.size());
    evhttp_send_reply(req, code_value, message, buffer);
    evbuffer_free(buffer);
}

int HttpServer::DispatchJsonMsg(HttpAckTask* task) {
    struct evhttp_request* ev_http_req = FindRequest(task->request_token);
    if (ev_http_req == nullptr) {
        LOG_WARN("Drop late HTTP response for token {}", task->request_token);
        return 0;
    }

    if (!AddHttpHeader(ev_http_req, task->request_id)) {
        return 0;
    }

    // NOTE: Cannot reuse a static evbuffer. Otherwise each evbuffer_add accumulates,
    // causing memory growth in high-frequency response scenarios like chunked uploads.
    struct evbuffer* evRes = evbuffer_new();
    if (!evRes) {
        LOG_ERRO("{}", "evbuffer_new failed");
        return 0;
    }
    if (task->response.empty()) {
        LOG_ERRO("{}", "task->response is empty");
    }
    evbuffer_add(evRes, task->response.c_str(), task->response.size());

    auto& codeMsg   = GetHttpResCodeMsg();
    auto it         = codeMsg.find(task->http_ack_code);
    const char* msg = (it != codeMsg.end()) ? it->second.c_str() : "UNKNOWN";
    evhttp_send_reply(ev_http_req, task->http_ack_code, msg, evRes);
    evbuffer_free(evRes);
    return 1;
}

int HttpServer::DispatchFileMsg(HttpAckTask* task) {
    struct evhttp_request* ev_http_req = FindRequest(task->request_token);
    if (ev_http_req == nullptr) {
        LOG_WARN("Drop late HTTP file response for token {}", task->request_token);
        return 0;
    }

    const int file_fd = open(task->file_path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    struct stat file_status {};
    if (file_fd < 0 || fstat(file_fd, &file_status) != 0 || !S_ISREG(file_status.st_mode) ||
        file_status.st_size < 0 ||
        (task->delete_file_after_send && (file_status.st_uid != geteuid() || file_status.st_nlink != 1))) {
        if (file_fd >= 0) {
            close(file_fd);
        }
        struct evbuffer* buf = evbuffer_new();
        evbuffer_add_printf(buf, "File not found: %s", task->file_name.c_str());
        evhttp_send_reply(ev_http_req, HTTP_NOTFOUND, "File Not Found", buf);
        evbuffer_free(buf);
        LOG_ERRO("File Path:{} Name:{} Not Found", task->file_path, task->file_name);
        return 0;
    }

    const auto file_size = static_cast<std::uint64_t>(file_status.st_size);
    ByteRange range;
    const auto range_result = ParseRangeHeader(task->range_request, file_size, range);
    if (range_result == RangeResult::kInvalid) {
        close(file_fd);
        auto* headers = evhttp_request_get_output_headers(ev_http_req);
        if (headers != nullptr) {
            AddCommonHeaders(headers, task->request_id);
            evhttp_add_header(headers, "Accept-Ranges", "bytes");
            const auto content_range = "bytes */" + std::to_string(file_size);
            evhttp_add_header(headers, "Content-Range", content_range.c_str());
            evhttp_add_header(headers, "Content-Length", "0");
        }
        struct evbuffer* empty = evbuffer_new();
        evhttp_send_reply(ev_http_req, 416, "Range Not Satisfiable", empty);
        evbuffer_free(empty);
        return 1;
    }

    const auto content_length = file_size == 0 ? 0 : range.last - range.first + 1;
    if (!AddHttpOctetHeader(ev_http_req, task->request_id, task->file_name, content_length)) {
        close(file_fd);
        return 0;
    }

    auto* headers = evhttp_request_get_output_headers(ev_http_req);
    evhttp_add_header(headers, "Accept-Ranges", "bytes");
    if (range_result == RangeResult::kPartial) {
        const auto content_range = "bytes " + std::to_string(range.first) + "-" + std::to_string(range.last) +
                                   "/" + std::to_string(file_size);
        evhttp_add_header(headers, "Content-Range", content_range.c_str());
    }

    if (task->delete_file_after_send) {
        struct stat named_status {};
        if (lstat(task->file_path.c_str(), &named_status) != 0 || named_status.st_dev != file_status.st_dev ||
            named_status.st_ino != file_status.st_ino || unlink(task->file_path.c_str()) != 0) {
            LOG_WARN("Could not unlink managed temporary download before streaming: {}", task->file_path);
        } else {
            task->delete_file_after_send = false;
        }
    }

    const int response_code   = range_result == RangeResult::kPartial ? 206 : HTTP_OK;
    const char* response_text = range_result == RangeResult::kPartial ? "Partial Content" : "OK";
    evhttp_send_reply_start(ev_http_req, response_code, response_text);

    std::uint64_t position  = range.first;
    std::uint64_t remaining = content_length;
    char buf[64 * 1024];
    while (remaining > 0) {
        const auto requested = static_cast<std::size_t>(std::min<std::uint64_t>(remaining, sizeof(buf)));
        const auto nread     = pread(file_fd, buf, requested, static_cast<off_t>(position));
        if (nread < 0 && errno == EINTR) {
            continue;
        }
        if (nread <= 0) {
            LOG_ERRO("Failed while streaming file {}", task->file_path);
            break;
        }
        struct evbuffer* chunk = evbuffer_new();
        if (chunk == nullptr) {
            LOG_ERRO("{}", "Cannot allocate HTTP download chunk");
            break;
        }
        evbuffer_add(chunk, buf, static_cast<std::size_t>(nread));
        evhttp_send_reply_chunk(ev_http_req, chunk);
        evbuffer_free(chunk);
        position += static_cast<std::uint64_t>(nread);
        remaining -= static_cast<std::uint64_t>(nread);
    }
    evhttp_send_reply_end(ev_http_req);
    close(file_fd);
    return 1;
}

void HttpServer::DispatchMsg() {
    {
        std::lock_guard<std::mutex> lock(lifecycle_mutex_);
        if (lifecycle_state_ != LifecycleState::kInitialized || event_base_ == nullptr) {
            LOG_ERRO("{}", "DispatchMsg: HTTP server is not initialized");
            return;
        }
        lifecycle_state_ = LifecycleState::kDispatching;
        event_thread_id_ = std::this_thread::get_id();
    }

    while (is_running_) {
        // Block once: woken by network events or 5ms tick
        int rc = event_base_loop(event_base_, EVLOOP_ONCE);
        if (rc < 0) {
            LOG_ERRO("{}", "event_base_loop failed");
            break;
        }

        int handle_cnt = HandleRespMsgList();
        if (handle_cnt < 0) {
            LOG_WARN("{}", "QUIT");
            is_running_ = false;
            break;
        }
    }
    is_running_ = false;

    {
        std::lock_guard<std::mutex> lock(lifecycle_mutex_);
        lifecycle_state_ = LifecycleState::kStopping;
    }
    ShutdownOnEventThread();
    MarkStopped();
}

int HttpServer::Put(cosmo::MsgEnvelope&& msg) {
    std::lock_guard<std::mutex> lck(msg_mutex_);
    msg_list_.push_back(std::move(msg));
    return static_cast<int>(msg_list_.size());
}

int HttpServer::HandleRespMsgList() {
    cosmo::MsgEnvelopeList lst_msg;
    {
        std::lock_guard<std::mutex> lck(msg_mutex_);
        if (msg_list_.empty()) {
            return 0;
        }
        lst_msg.swap(msg_list_);
    }

    int handle_cnt = 0;

    for (auto& it : lst_msg) {
        LOG_INFO("Handle:{}", it.GetMsgId());
        auto acl_task = it.ReleaseData();

        auto task = static_cast<HttpAckTask*>(acl_task.get());
        if (task != nullptr) {
            auto msgId = static_cast<InnerMsgId>(it.GetMsgId());
            switch (msgId) {
                case InnerMsgId::kHttpOctetAck:
                    handle_cnt += DispatchFileMsg(task);
                    break;
                case InnerMsgId::kHttpQuitAck:
                    return -1;
                default:
                    handle_cnt += DispatchJsonMsg(task);
                    break;
            }
        } else {
            LOG_ERRO("{}", "task is nullptr!");
            return 0;
        }
    }

    return handle_cnt;
}

bool HttpServer::AddCommonHeaders(struct evkeyvalq* ev_header, const std::string& req_id) {
    evhttp_add_header(ev_header, "Server", "CWAI");
    evhttp_add_header(ev_header, "RequestId", req_id.c_str());
    evhttp_add_header(ev_header, "AppKey", app_key_.c_str());

    std::string Nonce = cosmo::util::GenerateUUID();
    evhttp_add_header(ev_header, "Nonce", Nonce.c_str());

    auto now      = std::chrono::system_clock::now().time_since_epoch();
    auto cur_time = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();

    char cur_time_buf[32] = {0};
    snprintf(cur_time_buf, sizeof(cur_time_buf), "%013ld", cur_time);
    evhttp_add_header(ev_header, "CurTime", cur_time_buf);

    std::string sha_str   = Nonce + app_secret_ + cur_time_buf;
    std::string check_sum = cosmo::util::Sha1(sha_str);
    evhttp_add_header(ev_header, "CheckSum", check_sum.c_str());
    return true;
}

bool HttpServer::AddHttpHeader(struct evhttp_request* ev_http_req, const std::string& req_id) {
    auto* ev_header = evhttp_request_get_output_headers(ev_http_req);
    if (ev_header == nullptr) {
        LOG_ERRO("{}", "ev_header is nullptr!");
        return false;
    }

    if (!AddCommonHeaders(ev_header, req_id)) {
        return false;
    }

    evhttp_add_header(ev_header, "Content-Type", "application/json");
    evhttp_add_header(ev_header, "Connection", "close");
    return true;
}

bool HttpServer::AddHttpOctetHeader(struct evhttp_request* ev_http_req, const std::string& req_id,
                                    const std::string& file_name, std::uint64_t content_length_value) {
    auto* ev_header = evhttp_request_get_output_headers(ev_http_req);
    if (ev_header == nullptr) {
        LOG_ERRO("{}", "ev_header is nullptr!");
        return false;
    }

    if (!AddCommonHeaders(ev_header, req_id)) {
        return false;
    }

    evhttp_add_header(ev_header, "Content-Type", "application/octet-stream");
    std::string content_length = std::to_string(content_length_value);
    evhttp_add_header(ev_header, "Content-Length", content_length.c_str());
    std::string safe_file_name = file_name;
    std::replace(safe_file_name.begin(), safe_file_name.end(), '"', '_');
    std::replace(safe_file_name.begin(), safe_file_name.end(), '\\', '_');
    std::string attachmentFile = "attachment; filename=\"" + safe_file_name + "\"";
    evhttp_add_header(ev_header, "Content-Disposition", attachmentFile.c_str());
    return true;
}

}  // namespace cosmo::network::http
