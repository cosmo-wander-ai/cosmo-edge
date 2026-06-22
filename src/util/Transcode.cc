// Transcode utility implementation

#include "util/Transcode.h"

#include <cerrno>
#include <cstring>
#include <vector>

#include "util/ErrorCode.h"
#include "util/Exception.h"

namespace cosmo::util {
Transcode::Transcode(const char* toCode, const char* fromCode) : conv_value_(iconv_open(toCode, fromCode)) {
    if (conv_value_ == reinterpret_cast<iconv_t>(-1)) {
        throw cosmo::util::ErrorMessage(cosmo::util::ErrorEnum::DecoderColorConvertFailed,
                                        (std::string("iconv_open error: ") + strerror(errno)).c_str());
    }
}

Transcode::Transcode(Transcode&& other) : conv_value_(other.conv_value_) {
    other.conv_value_ = reinterpret_cast<iconv_t>(-1);
}

Transcode& Transcode::operator=(Transcode&& other) {
    if (this != &other) {
        conv_value_       = other.conv_value_;
        other.conv_value_ = reinterpret_cast<iconv_t>(-1);
    }
    return *this;
}

Transcode::~Transcode() {
    if (conv_value_ != reinterpret_cast<iconv_t>(-1)) {
        iconv_close(conv_value_);
    }
}

std::string Transcode::Convert(const std::string& str) {
    if (str.empty()) {
        return {};
    }

    std::string src(str);
    char* p_src   = src.data();
    size_t sz_src = src.size();

    std::vector<char> dst(src.size() * 2);
    char* p_dst   = dst.data();
    size_t sz_dst = dst.size();

    if (iconv(conv_value_, &p_src, &sz_src, &p_dst, &sz_dst) != static_cast<size_t>(-1)) {
        return std::string(dst.data(), dst.size() - sz_dst);
    }
    throw cosmo::util::ErrorMessage(cosmo::util::ErrorEnum::DecoderColorConvertFailed,
                                    (std::string("convert error: ") + strerror(errno)).c_str());
}

}  // namespace cosmo::util
