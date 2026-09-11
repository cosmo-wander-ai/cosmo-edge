#include "service/gb28181/impl/Gb28181ManagementImpl.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <deque>
#include <future>
#include <map>
#include <mutex>
#include <set>
#include <thread>

#include "service/detail/ServiceRegistry.h"
#include "service/gb28181/impl/GbConfigStore.h"
#include "service/gb28181/impl/GbSipProtocol.h"
#include "service/network/IHttpClient.h"
#include "util/Log.h"
#include "util/PathUtil.h"

namespace cosmo::service {
namespace {
    using Json  = nlohmann::json;
    using Clock = std::chrono::steady_clock;
    using Time  = Clock::time_point;
    using namespace std::chrono_literals;
    constexpr const char* kMediaApi = "http://127.0.0.1:1985/gb/v1/publish/";
    std::string Field(const Json& input, const char* key, const std::string& fallback = "",
                      size_t limit = 256) {
        const auto value = input.value(key, fallback);
        if (value.size() > limit || value.find_first_of("\r\n") != std::string::npos ||
            value.find('\0') != std::string::npos)
            throw std::runtime_error("invalid_parameter");
        return value;
    }
    int Listener(int port) {
        const int fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
        if (fd < 0)
            throw std::runtime_error("listen_failed");
        int reuse = 1;
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
        sockaddr_in address{};
        address.sin_family      = AF_INET;
        address.sin_port        = htons(port);
        address.sin_addr.s_addr = INADDR_ANY;
        if (bind(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) || listen(fd, 32)) {
            close(fd);
            throw std::runtime_error("listen_failed");
        }
        return fd;
    }
    std::string Ip(const sockaddr_in& address) {
        char text[INET_ADDRSTRLEN]{};
        inet_ntop(AF_INET, &address.sin_addr, text, sizeof(text));
        return text;
    }
    int Expires(const gb::Message& message) {
        std::string value  = message.Header("expires");
        const auto contact = message.Header("contact");
        auto position      = contact.find(";expires=");
        if (position != std::string::npos) {
            value = contact.substr(position + 9);
            value = value.substr(0, value.find_first_of("; >,"));
        }
        if (value.empty())
            return 3600;
        if (value.size() > 6 ||
            !std::all_of(value.begin(), value.end(), [](char c) { return c >= '0' && c <= '9'; }))
            throw std::runtime_error("invalid_sip");
        return std::min(std::stoi(value), 86400);
    }
    std::string CseqMethod(const gb::Message& message) {
        const auto cseq  = message.Header("cseq");
        const auto space = cseq.find(' ');
        return space == std::string::npos ? "" : cseq.substr(space + 1);
    }
}  // namespace

struct Gb28181ManagementImpl::Impl {
    struct Connection {
        std::string peer, local, input, output, device, nonce;
        int port{0}, failures{0};
        Time seen{Clock::now()}, nonceUntil{};
    };
    struct Device {
        int fd{-1}, catalogTotal{-1};
        Time expires{}, heartbeat{}, catalogDeadline{};
        std::string state{"unregistered"}, error, ip, sn;
        std::map<std::string, gb::Channel> channels, pending;
    };
    struct Stream {
        std::string device, id, ssrc, call, from, to, uri, branch, cseq, ack, state{"idle"}, error;
        Time retry{}, started{}, wanted{}, lastMedia{};
        bool allocated{false}, established{false};
    };
    struct Command {
        Json input;
        std::promise<Json> result;
    };
    // Only the worker owns sockets, device state and configuration. API threads
    // exchange bounded commands/snapshots, never holding a mutex during I/O.
    std::mutex mailbox_mtx_;
    std::deque<std::shared_ptr<Command>> commands_;
    std::set<std::string> demand_;
    Json snapshot_{{"available", false}};
    std::atomic<bool> running_{false};
    std::thread worker_;
    std::unique_ptr<gb::ConfigStore> store_;
    IHttpClient* http_{nullptr};  // Registered before this service; outlives its joined worker.
    Json config_{{"enabled", false},         {"platformId", "34020000002000000001"},
                 {"realm", "3402000000"},    {"address", ""},
                 {"sipPort", 5060},          {"heartbeatTimeout", 180},
                 {"devices", Json::object()}};
    std::map<int, Connection> connections_;
    std::map<std::string, Device> devices_;
    std::map<std::string, Stream> streams_;
    std::deque<Json> releases_;
    int listener_{-1}, sequence_{1}, mediaPort_{9001};
    Time nextMediaCheck_{};
    std::string serviceError_;

    ~Impl() {
        running_ = false;
        if (worker_.joinable())
            worker_.join();
    }
    Json Media(const Json& input) {
        const auto response = http_->Post(kMediaApi, input.dump(), "application/json", 1, 1);
        const auto result   = Json::parse(response.body, nullptr, false);
        if (response.statusCode != 200 || !result.is_object() || result.value("code", -1) != 0)
            throw std::runtime_error("media_unavailable");
        return result;
    }
    std::string Address(int fd) const {
        const auto configured = config_.value("address", "");
        return configured.empty() ? connections_.at(fd).local : configured;
    }
    bool Online(const Device& device) const {
        return device.fd >= 0 && connections_.count(device.fd) && Clock::now() < device.expires &&
               Clock::now() - device.heartbeat < std::chrono::seconds(config_.value("heartbeatTimeout", 180));
    }
    void Queue(int fd, const std::string& message) {
        if (!connections_.count(fd))
            return;
        auto& output = connections_.at(fd).output;
        if (output.size() + message.size() > 1024 * 1024)
            throw std::runtime_error("busy");
        output += message;
    }
    std::string Request(int fd, const std::string& method, const std::string& uri, const std::string& from,
                        const std::string& to, const std::string& call, const std::string& cseq,
                        const std::string& branch, const std::string& body = "",
                        const std::string& content = "", const std::string& extra = "") {
        const auto address = Address(fd) + ":" + std::to_string(config_.value("sipPort", 5060));
        return method + " " + uri + " SIP/2.0\r\nVia: SIP/2.0/TCP " + address + ";branch=" + branch +
               ";rport\r\nFrom: " + from + "\r\nTo: " + to + "\r\nCall-ID: " + call + "\r\nCSeq: " + cseq +
               " " + method + "\r\nContact: <sip:" + config_.value("platformId", "") + "@" + address +
               ";transport=tcp>\r\nMax-Forwards: 70\r\nUser-Agent: CosmoEdge\r\n" + extra +
               (content.empty() ? "" : "Content-Type: " + content + "\r\n") +
               "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
    }
    std::string RemoteUri(int fd, const std::string& id) const {
        const auto& connection = connections_.at(fd);
        return "sip:" + id + "@" + connection.peer + ":" + std::to_string(connection.port);
    }
    std::string From() const {
        return "<sip:" + config_.value("platformId", "") + "@" + config_.value("realm", "") +
               ">;tag=" + gb::RandomHex(8);
    }
    std::string To(const std::string& id) const {
        return "<sip:" + id + "@" + config_.value("realm", "") + ">";
    }
    std::string Sequence() {
        if (++sequence_ >= 1000000000)
            sequence_ = 1;
        return std::to_string(sequence_);
    }
    void Catalog(const std::string& id) {
        auto& device = devices_.at(id);
        if (!Online(device))
            throw std::runtime_error("device_offline");
        if (!device.sn.empty() && Clock::now() < device.catalogDeadline)
            throw std::runtime_error("busy");
        device.sn = Sequence();
        device.pending.clear();
        device.catalogTotal    = -1;
        device.catalogDeadline = Clock::now() + 15s;
        device.state           = "catalog_query";
        device.error.clear();
        Queue(device.fd, Request(device.fd, "MESSAGE", RemoteUri(device.fd, id), From(), To(id),
                                 gb::RandomHex(), device.sn, "z9hG4bK" + gb::RandomHex(8),
                                 gb::CatalogQuery(id, device.sn), "Application/MANSCDP+xml"));
    }
    void StopStream(Stream& stream, bool sendBye = true) {
        const auto device = devices_.find(stream.device);
        if (sendBye && device != devices_.end() && connections_.count(device->second.fd) &&
            !stream.call.empty()) {
            auto fd = device->second.fd;
            if (stream.established)
                Queue(fd, Request(fd, "BYE", stream.uri, stream.from, stream.to, stream.call, Sequence(),
                                  "z9hG4bK" + gb::RandomHex(8)));
            else if (stream.allocated)
                Queue(fd, Request(fd, "CANCEL", stream.uri, stream.from, stream.to, stream.call, stream.cseq,
                                  stream.branch));
        }
        if (stream.allocated) {
            // Release in the event loop, one request at a time. A failed media
            // server must not multiply config-save/shutdown latency by channel count.
            if (releases_.size() < 512)
                releases_.push_back({{"action", "release"}, {"id", stream.id}, {"ssrc", stream.ssrc}});
        }
        stream.allocated   = false;
        stream.established = false;
        stream.ack.clear();
        stream.call.clear();
        stream.retry = Clock::now() + 10s;
    }
    void Drop(int fd) {
        auto found = connections_.find(fd);
        if (found == connections_.end())
            return;
        const auto id = found->second.device;
        close(fd);
        connections_.erase(found);
        if (!id.empty() && devices_.count(id) && devices_.at(id).fd == fd) {
            auto& device = devices_.at(id);
            device.fd    = -1;
            device.state = "offline";
            device.error = "connection_closed";
            device.sn.clear();
            device.pending.clear();
            for (auto& item : streams_)
                if (item.second.device == id) {
                    StopStream(item.second, false);
                    item.second.state = "device_offline";
                }
        }
    }
    void Register(int fd, const gb::Message& message) {
        const auto id    = gb::User(message.Header("from"));
        auto& connection = connections_.at(fd);
        if (!gb::IsId(id) || gb::User(message.uri) != config_.value("platformId", "") ||
            gb::User(message.Header("to")) != id || !config_["devices"].contains(id) ||
            (!connection.device.empty() && connection.device != id)) {
            Queue(fd, gb::Response(message, 403));
            ++connection.failures;
            return;
        }
        auto& device            = devices_[id];
        const auto& credentials = config_["devices"][id];
        if (!credentials.value("allowUnauthenticated", false)) {
            if (Clock::now() >= connection.nonceUntil)
                connection.nonce.clear();
            if (!gb::VerifyDigest(message, credentials.value("username", id), config_.value("realm", ""),
                                  credentials.value("ha1", ""), connection.nonce)) {
                if (!message.Header("authorization").empty()) {
                    ++connection.failures;
                    if (!Online(device)) {
                        device.state = "auth_failed";
                        device.error = "unauthorized";
                    }
                }
                connection.nonce      = gb::RandomHex();
                connection.nonceUntil = Clock::now() + 60s;
                Queue(fd, gb::Response(message, 401,
                                       "WWW-Authenticate: Digest realm=\"" + config_.value("realm", "") +
                                           "\", nonce=\"" + connection.nonce +
                                           "\", algorithm=MD5, qop=\"auth\"\r\n"));
                return;
            }
            connection.nonce.clear();  // Single-use server nonce: rejects both qop and legacy digest replay.
        }
        const int expires = Expires(message);
        if (device.fd >= 0 && device.fd != fd && Online(device)) {
            Queue(fd, gb::Response(message, 403));
            ++connection.failures;
            return;  // No takeover of a live identity.
        }
        const bool newRegistration = !Online(device) || device.fd != fd;
        if (device.fd >= 0 && device.fd != fd)
            Drop(device.fd);
        connection.device   = id;
        connection.failures = 0;
        device.fd           = fd;
        device.ip           = connection.peer;
        device.heartbeat    = Clock::now();
        device.expires      = Clock::now() + std::chrono::seconds(expires);
        Queue(fd, gb::Response(message, 200,
                               "Contact: " + message.Header("contact") +
                                   "\r\nExpires: " + std::to_string(expires) + "\r\n"));
        if (expires == 0) {
            device.state = "unregistered";
            device.fd    = -1;
            connection.device.clear();
            for (auto& item : streams_)
                if (item.second.device == id) {
                    StopStream(item.second, false);
                    item.second.state = "device_offline";
                }
            return;
        }
        if (newRegistration) {
            device.state = "registered";
            device.error.clear();
            device.sn.clear();
            Catalog(id);
        }
    }
    void Incoming(int fd, const gb::Message& message) {
        if (message.method == "REGISTER") {
            if (CseqMethod(message) != "REGISTER")
                throw std::runtime_error("invalid_sip");
            Register(fd, message);
            return;
        }
        auto& connection = connections_.at(fd);
        if (connection.device.empty() || !Online(devices_.at(connection.device))) {
            if (!message.status)
                Queue(fd, gb::Response(message, 403));
            return;
        }
        auto& device = devices_.at(connection.device);
        if (message.status) {
            for (auto& item : streams_) {
                auto& stream = item.second;
                if (stream.device != connection.device || stream.call != message.Header("call-id") ||
                    message.Header("cseq") != stream.cseq + " INVITE" ||
                    message.Header("from") != stream.from ||
                    message.Header("via").find("branch=" + stream.branch) == std::string::npos)
                    continue;
                if (message.status < 200)
                    return;
                if (!stream.ack.empty() && message.status < 300) {
                    Queue(fd, stream.ack);
                    return;
                }
                if (message.status >= 300) {
                    Queue(fd, Request(fd, "ACK", stream.uri, stream.from, message.Header("to"), stream.call,
                                      stream.cseq, stream.branch));
                    stream.error = "invite_rejected_" + std::to_string(message.status);
                    StopStream(stream, false);
                    stream.state = "invite_failed";
                    return;
                }
                stream.to          = message.Header("to");
                const auto contact = gb::Uri(message.Header("contact"));
                if (!contact.empty())
                    stream.uri = contact;
                stream.ack = Request(fd, "ACK", stream.uri, stream.from, stream.to, stream.call, stream.cseq,
                                     "z9hG4bK" + gb::RandomHex(8));
                Queue(fd, stream.ack);
                stream.established = true;
                if (!gb::AcceptsOffer(message.body) || !message.Header("record-route").empty()) {
                    stream.error = "unsupported_transport";
                    StopStream(stream);
                    stream.state = "unsupported_transport";
                    return;
                }
                stream.state = "waiting_media";
                return;
            }
            return;
        }
        if (CseqMethod(message) != message.method)
            throw std::runtime_error("invalid_sip");
        if (message.method == "MESSAGE") {
            const auto xml = gb::ParseXml(message.body);
            if (xml.device != connection.device || gb::User(message.Header("from")) != connection.device) {
                Queue(fd, gb::Response(message, 403));
                return;
            }
            Queue(fd, gb::Response(message, 200));
            if (xml.command == "Keepalive") {
                device.heartbeat = Clock::now();
                return;
            }
            if (xml.command != "Catalog" || xml.sn != device.sn || Clock::now() >= device.catalogDeadline)
                return;
            if (device.catalogTotal >= 0 && device.catalogTotal != xml.total) {
                device.error = "catalog_invalid";
                return;
            }
            device.catalogTotal = xml.total;
            for (const auto& channel : xml.channels)
                device.pending[channel.id] = channel;
            if (device.pending.size() > static_cast<size_t>(xml.total) || device.pending.size() > 1024) {
                device.error = "catalog_invalid";
                return;
            }
            if (device.pending.size() == static_cast<size_t>(xml.total)) {
                device.channels = std::move(device.pending);
                device.pending.clear();
                device.sn.clear();
                device.state = "registered";
                device.error.clear();
                for (const auto& channel : device.channels)
                    if (streams_.count(channel.first) && !streams_.at(channel.first).allocated)
                        streams_.at(channel.first).retry = Time{};
            }
        } else if (message.method == "BYE") {
            for (auto& item : streams_) {
                auto& stream = item.second;
                if (stream.device == connection.device && stream.call == message.Header("call-id") &&
                    stream.established && message.Header("from") == stream.to &&
                    message.Header("to") == stream.from) {
                    Queue(fd, gb::Response(message, 200));
                    StopStream(stream, false);
                    stream.state = "device_stopped";
                    return;
                }
            }
            Queue(fd, gb::Response(message, 481));
        } else if (message.method == "OPTIONS")
            Queue(fd, gb::Response(message, 200));
        else if (message.method != "ACK")
            Queue(fd, gb::Response(message, 405));
    }
    std::string Owner(const std::string& channel) const {
        std::string owner;
        for (const auto& item : devices_)
            if (item.second.channels.count(channel)) {
                if (!owner.empty())
                    throw std::runtime_error("duplicate_channel_id");
                owner = item.first;
            }
        return owner;
    }
    void Invite(Stream& stream) {
        const auto owner = Owner(stream.id);
        if (owner.empty()) {
            stream.state = "channel_not_found";
            return;
        }
        auto& device = devices_.at(owner);
        if (!Online(device)) {
            stream.state = "device_offline";
            return;
        }
        const auto& channel = device.channels.at(stream.id);
        if (channel.status == "OFF") {
            stream.state = "channel_offline";
            return;
        }
        stream.device = owner;
        // Decimal 10-digit live SSRC (leading 0), randomized; SRS rejects collisions.
        stream.ssrc = "0" + config_.value("realm", "3402000000").substr(3, 5) +
                      std::to_string(1000 + (std::stoul(gb::RandomHex(4), nullptr, 16) % 9000));
        const auto allocation = Media({{"id", stream.id}, {"ssrc", stream.ssrc}});
        const auto port       = allocation.value("port", 0);
        if (!allocation.value("is_tcp", false) || port < 1 || port > 65535 ||
            !allocation.value("managed", false))
            throw std::runtime_error("media_unavailable");
        stream.allocated = true;
        mediaPort_       = port;
        stream.call      = gb::RandomHex();
        stream.from      = From();
        stream.to        = To(stream.id);
        stream.cseq      = Sequence();
        stream.branch    = "z9hG4bK" + gb::RandomHex(8);
        stream.uri       = RemoteUri(device.fd, stream.id);
        stream.started   = Clock::now();
        stream.lastMedia = stream.started;
        stream.state     = "inviting";
        stream.error.clear();
        Queue(device.fd,
              Request(device.fd, "INVITE", stream.uri, stream.from, stream.to, stream.call, stream.cseq,
                      stream.branch,
                      gb::Offer(config_.value("platformId", ""), Address(device.fd), port, stream.ssrc),
                      "application/sdp",
                      "Subject: " + stream.id + ":" + stream.ssrc + "," + config_.value("platformId", "") +
                          ":0\r\n"));
    }
    void PublishSnapshot() {
        Json snapshot         = config_;
        snapshot["available"] = true;
        snapshot["listening"] = listener_ >= 0;
        snapshot["error"]     = serviceError_;
        snapshot["mediaPort"] = mediaPort_;
        snapshot["devices"]   = Json::array();
        for (const auto& item : config_["devices"].items()) {
            Json row = item.value();
            row.erase("ha1");
            row["hasPassword"] = !item.value().value("ha1", "").empty();
            row["id"]          = item.key();
            const auto& device = devices_[item.key()];
            row["online"]      = Online(device);
            row["state"]       = device.state;
            row["error"]       = device.error;
            row["ip"]          = device.ip;
            row["channels"]    = Json::array();
            for (const auto& channel : device.channels) {
                Json c{{"id", channel.first},
                       {"name", channel.second.name},
                       {"status", channel.second.status},
                       {"state", "idle"},
                       {"error", ""}};
                if (streams_.count(channel.first)) {
                    c["state"] = streams_.at(channel.first).state;
                    c["error"] = streams_.at(channel.first).error;
                }
                try {
                    Owner(channel.first);
                } catch (...) {
                    c["error"] = "duplicate_channel_id";
                }
                row["channels"].push_back(c);
            }
            snapshot["devices"].push_back(row);
        }
        std::lock_guard<std::mutex> lock(mailbox_mtx_);
        snapshot_ = std::move(snapshot);
    }
    Json Apply(const Json& request) {
        const auto action = Field(request, "action");
        if (action == "savePlatform") {
            auto candidate          = config_;
            candidate["platformId"] = Field(request, "platformId", config_["platformId"]);
            candidate["realm"]      = Field(request, "realm", config_["realm"]);
            candidate["address"]    = Field(request, "address", config_["address"]);
            candidate["enabled"]    = request.value("enabled", config_.value("enabled", false));
            candidate["sipPort"]    = request.value("sipPort", config_.value("sipPort", 5060));
            candidate["heartbeatTimeout"] =
                request.value("heartbeatTimeout", config_.value("heartbeatTimeout", 180));
            const auto realm   = candidate["realm"].get<std::string>();
            const auto address = candidate["address"].get<std::string>();
            const int port = candidate["sipPort"], heartbeat = candidate["heartbeatTimeout"];
            if (!gb::IsId(candidate["platformId"]) || realm.size() != 10 ||
                !std::all_of(realm.begin(), realm.end(), [](char c) { return c >= '0' && c <= '9'; }) ||
                (!address.empty() && !gb::IsIpv4(address)) || port < 1024 || port > 65535 ||
                std::set<int>{1936, 1985, 8000, 9000, 9001, 18088}.count(port) || heartbeat < 15 ||
                heartbeat > 3600)
                throw std::runtime_error("invalid_parameter");
            if (candidate["realm"] != config_["realm"] && !config_["devices"].empty())
                throw std::runtime_error("realm_in_use");
            const bool reopen = candidate["enabled"] != config_["enabled"] ||
                                candidate["sipPort"] != config_["sipPort"] || listener_ < 0;
            int nextListener = listener_;
            if (reopen)
                nextListener = candidate.value("enabled", false) ? Listener(port) : -1;
            try {
                store_->Save(candidate);
            } catch (...) {
                if (reopen && nextListener >= 0)
                    close(nextListener);
                throw;
            }
            // Address/identity changes also invalidate existing dialogs.
            while (!connections_.empty())
                Drop(connections_.begin()->first);
            if (reopen) {
                if (listener_ >= 0)
                    close(listener_);
                listener_ = nextListener;
            }
            config_ = std::move(candidate);
            serviceError_.clear();
        } else if (action == "saveDevice") {
            const auto id = Field(request, "id");
            if (!gb::IsId(id))
                throw std::runtime_error("invalid_parameter");
            auto candidate = config_;
            Json entry     = candidate["devices"].value(
                id, Json{{"username", id}, {"ha1", ""}, {"allowUnauthenticated", false}});
            const auto username = Field(request, "username", entry.value("username", id));
            if (username.empty() ||
                username.find_first_not_of(
                    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-@") !=
                    std::string::npos)
                throw std::runtime_error("invalid_parameter");
            if (username != entry.value("username", id))
                entry["ha1"] = "";
            entry["username"]             = username;
            entry["allowUnauthenticated"] = request.value("allowUnauthenticated", false);
            const auto password           = Field(request, "password", "", 256);
            if (!password.empty())
                entry["ha1"] = gb::Md5(username + ":" + config_.value("realm", "") + ":" + password);
            if (!entry.value("allowUnauthenticated", false) && entry.value("ha1", "").empty())
                throw std::runtime_error("password_required");
            if (!candidate["devices"].contains(id) && candidate["devices"].size() >= 128)
                throw std::runtime_error("device_limit");
            candidate["devices"][id] = entry;
            store_->Save(candidate);
            config_      = std::move(candidate);
            auto& device = devices_[id];
            if (device.fd >= 0)
                Drop(device.fd);
            device.state = "unregistered";
            device.error.clear();
            device.channels.clear();
        } else if (action == "removeDevice") {
            const auto id  = Field(request, "id");
            auto candidate = config_;
            candidate["devices"].erase(id);
            store_->Save(candidate);
            config_ = std::move(candidate);
            if (devices_.count(id)) {
                if (devices_.at(id).fd >= 0)
                    Drop(devices_.at(id).fd);
                devices_.erase(id);
            }
        } else if (action == "catalog")
            Catalog(Field(request, "id"));
        else if (action == "validateChannel") {
            const auto id    = Field(request, "channelId");
            const auto owner = Owner(id);
            if (owner.empty() || owner != Field(request, "deviceId"))
                throw std::runtime_error("channel_not_found");
            if (!Online(devices_.at(owner)))
                throw std::runtime_error("device_offline");
            return {{"name", devices_.at(owner).channels.at(id).name}};
        } else
            throw std::runtime_error("invalid_parameter");
        PublishSnapshot();
        return {{"saved", true}};
    }
    void Tick() {
        std::deque<std::shared_ptr<Command>> commands;
        std::set<std::string> demand;
        {
            std::lock_guard<std::mutex> lock(mailbox_mtx_);
            commands.swap(commands_);
            demand.swap(demand_);
        }
        for (const auto& command : commands) {
            try {
                command->result.set_value(Apply(command->input));
            } catch (...) {
                command->result.set_exception(std::current_exception());
            }
        }
        const auto now = Clock::now();
        for (const auto& id : demand) {
            if (streams_.size() < 256 || streams_.count(id)) {
                auto& stream  = streams_[id];
                stream.id     = id;
                stream.wanted = now;
            }
        }
        for (auto& item : devices_) {
            auto& device = item.second;
            if (device.fd >= 0 && !Online(device)) {
                const auto fd = device.fd;
                Drop(fd);
                device.error = "registration_or_heartbeat_timeout";
            }
            if (!device.sn.empty() && now >= device.catalogDeadline) {
                device.sn.clear();
                device.pending.clear();
                device.state = "catalog_failed";
                device.error = "catalog_timeout";
            }
        }
        // At most one new media allocation per tick; a stalled SRS cannot fan out blocking work.
        bool attempted = false;
        for (auto it = streams_.begin(); it != streams_.end();) {
            auto& stream = it->second;
            if (stream.allocated) {
                try {
                    if (Owner(stream.id) != stream.device)
                        throw std::runtime_error("channel_not_found");
                } catch (const std::exception& error) {
                    stream.error = error.what();
                    StopStream(stream);
                    stream.state = "stream_failed";
                }
            }
            if (now - stream.wanted > 120s) {
                StopStream(stream);
                it = streams_.erase(it);
                continue;
            }
            if (stream.allocated && ((stream.state == "inviting" && now - stream.started > 15s) ||
                                     (stream.established && now - stream.lastMedia > 20s))) {
                stream.error = stream.established ? "media_timeout" : "invite_timeout";
                StopStream(stream);
                stream.state = "stream_failed";
            }
            if (!stream.allocated && now >= stream.retry && !attempted && config_.value("enabled", false)) {
                attempted    = true;
                stream.retry = now + 10s;
                try {
                    Invite(stream);
                } catch (const std::exception& error) {
                    stream.error = error.what();
                    stream.state = "stream_failed";
                }
            }
            ++it;
        }
        if (!releases_.empty()) {
            auto release = std::move(releases_.front());
            releases_.pop_front();
            try {
                Media(release);
            } catch (...) { /* SRS's bounded lease is the fallback. */
            }
        }
        if (now >= nextMediaCheck_) {
            nextMediaCheck_ = now + 3s;
            try {
                const auto response =
                    http_->Get("http://127.0.0.1:1985/api/v1/streams/?start=0&count=1000", 1, 1);
                const auto body = Json::parse(response.body, nullptr, false);
                if (body.is_object() && body.value("code", -1) == 0 && body.contains("streams") &&
                    body["streams"].is_array())
                    for (const auto& item : body["streams"]) {
                        const auto id = item.value("name", "");
                        if (item.value("app", "") == "live" && streams_.count(id) &&
                            item.contains("publish") && item["publish"].value("active", false)) {
                            auto& stream = streams_.at(id);
                            if (stream.allocated && stream.established) {
                                stream.lastMedia = now;
                                stream.state     = "receiving";
                                stream.error.clear();
                            }
                        }
                    }
            } catch (...) { /* A failed observation never becomes a false success. */
            }
            PublishSnapshot();
        }
    }
    void Run() {
        while (running_) {
            try {
                Tick();
                std::vector<pollfd> polls;
                if (listener_ >= 0)
                    polls.push_back({listener_, POLLIN, 0});
                for (const auto& item : connections_)
                    polls.push_back({item.first,
                                     static_cast<short>(POLLIN | (item.second.output.empty() ? 0 : POLLOUT)),
                                     0});
                if (poll(polls.data(), polls.size(), 50) < 0)
                    continue;
                for (const auto& item : polls) {
                    if (item.fd == listener_) {
                        if (!(item.revents & POLLIN))
                            continue;
                        sockaddr_in peer{};
                        socklen_t length = sizeof(peer);
                        const int fd     = accept4(listener_, reinterpret_cast<sockaddr*>(&peer), &length,
                                                   SOCK_NONBLOCK | SOCK_CLOEXEC);
                        if (fd < 0)
                            continue;
                        if (connections_.size() >= 64) {
                            close(fd);
                            continue;
                        }
                        sockaddr_in local{};
                        length = sizeof(local);
                        getsockname(fd, reinterpret_cast<sockaddr*>(&local), &length);
                        Connection connection;
                        connection.peer  = Ip(peer);
                        connection.local = Ip(local);
                        connection.port  = ntohs(peer.sin_port);
                        connections_.emplace(fd, std::move(connection));
                        continue;
                    }
                    if (!connections_.count(item.fd))
                        continue;
                    if (item.revents & (POLLERR | POLLHUP | POLLNVAL)) {
                        Drop(item.fd);
                        continue;
                    }
                    auto& connection = connections_.at(item.fd);
                    if (connection.failures >= 5 ||
                        (connection.device.empty() && Clock::now() - connection.seen > 30s)) {
                        Drop(item.fd);
                        continue;
                    }
                    try {
                        if (item.revents & POLLIN) {
                            char buffer[16384];
                            const auto count = recv(item.fd, buffer, sizeof(buffer), 0);
                            if (count == 0 || (count < 0 && errno != EAGAIN && errno != EINTR)) {
                                Drop(item.fd);
                                continue;
                            }
                            if (count > 0) {
                                connection.input.append(buffer, count);
                                connection.seen = Clock::now();
                            }
                        }
                        for (int processed = 0; processed < 16; ++processed) {
                            gb::Message message;
                            if (!gb::Pop(connection.input, message))
                                break;
                            Incoming(item.fd, message);
                        }
                        if (item.revents & POLLOUT) {
                            const auto count = send(item.fd, connection.output.data(),
                                                    connection.output.size(), MSG_NOSIGNAL);
                            if (count > 0)
                                connection.output.erase(0, count);
                            else if (count < 0 && errno != EAGAIN && errno != EINTR) {
                                Drop(item.fd);
                                continue;
                            }
                        }
                    } catch (...) {
                        const auto id = connection.device;
                        Drop(item.fd);
                        if (!id.empty() && devices_.count(id))
                            devices_.at(id).error = "invalid_sip";
                    }
                }
            } catch (...) {
                serviceError_ = "service_error";
                PublishSnapshot();
            }
        }
        while (!connections_.empty())
            Drop(connections_.begin()->first);
        // Bound shutdown work. Remaining orphan sessions expire in SRS itself.
        for (int count = 0; count < 4 && !releases_.empty(); ++count) {
            auto release = std::move(releases_.front());
            releases_.pop_front();
            try {
                Media(release);
            } catch (...) {
                break;
            }
        }
        if (listener_ >= 0) {
            close(listener_);
            listener_ = -1;
        }
        std::deque<std::shared_ptr<Command>> pending;
        {
            std::lock_guard<std::mutex> lock(mailbox_mtx_);
            pending.swap(commands_);
        }
        for (auto& command : pending)
            command->result.set_exception(std::make_exception_ptr(std::runtime_error("service_unavailable")));
    }
};

Gb28181ManagementImpl::Gb28181ManagementImpl() : impl_(std::make_unique<Impl>()) {}
Gb28181ManagementImpl::~Gb28181ManagementImpl() = default;
void Gb28181ManagementImpl::Init() {
    auto& state       = *impl_;
    state.http_       = &ServiceRegistry::Instance().Get<IHttpClient>();
    state.store_      = std::make_unique<gb::ConfigStore>(path::GetCfgPath("gb28181"));
    const auto config = state.store_->Load();
    if (!config.empty()) {
        if (!config.is_object() || !config.contains("devices") || !config["devices"].is_object() ||
            config["devices"].size() > 128)
            throw std::runtime_error("storage_error");
        state.config_ = config;
    }
    for (const auto& item : state.config_["devices"].items())
        state.devices_.emplace(item.key(), Impl::Device{});
    if (state.config_.value("enabled", false)) {
        try {
            state.listener_ = Listener(state.config_.value("sipPort", 5060));
        } catch (...) {
            state.serviceError_ = "listen_failed";
        }
    }
    state.PublishSnapshot();
    state.running_ = true;
    state.worker_  = std::thread([&state] { state.Run(); });
}
Json Gb28181ManagementImpl::Execute(const Json& request) {
    auto& state = *impl_;
    if (!state.running_)
        throw std::runtime_error("service_unavailable");
    if (request.value("action", "list") == "list") {
        std::lock_guard<std::mutex> lock(state.mailbox_mtx_);
        return state.snapshot_;
    }
    auto command   = std::make_shared<Impl::Command>();
    command->input = request;
    auto result    = command->result.get_future();
    {
        std::lock_guard<std::mutex> lock(state.mailbox_mtx_);
        if (state.commands_.size() >= 8)
            throw std::runtime_error("busy");
        state.commands_.push_back(command);
    }
    // Do not return an ambiguous timeout while a configuration mutation can still execute.
    return result.get();
}
void Gb28181ManagementImpl::Ensure(const std::string& channelId) {
    if (!impl_->running_ || !gb::IsId(channelId))
        return;
    std::lock_guard<std::mutex> lock(impl_->mailbox_mtx_);
    if (impl_->demand_.size() < 256)
        impl_->demand_.insert(channelId);
}
}  // namespace cosmo::service
