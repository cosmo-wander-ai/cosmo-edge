// MultipartStreamParser.cc — Extracted from HttpServerThread.cc (Phase 2 file split).

#include "network/http/MultipartStreamParser.h"

#include <cstring>
#include <fstream>
#include <optional>

#include "event2/buffer.h"

namespace cosmo::network::http {

MultipartStreamParser::MultipartStreamParser(std::string boundary) : boundary_(std::move(boundary)) {
    boundary_line_ = "--" + boundary_;
    end_boundary_  = boundary_line_ + "--";
    marker_        = "\r\n" + boundary_line_;
}

MultipartParseResult MultipartStreamParser::ParseToFile(
    struct evbuffer* input_buf, const std::function<std::string(const std::string& filename)>& makeFilePath) {
    MultipartParseResult out;
    if (!input_buf) {
        out.err = "input buffer is null";
        return out;
    }
    if (boundary_.empty()) {
        out.err = "boundary is empty";
        return out;
    }

    enum class State { FindFirstBoundary, ReadHeaders, ReadData, Done, Error };
    State state = State::FindFirstBoundary;

    std::string curName;
    std::string curFilename;
    std::string curFilePath;
    std::optional<std::ofstream> fileOut;
    size_t fileBytes = 0;

    std::string headerBuf;
    std::string fieldBuf;

    std::string carry;
    carry.reserve(marker_.size() + 8);

    auto flush_field = [&]() {
        if (!curName.empty()) {
            out.fields[curName] = fieldBuf;
        }
        curName.clear();
        fieldBuf.clear();
    };

    auto close_file = [&]() {
        if (fileOut && fileOut->is_open()) {
            fileOut->close();
        }
        fileOut.reset();
    };

    auto parse_content_disposition = [&](const std::string& headers) {
        curName.clear();
        curFilename.clear();
        auto pos = headers.find("Content-Disposition:");
        if (pos == std::string::npos)
            return;
        auto namePos = headers.find("name=\"", pos);
        if (namePos != std::string::npos) {
            namePos += 6;
            auto nameEnd = headers.find('"', namePos);
            if (nameEnd != std::string::npos)
                curName = headers.substr(namePos, nameEnd - namePos);
        }
        auto fnPos = headers.find("filename=\"", pos);
        if (fnPos != std::string::npos) {
            fnPos += 10;
            auto fnEnd = headers.find('"', fnPos);
            if (fnEnd != std::string::npos)
                curFilename = headers.substr(fnPos, fnEnd - fnPos);
        }
    };

    auto ensure_file_open = [&]() -> bool {
        if (curFilename.empty())
            return false;
        if (fileOut && fileOut->is_open())
            return true;
        curFilePath = makeFilePath(curFilename);
        if (curFilePath.empty())
            return false;
        fileOut.emplace(curFilePath, std::ios::binary);
        if (!fileOut->is_open()) {
            fileOut.reset();
            return false;
        }
        out.fields["filePath"] = curFilePath;
        out.fields["fileName"] = curFilename;
        fileBytes              = 0;
        return true;
    };

    auto consume_until = [&](std::string& buf, const std::string& token) -> std::optional<std::string> {
        auto p = buf.find(token);
        if (p == std::string::npos)
            return std::nullopt;
        std::string before = buf.substr(0, p);
        buf.erase(0, p + token.size());
        return before;
    };

    // Main loop: read chunks from evbuffer, append to carry, process
    while (state != State::Done && state != State::Error) {
        char tmp[64 * 1024];
        size_t n = evbuffer_remove(input_buf, tmp, sizeof(tmp));
        if (n == 0) {
            state   = State::Error;
            out.err = "unexpected end of multipart body";
            break;
        }

        carry.append(tmp, n);

        // Soft protection against unbounded carry growth
        if (carry.size() > (4 * 1024 * 1024) && state != State::ReadData) {
            state   = State::Error;
            out.err = "multipart headers too large";
            break;
        }

        bool progressed = true;
        while (progressed && state != State::Done && state != State::Error) {
            progressed = false;
            switch (state) {
                case State::FindFirstBoundary: {
                    auto p = carry.find(boundary_line_);
                    if (p == std::string::npos)
                        break;
                    carry.erase(0, p + boundary_line_.size());
                    if (carry.rfind("\r\n", 0) == 0)
                        carry.erase(0, 2);
                    state      = State::ReadHeaders;
                    progressed = true;
                    break;
                }
                case State::ReadHeaders: {
                    auto part = consume_until(carry, "\r\n\r\n");
                    if (!part)
                        break;
                    headerBuf = *part;
                    parse_content_disposition(headerBuf);
                    state      = State::ReadData;
                    progressed = true;
                    break;
                }
                case State::ReadData: {
                    auto p = carry.find(marker_);
                    if (p == std::string::npos) {
                        size_t keep     = std::min(carry.size(), marker_.size() + 8);
                        size_t writeLen = carry.size() - keep;
                        if (writeLen == 0)
                            break;

                        if (!curFilename.empty()) {
                            if (!ensure_file_open()) {
                                state   = State::Error;
                                out.err = "open temp file failed";
                                break;
                            }
                            fileOut->write(carry.data(), writeLen);
                            fileBytes += writeLen;
                        } else {
                            fieldBuf.append(carry.data(), writeLen);
                        }
                        carry.erase(0, writeLen);
                        progressed = true;
                        break;
                    }

                    std::string data = carry.substr(0, p);
                    if (!curFilename.empty()) {
                        if (!ensure_file_open()) {
                            state   = State::Error;
                            out.err = "open temp file failed";
                            break;
                        }
                        if (!data.empty()) {
                            fileOut->write(data.data(), data.size());
                            fileBytes += data.size();
                        }
                        close_file();
                        out.fields["contentLength"] = std::to_string(fileBytes);
                    } else {
                        fieldBuf.append(data);
                        if (fieldBuf.size() >= 2 && fieldBuf.compare(fieldBuf.size() - 2, 2, "\r\n") == 0) {
                            fieldBuf.resize(fieldBuf.size() - 2);
                        }
                        flush_field();
                    }

                    carry.erase(0, p + marker_.size());
                    if (carry.rfind("--", 0) == 0) {
                        state      = State::Done;
                        progressed = true;
                        break;
                    }
                    if (carry.rfind("\r\n", 0) == 0) {
                        carry.erase(0, 2);
                    }
                    state      = State::ReadHeaders;
                    progressed = true;
                    break;
                }
                case State::Done:
                    break;
                case State::Error:
                    break;
            }
        }
    }

    if (state == State::Done) {
        out.ok = true;
        return out;
    }
    close_file();
    if (out.err.empty())
        out.err = "multipart parse failed";
    return out;
}

}  // namespace cosmo::network::http
