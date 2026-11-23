#pragma once
#include <string>
#include <vector>

namespace cosmo::api::face_library_image {
inline bool IsImage(const std::vector<std::string> &parts) {
    if (parts.size() != 3 || parts[0] != "weblibPic" || parts[1] != "facePicture")
        return false;
    const auto &name = parts[2];
    if (name.size() != 40 || name.substr(36) != ".jpg")
        return false;
    for (std::size_t i = 0; i < 36; ++i) {
        const bool dash = i == 8 || i == 13 || i == 18 || i == 23;
        const char c    = name[i];
        if (dash ? c != '-' : !((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')))
            return false;
    }
    return true;
}
}  // namespace cosmo::api::face_library_image
