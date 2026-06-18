#pragma once

#include <fstream>
#include <vector>

namespace cosmo::network::http {

class HttpRequestHandler {
public:
    virtual ~HttpRequestHandler() = default;

    /**
     * @return: returning a value != size will abort the download
     */
    virtual size_t AppendData(const char* data, size_t size) = 0;

    /**
     * @return: flush buffered content
     */
    virtual void Flush(){};
};

class HttpStringHandler : public HttpRequestHandler {
public:
    size_t AppendData(const char* data, size_t size) override;
    const std::string& GetData() const;

private:
    std::string data_;
};

class HttpFileHandler : public HttpRequestHandler {
public:
    explicit HttpFileHandler(const std::string& filename);
    size_t AppendData(const char* data, size_t size) override;
    void Flush() override;

private:
    std::ofstream file_;
};

class HttpImageHandler : public HttpRequestHandler {
public:
    size_t AppendData(const char* data, size_t size) override;
    const std::vector<u_char>& GetImageData() const;

private:
    std::vector<u_char> data_;
};

}  // namespace cosmo::network::http
