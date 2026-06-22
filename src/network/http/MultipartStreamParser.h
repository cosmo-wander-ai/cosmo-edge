#pragma once

#include <functional>
#include <map>
#include <string>

struct evbuffer;

namespace cosmo::network::http {

/// Result of a multipart parse operation.
struct MultipartParseResult {
    std::map<std::string, std::string> fields;
    bool ok{false};
    std::string err;
};

/// Stream parser for "single file + few fields" multipart uploads:
/// - Reads from evbuffer in chunks to avoid evbuffer_pullup(-1) large memory allocation
/// - File content is written directly to disk, not concatenated in memory
class MultipartStreamParser {
public:
    explicit MultipartStreamParser(std::string boundary);

    /// Parse multipart body from evbuffer, writing file content to disk via makeFilePath callback.
    /// @param input_buf    The libevent evbuffer containing the raw multipart body.
    /// @param makeFilePath Callback that returns a filesystem path for a given upload filename.
    /// @return             Parse result containing extracted fields and status.
    MultipartParseResult ParseToFile(
        struct evbuffer* input_buf,
        const std::function<std::string(const std::string& filename)>& makeFilePath);

private:
    std::string boundary_;
    std::string boundary_line_;
    std::string end_boundary_;
    std::string marker_;
};

}  // namespace cosmo::network::http
