#include "service/ai/impl/VisualDecisionProtocol.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <set>
#include <stdexcept>

#include "cryptopp/sha.h"

namespace cosmo::service::visual {
namespace {
    constexpr const char* kIdentities[] = {"request_id", "task_id", "run_epoch",
                                           "frame_id",   "roi_id",  "config_revision"};

    struct Descriptor {
        int fd{-1};
        ~Descriptor() {
            if (fd >= 0)
                close(fd);
        }
    };

    void Require(bool value, const char* reason) {
        if (!value)
            throw std::runtime_error(reason);
    }

    double Number(const Json& value) {
        Require(value.is_number(), "invalid_worker_scores");
        auto number = value.get<double>();
        Require(std::isfinite(number), "invalid_worker_scores");
        return number;
    }

    void Await(int fd, short events, Clock::time_point deadline) {
        for (;;) {
            const auto remaining =
                std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now());
            Require(remaining.count() > 0, "deadline_exceeded");
            pollfd pfd{fd, events, 0};
            const int rc = poll(&pfd, 1, static_cast<int>(std::min<int64_t>(remaining.count(), 30000)));
            if (rc < 0 && errno == EINTR)
                continue;
            Require(rc != 0, "deadline_exceeded");
            Require(rc > 0 && (pfd.revents & events), "worker_unavailable");
            return;
        }
    }

    void Transfer(int fd, void* data, size_t length, bool writing, Clock::time_point deadline) {
        auto* bytes = static_cast<uint8_t*>(data);
        while (length) {
            Await(fd, writing ? POLLOUT : POLLIN, deadline);
            ssize_t count;
            if (writing) {
#ifdef MSG_NOSIGNAL
                count = send(fd, bytes, length, MSG_NOSIGNAL);
#else
                count = send(fd, bytes, length, 0);
#endif
            } else {
                count = recv(fd, bytes, length, 0);
            }
            if (count < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK))
                continue;
            Require(count > 0, "worker_unavailable");
            length -= static_cast<size_t>(count);
            bytes += count;
        }
    }

    void ValidateScores(const Json& row, const VisualQuestionRef& question) {
        Require(row.at("business_qualified").is_boolean() && !row.at("business_qualified").get<bool>(),
                "worker_qualification_rejected");
        Require(row.at("qtype").is_number_integer() && row.at("qtype") == question.qtype &&
                    row.at("ordered_options") == question.orderedOptions &&
                    row.at("temperature") == question.temperature,
                "worker_question_semantics_mismatch");
        const auto& probabilities = row.at("probabilities");
        const auto& logits        = row.at("raw_option_logits");
        const auto& acts          = row.at("raw_action_logits");
        const size_t count        = question.orderedOptions.size();
        Require(probabilities.is_array() && probabilities.size() == count && logits.is_array() &&
                    logits.size() == count && acts.is_array() && !acts.empty() && acts.size() <= 16,
                "invalid_worker_scores");
        double sum          = 0;
        double maximumLogit = -std::numeric_limits<double>::infinity();
        std::vector<double> values;
        for (size_t i = 0; i < count; ++i) {
            const auto p = Number(probabilities[i]);
            Require(p >= 0 && p <= 1, "invalid_worker_scores");
            sum += p;
            values.push_back(p);
            maximumLogit = std::max(maximumLogit, Number(logits[i]));
        }
        Require(std::abs(sum - 1) <= 1e-5, "invalid_worker_scores");
        const auto best =
            static_cast<size_t>(std::max_element(values.begin(), values.end()) - values.begin());
        Require(row.at("top1") == question.orderedOptions[best], "invalid_worker_scores");
        auto sorted = values;
        std::sort(sorted.begin(), sorted.end());
        Require(std::abs(Number(row.at("top2_margin")) - (sorted[count - 1] - sorted[count - 2])) <= 1e-5,
                "invalid_worker_scores");
        double denom = 0;
        std::vector<double> weights;
        const double temperature = Number(question.temperature.at("value"));
        for (const auto& logit : logits) {
            weights.push_back(std::exp((Number(logit) - maximumLogit) / temperature));
            denom += weights.back();
        }
        for (size_t i = 0; i < count; ++i)
            Require(std::abs(weights[i] / denom - values[i]) <= 1e-5, "invalid_worker_scores");
        for (const auto& action : acts)
            Number(action);
        const auto& timing = row.at("decision_timing_ms");
        Require(timing.is_object() && timing.size() == 4, "invalid_worker_timing");
        for (const auto* stage : {"h2d_ms", "launch_sync_ms", "d2h_ms", "total_ms"})
            Require(Number(timing.at(stage)) >= 0, "invalid_worker_timing");
    }
}  // namespace

bool Identity(const std::string& value) {
    return !value.empty() && value.size() <= 256 &&
           std::none_of(value.begin(), value.end(), [](unsigned char c) { return c < 32; });
}

bool Sha256Identity(const std::string& value) {
    return value.size() == 64 && std::all_of(value.begin(), value.end(), [](char c) {
               return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
           });
}

std::string Sha256(const uint8_t* data, size_t size) {
    std::array<uint8_t, CryptoPP::SHA256::DIGESTSIZE> bytes{};
    CryptoPP::SHA256().CalculateDigest(bytes.data(), data, size);
    std::string result;
    for (auto byte : bytes) {
        result.push_back("0123456789abcdef"[byte >> 4]);
        result.push_back("0123456789abcdef"[byte & 15]);
    }
    return result;
}

Json Parse(const std::string& encoded) {
    Require(!encoded.empty() && encoded.size() <= kMaxJson, "invalid_json_size");
    std::vector<std::set<std::string>> objects;
    auto callback = [&](int depth, Json::parse_event_t event, Json& parsed) {
        Require(depth <= 16, "json_too_deep");
        if (event == Json::parse_event_t::object_start)
            objects.emplace_back();
        else if (event == Json::parse_event_t::object_end)
            objects.pop_back();
        else if (event == Json::parse_event_t::key)
            Require(!objects.empty() && objects.back().insert(parsed.get<std::string>()).second,
                    "duplicate_json_key");
        return true;
    };
    return Json::parse(encoded, callback);
}

bool Release::Valid() const {
    if (!Sha256Identity(manifestSha256) || !modelHashes.is_object() || modelHashes.size() != 3)
        return false;
    for (const auto* name : {"tower", "adapter", "decision"}) {
        if (!modelHashes.contains(name) || !modelHashes[name].is_string() ||
            !Sha256Identity(modelHashes[name].get<std::string>()))
            return false;
    }
    return true;
}

Release ReadRelease(const std::string& path, const std::string& expectedSha256) {
    Require(Sha256Identity(expectedSha256), "invalid_manifest_identity");
    std::ifstream file(path, std::ios::binary);
    Require(file.good(), "manifest_unavailable");
    std::string encoded(kMaxJson + 1, '\0');
    file.read(encoded.data(), static_cast<std::streamsize>(encoded.size()));
    encoded.resize(static_cast<size_t>(file.gcount()));
    Require(Sha256(reinterpret_cast<const uint8_t*>(encoded.data()), encoded.size()) == expectedSha256,
            "manifest_identity_mismatch");
    auto manifest = Parse(encoded);
    Require(manifest.at("schema") == 2 && manifest.at("qualification") == "business-acceptance-pending",
            "unsupported_manifest");
    Release release{expectedSha256, Json::object()};
    for (const auto* name : {"tower", "adapter", "decision"})
        release.modelHashes[name] = manifest.at("models").at(name).at("sha256");
    Require(release.Valid(), "invalid_model_identity");
    return release;
}

bool ValidQuestion(const VisualQuestionRef& q) {
    try {
        if (!Identity(q.itemId) || !Identity(q.questionId) || q.questionVersion <= 0 ||
            !Sha256Identity(q.compiledSha256) || (q.qtype != 0 && q.qtype != 2) ||
            q.orderedOptions.size() < 2 || q.orderedOptions.size() > 16 || !q.temperature.is_object())
            return false;
        if (q.qtype == 2 && q.orderedOptions != std::vector<std::string>{"false", "true"})
            return false;
        std::set<std::string> unique;
        for (const auto& option : q.orderedOptions)
            if (!Identity(option) || !unique.insert(option).second)
                return false;
        const double temperature = Number(q.temperature.at("value"));
        return temperature >= 0.5 && temperature <= 5 &&
               Identity(q.temperature.at("bucket").get<std::string>()) &&
               Identity(q.temperature.at("source").get<std::string>());
    } catch (...) {
        return false;
    }
}

Json ItemIdentity(const VisualQuestionRef& q) {
    return {{"item_id", q.itemId},
            {"question_id", q.questionId},
            {"question_version", q.questionVersion},
            {"compiled_sha256", q.compiledSha256}};
}

Json Failure(const Json& request, const std::string& reason) {
    Json result = {{"protocol", kProtocol},
                   {"profile", kProfile},
                   {"status", "unknown"},
                   {"reason", reason},
                   {"manifest_sha256", request.at("manifest_sha256")},
                   {"items", Json::array()}};
    for (const auto* key : kIdentities)
        result[key] = request.at(key);
    for (auto item : request.at("items")) {
        item["status"]             = "unknown";
        item["business_qualified"] = false;
        item["reason"]             = reason;
        result["items"].push_back(std::move(item));
    }
    return result;
}

Json ValidateResponse(const Json& request, const std::vector<VisualQuestionRef>& questions,
                      const Release& release, const std::string& imageSha256, const Json& response) {
    try {
        Require(response.is_object() && response.at("protocol") == kProtocol &&
                    response.at("profile") == kProfile &&
                    response.at("manifest_sha256") == release.manifestSha256,
                "worker_identity_mismatch");
        for (const auto* key : kIdentities)
            Require(response.at(key) == request.at(key), "worker_identity_mismatch");
        const auto& rows = response.at("items");
        Require(rows.is_array() && rows.size() == questions.size(), "worker_item_identity_mismatch");
        size_t completed = 0;
        for (size_t i = 0; i < rows.size(); ++i) {
            const auto& row = rows[i];
            Require(row.at("question_version").is_number_integer(), "worker_item_identity_mismatch");
            for (const auto* key : {"item_id", "question_id", "question_version", "compiled_sha256"})
                Require(row.at(key) == request.at("items")[i].at(key), "worker_item_identity_mismatch");
            Require(row.at("business_qualified").is_boolean() && !row.at("business_qualified").get<bool>(),
                    "worker_qualification_rejected");
            if (row.at("status") == "completed") {
                ValidateScores(row, questions[i]);
                ++completed;
            } else {
                Require(row.at("status") == "unknown" && row.size() == 7 &&
                            Identity(row.at("reason").get<std::string>()),
                        "invalid_worker_response");
            }
        }
        const char* status = completed == rows.size() ? "completed" : completed ? "partial" : "unknown";
        Require(response.at("status") == status, "invalid_worker_response");
        if (completed) {
            Require(response.at("model_hashes") == release.modelHashes, "model_identity_mismatch");
            Require(response.at("image_sha256") == imageSha256, "image_identity_mismatch");
        }
        return response;
    } catch (const std::runtime_error& e) {
        return Failure(request, e.what());
    } catch (...) {
        return Failure(request, "invalid_worker_response");
    }
}

int64_t MonotonicMilliseconds() {
    timespec now{};
    Require(clock_gettime(CLOCK_MONOTONIC, &now) == 0, "clock_unavailable");
    return static_cast<int64_t>(now.tv_sec) * 1000 + now.tv_nsec / 1000000;
}

Json Exchange(const std::string& path, const Json& request, const std::vector<uint8_t>& jpeg,
              Clock::time_point deadline) {
    const std::string encoded = request.dump();
    Require(!encoded.empty() && encoded.size() <= kMaxJson && !jpeg.empty() && jpeg.size() <= kMaxImage &&
                !path.empty() && path.front() == '/' && path.size() < sizeof(sockaddr_un::sun_path) &&
                path.find('\0') == std::string::npos,
            "invalid_request_size");
    Descriptor socketFd{socket(AF_UNIX, SOCK_STREAM, 0)};
    Require(socketFd.fd >= 0, "worker_unavailable");
    Require(fcntl(socketFd.fd, F_SETFL, O_NONBLOCK) == 0 && fcntl(socketFd.fd, F_SETFD, FD_CLOEXEC) == 0,
            "worker_unavailable");
#ifdef SO_NOSIGPIPE
    int noSigPipe = 1;
    setsockopt(socketFd.fd, SOL_SOCKET, SO_NOSIGPIPE, &noSigPipe, sizeof(noSigPipe));
#endif
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    std::memcpy(address.sun_path, path.c_str(), path.size() + 1);
    if (connect(socketFd.fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0) {
        Require(errno == EINPROGRESS || errno == EAGAIN, "worker_unavailable");
        Await(socketFd.fd, POLLOUT, deadline);
        int error        = 0;
        socklen_t length = sizeof(error);
        Require(getsockopt(socketFd.fd, SOL_SOCKET, SO_ERROR, &error, &length) == 0 && error == 0,
                "worker_unavailable");
    }
    uint32_t length = htonl(static_cast<uint32_t>(encoded.size()));
    Transfer(socketFd.fd, &length, sizeof(length), true, deadline);
    Transfer(socketFd.fd, const_cast<char*>(encoded.data()), encoded.size(), true, deadline);
    Transfer(socketFd.fd, const_cast<uint8_t*>(jpeg.data()), jpeg.size(), true, deadline);
    Transfer(socketFd.fd, &length, sizeof(length), false, deadline);
    const auto size = ntohl(length);
    Require(size > 0 && size <= kMaxJson, "invalid_response_size");
    std::string response(size, '\0');
    Transfer(socketFd.fd, response.data(), response.size(), false, deadline);
    try {
        return Parse(response);
    } catch (...) {
        throw std::runtime_error("invalid_worker_response");
    }
}
}  // namespace cosmo::service::visual
