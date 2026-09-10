#pragma once

#include <exception>
#include <filesystem>

namespace cosmo::test {

class ScopedCurrentPath final {
public:
    explicit ScopedCurrentPath(const std::filesystem::path& path)
        : previous_(std::filesystem::current_path()) {
        std::filesystem::current_path(path);
    }

    ~ScopedCurrentPath() noexcept {
        std::error_code error;
        std::filesystem::current_path(previous_, error);
        if (error) {
            std::terminate();
        }
    }

    ScopedCurrentPath(const ScopedCurrentPath&)            = delete;
    ScopedCurrentPath& operator=(const ScopedCurrentPath&) = delete;
    ScopedCurrentPath(ScopedCurrentPath&&)                 = delete;
    ScopedCurrentPath& operator=(ScopedCurrentPath&&)      = delete;

private:
    std::filesystem::path previous_;
};

}  // namespace cosmo::test
