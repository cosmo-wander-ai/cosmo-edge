#include "service/onvif/impl/OnvifProtocol.h"

#include <arpa/inet.h>
#include <curl/curl.h>
#include <openssl/evp.h>
#include <openssl/rand.h>

#include <algorithm>
#include <array>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <stdexcept>

#include "util/CipherUtil.h"

namespace cosmo::service::onvif {
namespace {
    constexpr const char* kDevice = "http://www.onvif.org/ver10/device/wsdl";
    constexpr const char* kMedia  = "http://www.onvif.org/ver10/media/wsdl";
    constexpr const char* kMedia2 = "http://www.onvif.org/ver20/media/wsdl";
    constexpr size_t kMaxXml      = 1024 * 1024;

    struct Transfer {
        std::string body;
        const std::atomic<bool>* running;
        std::chrono::steady_clock::time_point deadline;
    };
    size_t Receive(char* data, size_t size, size_t count, void* context) {
        auto& state = *static_cast<Transfer*>(context);
        if (size && count > kMaxXml / size)
            return 0;
        const auto length = size * count;
        if (state.body.size() + length > kMaxXml)
            return 0;
        state.body.append(data, length);
        return length;
    }
    int Progress(void* context, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
        const auto& state = *static_cast<Transfer*>(context);
        return (state.running && !state.running->load()) || std::chrono::steady_clock::now() > state.deadline;
    }
    size_t Header(char* data, size_t size, size_t count, void* context) {
        const auto length = size * count;
        if (length >= 5 && std::string_view(data, 5) == "HTTP/")
            static_cast<Transfer*>(context)->body.clear();
        return length;
    }
    std::string UrlPart(CURLU* url, CURLUPart part) {
        char* value = nullptr;
        if (curl_url_get(url, part, &value, 0) != CURLUE_OK)
            return {};
        std::string result(value);
        curl_free(value);
        return result;
    }
    std::unique_ptr<CURLU, decltype(&curl_url_cleanup)> Url(const std::string& input) {
        std::unique_ptr<CURLU, decltype(&curl_url_cleanup)> url(curl_url(), curl_url_cleanup);
        if (!url || input.size() > 2048 || input.find_first_of("\r\n") != std::string::npos ||
            curl_url_set(url.get(), CURLUPART_URL, input.c_str(), CURLU_NON_SUPPORT_SCHEME) != CURLUE_OK)
            throw std::runtime_error("invalid_endpoint");
        return url;
    }
    std::string Namespace(const tinyxml2::XMLElement* element) {
        const std::string name = element->Name();
        const auto colon       = name.find(':');
        const auto attribute   = colon == std::string::npos ? "xmlns" : "xmlns:" + name.substr(0, colon);
        for (auto* node = static_cast<const tinyxml2::XMLNode*>(element); node; node = node->Parent()) {
            if (const auto* parent = node->ToElement())
                if (const auto* value = parent->Attribute(attribute.c_str()))
                    return value;
        }
        return {};
    }
    std::string Timestamp(long offset) {
        auto now = std::time(nullptr) + offset;
        std::tm utc{};
        gmtime_r(&now, &utc);
        char buffer[32];
        std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &utc);
        return buffer;
    }
    int Number(const tinyxml2::XMLNode* node, const char* name) {
        const auto value = Text(node, name);
        try {
            return std::stoi(value);
        } catch (...) {
            return 0;
        }
    }
}  // namespace

std::string Escape(const std::string& text) {
    std::string result;
    for (unsigned char c : text) {
        switch (c) {
            case '&':
                result += "&amp;";
                break;
            case '<':
                result += "&lt;";
                break;
            case '>':
                result += "&gt;";
                break;
            case '"':
                result += "&quot;";
                break;
            case '\'':
                result += "&apos;";
                break;
            default:
                if (c < 32 && c != '\t' && c != '\n' && c != '\r')
                    throw std::runtime_error("invalid_parameter");
                result += static_cast<char>(c);
        }
    }
    return result;
}
std::string NormalizeEndpoint(const std::string& endpoint) {
    auto url          = Url(endpoint.find("://") == std::string::npos ? "http://" + endpoint : endpoint);
    const auto scheme = UrlPart(url.get(), CURLUPART_SCHEME);
    if ((scheme != "http" && scheme != "https") || UrlPart(url.get(), CURLUPART_HOST).empty() ||
        !UrlPart(url.get(), CURLUPART_USER).empty() || !UrlPart(url.get(), CURLUPART_PASSWORD).empty() ||
        !UrlPart(url.get(), CURLUPART_FRAGMENT).empty())
        throw std::runtime_error("invalid_endpoint");
    if (UrlPart(url.get(), CURLUPART_PATH) == "/")
        curl_url_set(url.get(), CURLUPART_PATH, "/onvif/device_service", 0);
    return UrlPart(url.get(), CURLUPART_URL);
}
std::string AuthenticatedUri(const std::string& uri, const Config& config) {
    auto url = Url(uri);
    if (UrlPart(url.get(), CURLUPART_SCHEME) != "rtsp" || UrlPart(url.get(), CURLUPART_HOST).empty())
        throw std::runtime_error("unsupported_stream");
    // Device-supplied userinfo is never trusted or persisted.
    const auto& user     = config.separateRtspCredentials ? config.rtspUsername : config.username;
    const auto& password = config.separateRtspCredentials ? config.rtspPassword : config.password;
    curl_url_set(url.get(), CURLUPART_USER, user.c_str(), CURLU_URLENCODE);
    curl_url_set(url.get(), CURLUPART_PASSWORD, password.c_str(), CURLU_URLENCODE);
    return UrlPart(url.get(), CURLUPART_URL);
}
std::vector<std::string> DiscoveryEndpoints(const std::string& advertised, const std::string& sender) {
    in_addr address{};
    if (inet_pton(AF_INET, sender.c_str(), &address) != 1)
        throw std::runtime_error("invalid_endpoint");
    std::vector<std::string> result;
    std::istringstream input(advertised);
    std::string endpoint;
    while (result.size() < 8 && input >> endpoint) {
        try {
            // XAddrs must contain absolute service URIs. Some cameras truncate
            // the IPv4 entry and only advertise a usable IPv6 URI. Discovery is
            // IPv4 here: preserve the advertised path/port, but contact the actual
            // reply sender, never an unrelated advertised host with credentials.
            if (endpoint.find("://") == std::string::npos)
                continue;
            auto url = Url(NormalizeEndpoint(endpoint));
            if (curl_url_set(url.get(), CURLUPART_HOST, sender.c_str(), 0) != CURLUE_OK)
                continue;
            endpoint = UrlPart(url.get(), CURLUPART_URL);
            if (std::find(result.begin(), result.end(), endpoint) == result.end())
                result.push_back(endpoint);
        } catch (const std::exception&) {
            // Ignore malformed entries; a conventional endpoint is tried only
            // when there were no valid advertised service paths at all.
        }
    }
    if (result.empty())
        result.push_back(NormalizeEndpoint(sender));
    return result;
}
std::string LocalName(const char* name) {
    if (!name)
        return {};
    std::string value(name);
    const auto colon = value.find(':');
    return colon == std::string::npos ? value : value.substr(colon + 1);
}
const tinyxml2::XMLElement* Child(const tinyxml2::XMLNode* node, const char* name) {
    if (!node)
        return nullptr;
    for (auto* e = node->FirstChildElement(); e; e = e->NextSiblingElement())
        if (LocalName(e->Name()) == name)
            return e;
    return nullptr;
}
const tinyxml2::XMLElement* Find(const tinyxml2::XMLNode* node, const char* name) {
    if (!node)
        return nullptr;
    for (auto* e = node->FirstChildElement(); e; e = e->NextSiblingElement()) {
        if (LocalName(e->Name()) == name)
            return e;
        if (const auto* result = Find(e, name))
            return result;
    }
    return nullptr;
}
std::string Text(const tinyxml2::XMLNode* node, const char* name) {
    const auto* element = Find(node, name);
    return element && element->GetText() ? element->GetText() : "";
}
std::unique_ptr<tinyxml2::XMLDocument> Parse(const std::string& xml) {
    auto document = std::make_unique<tinyxml2::XMLDocument>();
    if (xml.size() > kMaxXml || xml.find("<!DOCTYPE") != std::string::npos ||
        xml.find("<!ENTITY") != std::string::npos ||
        document->Parse(xml.data(), xml.size()) != tinyxml2::XML_SUCCESS)
        throw std::runtime_error("invalid_xml");
    const auto* envelope = document->RootElement();
    if (!envelope || LocalName(envelope->Name()) != "Envelope" || !Child(envelope, "Body") ||
        Namespace(envelope) != "http://www.w3.org/2003/05/soap-envelope")
        throw std::runtime_error("invalid_xml");
    return document;
}
std::string UsernameToken(const Config& config, const std::string& nonce, const std::string& created) {
    const auto input = nonce + created + config.password;
    std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
    unsigned int length = 0;
    if (EVP_Digest(input.data(), input.size(), digest.data(), &length, EVP_sha1(), nullptr) != 1)
        throw std::runtime_error("crypto_error");
    return "<wsse:Security s:mustUnderstand=\"1\" "
           "xmlns:wsse=\"http://docs.oasis-open.org/wss/2004/01/oasis-200401-wss-wssecurity-secext-1.0.xsd\" "
           "xmlns:wsu=\"http://docs.oasis-open.org/wss/2004/01/"
           "oasis-200401-wss-wssecurity-utility-1.0.xsd\"><wsse:UsernameToken><wsse:Username>" +
           Escape(config.username) +
           "</wsse:Username><wsse:Password "
           "Type=\"http://docs.oasis-open.org/wss/2004/01/"
           "oasis-200401-wss-username-token-profile-1.0#PasswordDigest\">" +
           util::EncBase64Ex(digest.data(), length) +
           "</wsse:Password><wsse:Nonce "
           "EncodingType=\"http://docs.oasis-open.org/wss/2004/01/"
           "oasis-200401-wss-soap-message-security-1.0#Base64Binary\">" +
           util::EncBase64(nonce) + "</wsse:Nonce><wsu:Created>" + created +
           "</wsu:Created></wsse:UsernameToken></wsse:Security>";
}
nlohmann::json ParseProfiles(const std::string& xml) {
    const auto document  = Parse(xml);
    const auto* response = Find(document.get(), "GetProfilesResponse");
    if (!response)
        throw std::runtime_error("invalid_xml");
    auto result = nlohmann::json::array();
    for (const auto* profile = response->FirstChildElement(); profile;
         profile             = profile->NextSiblingElement()) {
        if (LocalName(profile->Name()) != "Profiles")
            continue;
        const char* token = profile->Attribute("token");
        if (!token || !*token)
            continue;
        auto* encoder = Find(profile, "VideoEncoderConfiguration");
        if (!encoder)
            encoder = Find(profile, "VideoEncoder");
        auto* source = Find(profile, "VideoSourceConfiguration");
        if (!source)
            source = Find(profile, "VideoSource");
        if (!encoder)
            continue;
        result.push_back({{"token", token},
                          {"name", Text(profile, "Name")},
                          {"codec", Text(encoder, "Encoding")},
                          {"width", Number(encoder, "Width")},
                          {"height", Number(encoder, "Height")},
                          {"fps", Number(encoder, "FrameRateLimit")},
                          {"videoSourceToken", Text(source, "SourceToken")}});
        if (result.size() >= 128)
            break;
    }
    return result;
}

Client::Client(Config config, const std::atomic<bool>* running)
    : config_(std::move(config)),
      running_(running),
      deadline_(std::chrono::steady_clock::now() + std::chrono::seconds(20)) {}

std::string Client::Call(const std::string& url, const std::string& ns, const std::string& operation,
                         const std::string& body, bool anonymous) {
    // Bounded SOAP transport; no redirects or environment proxies carrying camera credentials.
    NormalizeEndpoint(url);
    if (UrlPart(Url(url).get(), CURLUPART_HOST) != UrlPart(Url(config_.endpoint).get(), CURLUPART_HOST))
        throw std::runtime_error("invalid_endpoint");
    const auto remaining =
        std::chrono::duration_cast<std::chrono::milliseconds>(deadline_ - std::chrono::steady_clock::now())
            .count();
    if (remaining <= 0 || (running_ && !running_->load()))
        throw std::runtime_error("timeout");
    std::string security;
    if (!anonymous) {
        std::string nonce(20, '\0');
        if (RAND_bytes(reinterpret_cast<unsigned char*>(nonce.data()), nonce.size()) != 1)
            throw std::runtime_error("crypto_error");
        security = UsernameToken(config_, nonce, Timestamp(time_offset_));
    }
    const std::string envelope =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?><s:Envelope "
        "xmlns:s=\"http://www.w3.org/2003/05/soap-envelope\" "
        "xmlns:tt=\"http://www.onvif.org/ver10/schema\"><s:Header>" +
        security + "</s:Header><s:Body><t:" + operation + " xmlns:t=\"" + ns + "\">" + body +
        "</t:" + operation + "></s:Body></s:Envelope>";
    std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> curl(curl_easy_init(), curl_easy_cleanup);
    if (!curl)
        throw std::runtime_error("network_error");
    Transfer state{{}, running_, deadline_};
    curl_slist* raw_headers = nullptr;
    const auto content_type =
        "Content-Type: application/soap+xml; charset=utf-8; action=\"" + ns + "/" + operation + "\"";
    raw_headers = curl_slist_append(raw_headers, content_type.c_str());
    raw_headers = curl_slist_append(raw_headers, "Expect:");
    std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)> headers(raw_headers, curl_slist_free_all);
    curl_easy_setopt(curl.get(), CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl.get(), CURLOPT_PROXY, "");
    curl_easy_setopt(curl.get(), CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(curl.get(), CURLOPT_HTTPHEADER, headers.get());
    curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDS, envelope.c_str());
    curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(envelope.size()));
    curl_easy_setopt(curl.get(), CURLOPT_CONNECTTIMEOUT_MS, 2000L);
    curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT_MS, static_cast<long>(std::min<int64_t>(5000, remaining)));
    curl_easy_setopt(curl.get(), CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, Receive);
    curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &state);
    curl_easy_setopt(curl.get(), CURLOPT_HEADERFUNCTION, Header);
    curl_easy_setopt(curl.get(), CURLOPT_HEADERDATA, &state);
    curl_easy_setopt(curl.get(), CURLOPT_XFERINFOFUNCTION, Progress);
    curl_easy_setopt(curl.get(), CURLOPT_XFERINFODATA, &state);
    curl_easy_setopt(curl.get(), CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl.get(), CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl.get(), CURLOPT_SSL_VERIFYHOST, 2L);
    // Send the complete WSSE envelope first. Preselecting Digest makes libcurl
    // send an empty POST probe, which some SOAP cameras leave unanswered.
    auto result = curl_easy_perform(curl.get());
    if (result != CURLE_OK)
        throw std::runtime_error(result == CURLE_OPERATION_TIMEDOUT ? "timeout" : "network_error");
    long status = 0;
    curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &status);
    if (status == 401 && !anonymous) {
        const auto retry_remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                                         deadline_ - std::chrono::steady_clock::now())
                                         .count();
        if (retry_remaining <= 0 || (running_ && !running_->load()))
            throw std::runtime_error("timeout");
        state.body.clear();
        curl_easy_setopt(curl.get(), CURLOPT_HTTPAUTH, static_cast<long>(CURLAUTH_DIGEST));
        curl_easy_setopt(curl.get(), CURLOPT_USERNAME, config_.username.c_str());
        curl_easy_setopt(curl.get(), CURLOPT_PASSWORD, config_.password.c_str());
        curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT_MS,
                         static_cast<long>(std::min<int64_t>(5000, retry_remaining)));
        result = curl_easy_perform(curl.get());
        if (result != CURLE_OK)
            throw std::runtime_error(result == CURLE_OPERATION_TIMEDOUT ? "timeout" : "network_error");
        curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &status);
    }
    if (status == 401 || status == 403)
        throw std::runtime_error("unauthorized");
    if (status == 404 || status == 405)
        throw std::runtime_error("service_unavailable");
    const auto document = Parse(state.body);
    if (const auto* fault = Find(document.get(), "Fault")) {
        // Only classify device text. Never return untrusted SOAP/credential material in errors.
        const auto code = Text(Find(fault, "Subcode"), "Value");
        if (code.find("NotAuthorized") != std::string::npos)
            throw std::runtime_error("unauthorized");
        throw std::runtime_error("soap_fault");
    }
    if (status != 200)
        throw std::runtime_error("service_unavailable");
    const auto* response = Child(Child(document->RootElement(), "Body"), (operation + "Response").c_str());
    if (!response || Namespace(response) != ns)
        throw std::runtime_error("invalid_xml");
    return state.body;
}
void Client::Prepare() {
    config_.endpoint = NormalizeEndpoint(config_.endpoint);
    try {
        const auto doc = Parse(Call(config_.endpoint, kDevice, "GetSystemDateAndTime", "", true));
        if (const auto* utc = Find(doc.get(), "UTCDateTime")) {
            std::tm time{};
            time.tm_year = Number(utc, "Year") - 1900;
            time.tm_mon  = Number(utc, "Month") - 1;
            time.tm_mday = Number(utc, "Day");
            time.tm_hour = Number(utc, "Hour");
            time.tm_min  = Number(utc, "Minute");
            time.tm_sec  = Number(utc, "Second");
            if (time.tm_year >= 70 && time.tm_mon >= 0 && time.tm_mon < 12 && time.tm_mday > 0)
                time_offset_ = static_cast<long>(timegm(&time) - std::time(nullptr));
        }
    } catch (const std::exception&) { /* Some devices require authentication even for time. */
    }
    try {
        const auto doc       = Parse(Call(config_.endpoint, kDevice, "GetServices",
                                          "<t:IncludeCapability>false</t:IncludeCapability>"));
        const auto* response = Find(doc.get(), "GetServicesResponse");
        for (const auto* entry = response->FirstChildElement(); entry; entry = entry->NextSiblingElement()) {
            const auto ns = Text(entry, "Namespace");
            if (ns == kMedia)
                media_ = Text(entry, "XAddr");
            if (ns == kMedia2)
                media2_ = Text(entry, "XAddr");
        }
    } catch (const std::exception& error) {
        if (std::string(error.what()) == "unauthorized")
            throw;
    }
    if (media_.empty() && media2_.empty()) {
        const auto doc =
            Parse(Call(config_.endpoint, kDevice, "GetCapabilities", "<t:Category>Media</t:Category>"));
        media_ = Text(Find(doc.get(), "Media"), "XAddr");
    }
    if (media_.empty() && media2_.empty())
        throw std::runtime_error("service_unavailable");
}
nlohmann::json Client::Probe() {
    Prepare();
    auto profiles = nlohmann::json::array();
    if (!media2_.empty()) {
        try {
            profiles = ParseProfiles(Call(media2_, kMedia2, "GetProfiles", "<t:Type>All</t:Type>"));
        } catch (const std::exception&) {
            if (media_.empty())
                throw;
        }
    }
    if (profiles.empty() && !media_.empty())
        profiles = ParseProfiles(Call(media_, kMedia, "GetProfiles", ""));
    if (profiles.empty())
        throw std::runtime_error("no_profiles");
    return {{"endpoint", config_.endpoint}, {"profiles", profiles}};
}
std::string Client::StreamUri() {
    Prepare();
    if (config_.profileToken.empty())
        throw std::runtime_error("no_profile_selected");
    std::string uri;
    if (!media2_.empty()) {
        try {
            const auto doc = Parse(Call(media2_, kMedia2, "GetStreamUri",
                                        "<t:Protocol>RTSP</t:Protocol><t:ProfileToken>" +
                                            Escape(config_.profileToken) + "</t:ProfileToken>"));
            uri            = Text(doc.get(), "Uri");
        } catch (const std::exception&) {
            if (media_.empty())
                throw;
        }
    }
    if (uri.empty() && !media_.empty()) {
        const auto doc =
            Parse(Call(media_, kMedia, "GetStreamUri",
                       "<t:StreamSetup><tt:Stream>RTP-Unicast</tt:Stream><tt:Transport><tt:Protocol>RTSP</"
                       "tt:Protocol></tt:Transport></t:StreamSetup><t:ProfileToken>" +
                           Escape(config_.profileToken) + "</t:ProfileToken>"));
        uri = Text(doc.get(), "Uri");
    }
    return AuthenticatedUri(uri, config_);
}
}  // namespace cosmo::service::onvif
