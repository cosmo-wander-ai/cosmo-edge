// Transcode utility (GB18030 -> UTF-8, etc.)

#pragma once

#include <iconv.h>

#include <string>

namespace cosmo::util {
class Transcode {
public:
    Transcode(const char *toCode, const char *fromCode);
    Transcode(const Transcode &) = delete;
    Transcode(Transcode &&);
    Transcode &operator=(const Transcode &) = delete;
    Transcode &operator=(Transcode &&);
    ~Transcode();

    std::string Convert(const std::string &str);

private:
    iconv_t conv_value_;
};
}  // namespace cosmo::util
