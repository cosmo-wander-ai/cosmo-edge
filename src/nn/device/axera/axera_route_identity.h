#pragma once

#include <string>
#include <string_view>

#ifdef COSMO_NN_USE_AXERA_BACKEND

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <map>
#include <nlohmann/json.hpp>

#include "flow/detect/AxeraTrustedDirectories.h"

namespace cosmo::nn::axera::route_identity {

inline bool SafeComponent(std::string_view value) {
    return !value.empty() && value.size() <= 120 && value != "." && value != ".." &&
           std::all_of(value.begin(), value.end(), [](char ch) {
               return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') ||
                      ch == '-' || ch == '_' || ch == '.';
           });
}

inline bool ValidDigest(std::string_view value) {
    return value.size() == 64 && std::all_of(value.begin(), value.end(), [](char ch) {
               return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f');
           });
}

namespace detail {

    using Catalog = std::map<std::string, std::string, std::less<>>;

    inline Catalog ReadCatalog(const std::string& path) {
        using namespace cosmo::axera_trusted_directories;
        constexpr off_t kCatalogLimit = 64 * 1024;
        const auto slash              = path.rfind('/');
        if (slash == std::string::npos || path.front() != '/' ||
            !SafeComponent(std::string_view(path).substr(slash + 1)))
            return {};

        const Fd directory{OpenTrustedAbsoluteDirectory(slash == 0 ? "/" : path.substr(0, slash))};
        if (directory.value < 0)
            return {};
        const Fd file{::openat(directory.value, path.c_str() + slash + 1,
                               O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK)};
        struct stat info {};
        if (file.value < 0 || ::fstat(file.value, &info) != 0 || !S_ISREG(info.st_mode) ||
            (info.st_uid != ::geteuid() && info.st_uid != 0) || (info.st_mode & 0022) || info.st_size <= 0 ||
            info.st_size > kCatalogLimit)
            return {};

        std::string content(static_cast<std::size_t>(info.st_size), '\0');
        std::size_t done = 0;
        while (done < content.size()) {
            const auto result = ::read(file.value, content.data() + done, content.size() - done);
            if (result < 0 && errno == EINTR)
                continue;
            if (result <= 0)
                return {};
            done += static_cast<std::size_t>(result);
        }
        const auto json = nlohmann::json::parse(content, nullptr, false);
        if (!json.is_object() || json.size() != 2 || !json.contains("schema") ||
            json["schema"] != "cosmo.axera-route-identities.v1" || !json.contains("identities") ||
            !json["identities"].is_object())
            return {};

        Catalog result;
        for (auto item = json["identities"].begin(); item != json["identities"].end(); ++item) {
            if (!SafeComponent(item.key()) || !item.value().is_string())
                return {};
            const auto& value = item.value().get_ref<const std::string&>();
            if (!SafeComponent(value))
                return {};
            result.emplace(item.key(), value);
        }
        return result;
    }

}  // namespace detail

inline const std::string& Lookup(std::string_view key) {
    static const auto catalog = [] {
        try {
            const auto* path = std::getenv("COSMO_AXERA_ROUTE_IDENTITIES");
            return path && *path ? detail::ReadCatalog(path) : detail::Catalog{};
        } catch (...) {
            return detail::Catalog{};
        }
    }();
    static const std::string empty;
    const auto item = catalog.find(key);
    return item == catalog.end() ? empty : item->second;
}

inline const std::string& Digest(std::string_view key) {
    static const std::string empty;
    const auto& value = Lookup(key);
    return ValidDigest(value) ? value : empty;
}

inline bool IsPersonAttributesModel(std::string_view atomic) {
    const auto& model = Lookup("person_attributes.atomic_code");
    return !model.empty() && atomic == model;
}

inline bool MatchesPersonAttributesRoute(std::string_view atomic, std::string_view scene) {
    const auto& configured_scene = Lookup("person_attributes.scene");
    return IsPersonAttributesModel(atomic) && !configured_scene.empty() && scene == configured_scene;
}

}  // namespace cosmo::nn::axera::route_identity

#else

namespace cosmo::nn::axera::route_identity {
inline const std::string& Digest(std::string_view) {
    static const std::string empty;
    return empty;
}
}  // namespace cosmo::nn::axera::route_identity

#endif
