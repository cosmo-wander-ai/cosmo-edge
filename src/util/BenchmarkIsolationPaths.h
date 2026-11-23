#pragma once

#include <algorithm>
#include <filesystem>
#include <stdexcept>
#include <string>

namespace cosmo::util {

inline bool BenchmarkPathWithin(const std::filesystem::path& root, const std::filesystem::path& path) {
    return std::mismatch(root.begin(), root.end(), path.begin(), path.end()).first == root.end();
}

inline void ValidateBenchmarkRoots(const char* data, const char* app, const char* default_data,
                                   const char* default_app) {
    namespace fs = std::filesystem;
    if (!data || !*data || !app || !*app)
        throw std::runtime_error("benchmark isolation requires explicit data and app roots");
    const auto validate = [](const fs::path& path, const char* marker) {
        std::error_code ec;
        const auto canonical = fs::canonical(path, ec);
        if (ec || !path.is_absolute() || canonical == "/" || canonical != path.lexically_normal() ||
            !fs::is_directory(canonical))
            throw std::runtime_error("benchmark roots must be existing canonical directories");
        const auto marker_status = fs::symlink_status(canonical / marker, ec);
        if (ec || !fs::is_regular_file(marker_status))
            throw std::runtime_error("benchmark roots require their regular marker files");
        return canonical;
    };
    const auto data_root = validate(data, ".cosmo-benchmark-isolation");
    const auto app_root  = validate(app, ".cosmo-benchmark-app");
    if (BenchmarkPathWithin(data_root, app_root) || BenchmarkPathWithin(app_root, data_root))
        throw std::runtime_error("benchmark app and data roots must be separate directories");
    for (const char* default_root : {default_data, default_app}) {
        if (!default_root || !*default_root)
            continue;
        std::error_code ec;
        const auto root = fs::weakly_canonical(default_root, ec);
        if (!ec && (BenchmarkPathWithin(root, data_root) || BenchmarkPathWithin(root, app_root) ||
                    BenchmarkPathWithin(data_root, root) || BenchmarkPathWithin(app_root, root)))
            throw std::runtime_error("benchmark roots must not overlap default roots");
    }
}

}  // namespace cosmo::util
