#pragma once
#include <fcntl.h>
#include <sys/stat.h>

#include <cerrno>
#include <cstddef>
#include <optional>
#include <string>
namespace cosmo::nn::axera::person_manifest {
inline bool SafeScene(const std::string& scene) {
    if (scene.empty() || scene.size() > 120 || scene == "." || scene == "..")
        return false;
    for (unsigned char c : scene) {
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' ||
              c == '-' || c == '.'))
            return false;
    }
    return true;
}
inline bool Matches(const std::string& manifest, const std::string& atomic, const std::string& scene) {
    if (atomic != "1001003" || !SafeScene(scene))
        return false;
    return manifest == atomic + ".json" || manifest == atomic + "-" + scene + ".json";
}
// Caller must use a previously pinned and trusted directory fd. A present
// scene-specific file always wins, even if invalid. Only ENOENT permits the
// legacy single-scene manifest, whose parsed scene_id remains mandatory.
inline std::optional<std::string> Select(int fd, const std::string& atomic, const std::string& scene) {
    if (fd < 0 || atomic != "1001003" || !SafeScene(scene))
        return std::nullopt;
    const std::string name = atomic + "-" + scene + ".json";
    struct stat st {};
    if (::fstatat(fd, name.c_str(), &st, AT_SYMLINK_NOFOLLOW) == 0)
        return name;
    if (errno != ENOENT)
        return std::nullopt;
    const std::string legacy = atomic + ".json";
    if (::fstatat(fd, legacy.c_str(), &st, AT_SYMLINK_NOFOLLOW) != 0)
        return std::nullopt;
    return legacy;
}
}  // namespace cosmo::nn::axera::person_manifest
