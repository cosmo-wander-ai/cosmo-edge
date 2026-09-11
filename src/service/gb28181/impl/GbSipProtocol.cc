#include "service/gb28181/impl/GbSipProtocol.h"

#include <arpa/inet.h>
#include <iconv.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <tinyxml2.h>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <sstream>
#include <stdexcept>

namespace cosmo::service::gb {
namespace {
    std::string Lower(std::string value) {
        std::transform(value.begin(), value.end(), value.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        return value;
    }
    std::string Trim(const std::string& value) {
        const auto first = value.find_first_not_of(" \t\r\n");
        return first == std::string::npos
                   ? ""
                   : value.substr(first, value.find_last_not_of(" \t\r\n") - first + 1);
    }
    int Number(const std::string& value, int maximum) {
        int number  = 0;
        auto result = std::from_chars(value.data(), value.data() + value.size(), number);
        if (value.empty() || result.ec != std::errc{} || result.ptr != value.data() + value.size() ||
            number < 0 || number > maximum)
            throw std::runtime_error("invalid_sip");
        return number;
    }
    std::string Hex(const unsigned char* bytes, size_t size) {
        static constexpr char digits[] = "0123456789abcdef";
        std::string out;
        for (size_t i = 0; i < size; ++i) {
            out += digits[bytes[i] >> 4];
            out += digits[bytes[i] & 15];
        }
        return out;
    }
    std::map<std::string, std::string> Digest(const std::string& header) {
        std::map<std::string, std::string> result;
        if (Lower(header.substr(0, 7)) != "digest ")
            return result;
        size_t position = 7;
        while (position < header.size()) {
            while (position < header.size() &&
                   (header[position] == ',' || std::isspace(static_cast<unsigned char>(header[position]))))
                ++position;
            auto equal = header.find('=', position);
            if (equal == std::string::npos)
                break;
            auto key = Lower(Trim(header.substr(position, equal - position)));
            position = equal + 1;
            while (position < header.size() && header[position] == ' ')
                ++position;
            std::string value;
            if (position < header.size() && header[position] == '"') {
                ++position;
                bool closed = false;
                while (position < header.size()) {
                    char c = header[position++];
                    if (c == '"') {
                        closed = true;
                        break;
                    }
                    if (c == '\\' && position < header.size())
                        c = header[position++];
                    value += c;
                }
                if (!closed)
                    return {};
            } else {
                auto end = header.find(',', position);
                value    = Trim(header.substr(position, end - position));
                position = end == std::string::npos ? header.size() : end;
            }
            if (!result.emplace(key, value).second)
                return {};
        }
        return result;
    }
    std::string Text(const tinyxml2::XMLElement* parent, const char* name) {
        auto* element = parent ? parent->FirstChildElement(name) : nullptr;
        return element && element->GetText() ? Trim(element->GetText()) : "";
    }
    std::string Utf8(const std::string& body) {
        auto declaration = Lower(body.substr(0, std::min<size_t>(body.find("?>") + 2, 200)));
        if (declaration.find("gb2312") == std::string::npos && declaration.find("gbk") == std::string::npos)
            return body;
        auto converter = iconv_open("UTF-8", "GB18030");
        if (converter == reinterpret_cast<iconv_t>(-1))
            throw std::runtime_error("invalid_xml");
        std::string out(body.size() * 4 + 16, '\0');
        char* input      = const_cast<char*>(body.data());
        char* output     = out.data();
        size_t inputSize = body.size(), outputSize = out.size();
        const auto result = iconv(converter, &input, &inputSize, &output, &outputSize);
        iconv_close(converter);
        if (result == static_cast<size_t>(-1))
            throw std::runtime_error("invalid_xml");
        out.resize(out.size() - outputSize);
        return out;
    }
}  // namespace
std::string Message::Header(const std::string& name) const {
    auto it = headers.find(name);
    return it == headers.end() ? "" : it->second;
}
bool IsId(const std::string& value) {
    return value.size() == 20 &&
           std::all_of(value.begin(), value.end(), [](char c) { return c >= '0' && c <= '9'; });
}
bool IsIpv4(const std::string& value) {
    in_addr address{};
    return inet_pton(AF_INET, value.c_str(), &address) == 1 && ntohl(address.s_addr) != 0 &&
           (ntohl(address.s_addr) >> 24) < 224;
}
std::string RandomHex(size_t bytes) {
    if (bytes > 64)
        throw std::runtime_error("crypto_error");
    unsigned char data[64];
    if (RAND_bytes(data, bytes) != 1)
        throw std::runtime_error("crypto_error");
    return Hex(data, bytes);
}
std::string Md5(const std::string& value) {
    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int size = 0;
    if (EVP_Digest(value.data(), value.size(), hash, &size, EVP_md5(), nullptr) != 1)
        throw std::runtime_error("crypto_error");
    return Hex(hash, size);
}
std::string Uri(const std::string& address) {
    auto start = address.find("sip:");
    if (start == std::string::npos)
        return "";
    auto end = address.find_first_of("> \t\r\n", start);
    return address.substr(start, end - start);
}
std::string User(const std::string& address) {
    auto uri = Uri(address);
    auto end = uri.find('@');
    return end == std::string::npos ? "" : uri.substr(4, end - 4);
}
bool Pop(std::string& buffer, Message& message) {
    while (buffer.rfind("\r\n", 0) == 0)
        buffer.erase(0, 2);
    if (buffer.size() > 1100000)
        throw std::runtime_error("invalid_sip");
    const auto end = buffer.find("\r\n\r\n");
    if (end == std::string::npos) {
        if (buffer.size() > 16384)
            throw std::runtime_error("invalid_sip");
        return false;
    }
    if (end > 16384)
        throw std::runtime_error("invalid_sip");
    Message parsed;
    std::istringstream lines(buffer.substr(0, end));
    std::string line, version;
    std::getline(lines, line);
    std::istringstream first(Trim(line));
    first >> parsed.method;
    if (parsed.method == "SIP/2.0") {
        std::string code;
        first >> code;
        parsed.status = Number(code, 699);
        parsed.method.clear();
        if (parsed.status < 100)
            throw std::runtime_error("invalid_sip");
    } else {
        first >> parsed.uri >> version;
        if (version != "SIP/2.0" || parsed.uri.rfind("sip:", 0) != 0)
            throw std::runtime_error("invalid_sip");
    }
    while (std::getline(lines, line)) {
        if (line.empty() || line.front() == ' ' || line.front() == '\t')
            throw std::runtime_error("invalid_sip");
        const auto colon = line.find(':');
        if (colon == std::string::npos)
            throw std::runtime_error("invalid_sip");
        auto key = Lower(Trim(line.substr(0, colon)));
        static const std::map<std::string, std::string> compact{
            {"v", "via"},     {"f", "from"},        {"t", "to"}, {"i", "call-id"}, {"l", "content-length"},
            {"m", "contact"}, {"c", "content-type"}};
        if (compact.count(key))
            key = compact.at(key);
        const auto value = Trim(line.substr(colon + 1));
        if (value.find_first_of("\r\n") != std::string::npos || value.find('\0') != std::string::npos ||
            parsed.headers.size() >= 128)
            throw std::runtime_error("invalid_sip");
        if (!parsed.headers.emplace(key, value).second) {
            if (key != "via" && key != "record-route")
                throw std::runtime_error("invalid_sip");
            parsed.headers[key] += ", " + value;
        }
    }
    for (const auto* key : {"via", "from", "to", "call-id", "cseq", "content-length"})
        if (parsed.Header(key).empty())
            throw std::runtime_error("invalid_sip");
    const int length = Number(parsed.Header("content-length"), 1024 * 1024);
    if (buffer.size() < end + 4 + length)
        return false;
    parsed.body = buffer.substr(end + 4, length);
    buffer.erase(0, end + 4 + length);
    message = std::move(parsed);
    return true;
}
std::string Response(const Message& request, int status, const std::string& extra) {
    const std::map<int, std::string> reasons{{200, "OK"},
                                             {400, "Bad Request"},
                                             {401, "Unauthorized"},
                                             {403, "Forbidden"},
                                             {404, "Not Found"},
                                             {405, "Method Not Allowed"},
                                             {481, "Call/Transaction Does Not Exist"},
                                             {503, "Service Unavailable"}};
    std::string out = "SIP/2.0 " + std::to_string(status) + " " +
                      (reasons.count(status) ? reasons.at(status) : "Error") + "\r\n";
    for (const auto* key : {"via", "from", "to", "call-id", "cseq"}) {
        auto value = request.Header(key);
        if (std::string(key) == "to" && value.find(";tag=") == std::string::npos)
            value += ";tag=cosmo";
        out += std::string(key) + ": " + value + "\r\n";
    }
    return out + extra + "User-Agent: CosmoEdge\r\nContent-Length: 0\r\n\r\n";
}
bool VerifyDigest(const Message& request, const std::string& username, const std::string& realm,
                  const std::string& ha1, const std::string& nonce) {
    auto auth = Digest(request.Header("authorization"));
    if (nonce.empty() || auth["username"] != username || auth["realm"] != realm || auth["nonce"] != nonce ||
        auth["uri"] != request.uri || (!auth["algorithm"].empty() && Lower(auth["algorithm"]) != "md5"))
        return false;
    std::string middle = nonce;
    if (!auth["qop"].empty()) {
        if (auth["qop"] != "auth" || auth["nc"] != "00000001" || auth["cnonce"].empty() ||
            auth["cnonce"].size() > 256)
            return false;
        middle += ":" + auth["nc"] + ":" + auth["cnonce"] + ":auth";
    }
    const auto expected = Md5(ha1 + ":" + middle + ":" + Md5(request.method + ":" + request.uri));
    const auto actual   = Lower(auth["response"]);
    return expected.size() == actual.size() &&
           CRYPTO_memcmp(expected.data(), actual.data(), expected.size()) == 0;
}
XmlMessage ParseXml(const std::string& body) {
    if (body.empty() || body.size() > 1024 * 1024 || body.find("<!DOCTYPE") != std::string::npos ||
        body.find("<!ENTITY") != std::string::npos)
        throw std::runtime_error("invalid_xml");
    tinyxml2::XMLDocument document;
    const auto xml = Utf8(body);
    if (document.Parse(xml.data(), xml.size()) != tinyxml2::XML_SUCCESS)
        throw std::runtime_error("invalid_xml");
    const auto* root = document.RootElement();
    if (!root || (std::string(root->Name()) != "Response" && std::string(root->Name()) != "Notify"))
        throw std::runtime_error("invalid_xml");
    XmlMessage result;
    result.command = Text(root, "CmdType");
    result.device  = Text(root, "DeviceID");
    result.sn      = Text(root, "SN");
    if (!IsId(result.device) || result.sn.empty() || result.sn.size() > 10)
        throw std::runtime_error("invalid_xml");
    if (result.command == "Catalog") {
        result.total     = Number(Text(root, "SumNum"), 1024);
        const auto* list = root->FirstChildElement("DeviceList");
        int count        = 0;
        for (auto* item = list ? list->FirstChildElement("Item") : nullptr; item;
             item       = item->NextSiblingElement("Item")) {
            Channel channel{Text(item, "DeviceID"), Text(item, "Name"), Text(item, "Status")};
            if (!IsId(channel.id) || channel.name.size() > 512 || ++count > 1024)
                throw std::runtime_error("invalid_xml");
            result.channels.push_back(std::move(channel));
        }
        if (list && list->Attribute("Num") && Number(list->Attribute("Num"), 1024) != count)
            throw std::runtime_error("invalid_xml");
    }
    return result;
}
std::string CatalogQuery(const std::string& platformId, const std::string& sn) {
    return "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\r\n<Query><CmdType>Catalog</CmdType><SN>" + sn +
           "</SN><DeviceID>" + platformId + "</DeviceID></Query>\r\n";
}
std::string Offer(const std::string& platformId, const std::string& address, int port,
                  const std::string& ssrc) {
    return "v=0\r\no=" + platformId + " 0 0 IN IP4 " + address + "\r\ns=Play\r\nc=IN IP4 " + address +
           "\r\nt=0 0\r\nm=video " + std::to_string(port) +
           " TCP/RTP/AVP 96\r\na=recvonly\r\na=rtpmap:96 "
           "PS/90000\r\na=setup:passive\r\na=connection:new\r\ny=" +
           ssrc + "\r\nf=\r\n";
}
bool AcceptsOffer(const std::string& sdp) {
    // Absence of setup is tolerated for older devices which initiate TCP anyway.
    bool video = false, ps = false;
    std::istringstream lines(sdp);
    std::string line;
    while (std::getline(lines, line)) {
        line = Trim(line);
        if (line.rfind("m=video ", 0) == 0) {
            std::istringstream media(line);
            std::string kind, port, protocol, payload;
            media >> kind >> port >> protocol;
            if (port == "0" || protocol != "TCP/RTP/AVP")
                return false;
            while (media >> payload)
                if (payload == "96")
                    video = true;
        }
        if (Lower(line) == "a=rtpmap:96 ps/90000")
            ps = true;
        if (line == "a=setup:passive" || line == "a=sendrecv" || line == "a=recvonly" || line == "a=inactive")
            return false;
    }
    return video && ps;
}
}  // namespace cosmo::service::gb
