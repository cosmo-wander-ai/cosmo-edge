#pragma once
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <string>

#include "FaceModelBundle.h"
namespace cosmo::helmet_candidate {
struct FaceFd {
    int value{-1};
    ~FaceFd() {
        if (value >= 0)
            ::close(value);
    }
};
inline std::shared_ptr<const std::vector<std::uint8_t>> ReadFaceModelFile(const std::string& dir,
                                                                          const char* name) {
    FaceFd root{::open(dir.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW)};
    struct stat root_stat {};
    if (root.value < 0 || ::fstat(root.value, &root_stat) || !S_ISDIR(root_stat.st_mode) ||
        (root_stat.st_mode & 0022) || (root_stat.st_uid != 0 && root_stat.st_uid != ::geteuid()))
        return {};
    FaceFd file{::openat(root.value, name, O_RDONLY | O_CLOEXEC | O_NOFOLLOW)};
    struct stat before {
    }, after{};
    if (file.value < 0 || ::fstat(file.value, &before) || !S_ISREG(before.st_mode) ||
        (before.st_mode & 0022) || (before.st_uid != 0 && before.st_uid != ::geteuid()) ||
        before.st_size <= 0 || before.st_size > 256ll * 1024 * 1024)
        return {};
    auto bytes       = std::make_shared<std::vector<std::uint8_t>>(static_cast<std::size_t>(before.st_size));
    std::size_t done = 0;
    while (done < bytes->size()) {
        const auto n = ::read(file.value, bytes->data() + done, bytes->size() - done);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            return {};
        done += static_cast<std::size_t>(n);
    }
    if (::fstat(file.value, &after) || before.st_dev != after.st_dev || before.st_ino != after.st_ino ||
        before.st_size != after.st_size || before.st_mtim.tv_sec != after.st_mtim.tv_sec ||
        before.st_mtim.tv_nsec != after.st_mtim.tv_nsec || before.st_ctim.tv_sec != after.st_ctim.tv_sec ||
        before.st_ctim.tv_nsec != after.st_ctim.tv_nsec)
        return {};
    return bytes;
}
inline std::shared_ptr<FaceModelBundle> LoadJfifFaceModels(const std::string& dir) {
    const char* names[]{"jpeg-landmark.axmodel", "jpeg-crop.axmodel", "feature0.axmodel", "feature1.axmodel",
                        "feature2.axmodel"};
    std::array<std::shared_ptr<const std::vector<std::uint8_t>>, 5> snapshots{};
    std::array<FaceModelBytes, 5> inputs{};
    for (std::size_t i = 0; i < 5; ++i) {
        snapshots[i] = ReadFaceModelFile(dir, names[i]);
        if (!snapshots[i])
            return {};
        inputs[i] = {snapshots[i]->data(), snapshots[i]->size()};
    }
    return LoadFaceModelBundle(FaceInputProfile::Jfif601Full, inputs);
}
}  // namespace cosmo::helmet_candidate
