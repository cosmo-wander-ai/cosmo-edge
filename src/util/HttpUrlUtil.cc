#include "util/HttpUrlUtil.h"

#include <algorithm>
#include <cctype>

namespace cosmo::util {

namespace {
    constexpr size_t kMaxHttpUrlBytes = 2048;
    constexpr std::string_view kHttpPrefix{"http://"};
    constexpr std::string_view kHttpsPrefix{"https://"};
}  // namespace

bool IsValidHttpUrl(std::string_view url) {
    if (url.empty() || url.size() > kMaxHttpUrlBytes || url.find('\\') != std::string_view::npos) {
        return false;
    }
    if (std::any_of(url.begin(), url.end(), [](char character) {
            const auto byte = static_cast<unsigned char>(character);
            return byte < 0x20 || byte == 0x7f;
        })) {
        return false;
    }

    size_t authority_begin = 0;
    if (url.compare(0, kHttpPrefix.size(), kHttpPrefix) == 0) {
        authority_begin = kHttpPrefix.size();
    } else if (url.compare(0, kHttpsPrefix.size(), kHttpsPrefix) == 0) {
        authority_begin = kHttpsPrefix.size();
    } else {
        return false;
    }

    const auto authority_end = url.find_first_of("/?#", authority_begin);
    const auto authority     = url.substr(authority_begin, authority_end - authority_begin);
    if (authority.empty() || authority.find('@') != std::string_view::npos) {
        return false;
    }

    const bool syntax_valid = std::all_of(authority.begin(), authority.end(), [](char character) {
        const auto byte = static_cast<unsigned char>(character);
        return std::isalnum(byte) != 0 || character == '.' || character == '-' || character == '_' ||
               character == ':' || character == '[' || character == ']';
    });
    return syntax_valid && std::any_of(authority.begin(), authority.end(), [](char character) {
               return std::isalnum(static_cast<unsigned char>(character)) != 0;
           });
}

}  // namespace cosmo::util
