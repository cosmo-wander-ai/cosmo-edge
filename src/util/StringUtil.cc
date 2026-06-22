// String utility implementation

#include "util/StringUtil.h"

#include <algorithm>
#include <stdexcept>
namespace cosmo::util {

std::vector<std::string_view> Split(std::string_view sv, std::string_view delimiters) {
    std::vector<std::string_view> res;
    size_t start = 0, pos = 0;

    for (;;) {
        pos = sv.find_first_of(delimiters, start);
        if (pos == std::string_view::npos) {
            break;
        }
        res.push_back(sv.substr(start, pos - start));
        start = pos + 1;
    }
    res.push_back(sv.substr(start));

    return res;
}

std::string_view Trim(std::string_view sv, std::string_view ch) {
    auto start = sv.find_first_not_of(ch);
    if (start == std::string_view::npos) {
        return {};
    }
    auto end = sv.find_last_not_of(ch);
    return sv.substr(start, end - start + 1);
}

std::string ToUpper(std::string_view str) {
    std::string retStr;
    retStr.reserve(str.size());
    std::transform(str.begin(), str.end(), std::back_inserter(retStr),
                   [](unsigned char c) { return std::toupper(c); });
    return retStr;
}

std::string ToLower(std::string_view str) {
    std::string retStr;
    retStr.reserve(str.size());
    std::transform(str.begin(), str.end(), std::back_inserter(retStr),
                   [](unsigned char c) { return std::tolower(c); });
    return retStr;
}

size_t UTF8Length(std::string_view str) {
    size_t count = 0;
    for (size_t i = 0; i < str.size(); ++i) {
        if ((str[i] & 0x80) == 0x00) {
            ++count;
        } else if ((str[i] & 0xE0) == 0xC0) {
            if (i + 1 < str.size() && (str[i + 1] & 0xC0) == 0x80) {
                ++count;
                i += 1;
            } else {
                throw std::logic_error("not UTF-8 string");
            }
        } else if ((str[i] & 0xF0) == 0xE0) {
            if (i + 2 < str.size() && (str[i + 1] & 0xC0) == 0x80 && (str[i + 2] & 0xC0) == 0x80) {
                ++count;
                i += 2;
            } else {
                throw std::logic_error("not UTF-8 string");
            }
        } else if ((str[i] & 0xF8) == 0xF0) {
            if (i + 3 < str.size() && (str[i + 1] & 0xC0) == 0x80 && (str[i + 2] & 0xC0) == 0x80 &&
                (str[i + 3] & 0xC0) == 0x80) {
                ++count;
                i += 3;
            } else {
                throw std::logic_error("not UTF-8 string");
            }
        }
    }
    return count;
}

std::string JoinStrings(const std::vector<std::string>& items, const char* sep) {
    std::string out;
    for (size_t i = 0; i < items.size(); ++i) {
        if (i > 0) {
            out.append(sep);
        }
        out.append(items[i]);
    }
    return out;
}

std::string_view GetLastPathSegment(std::string_view uri) {
    auto pos = uri.find_last_of('/');
    if (pos == std::string_view::npos) {
        return uri;
    }
    return uri.substr(pos + 1);
}

}  // namespace cosmo::util
