#pragma once

#if defined(COSMO_NN_USE_AXERA_BACKEND) && defined(COSMO_AXERA_SCENARIOBENCH_CANDIDATE)
#include <cstdint>
#include <vector>

#include "flow/detect/AxeraTrustedDirectories.h"
#include "nn/device/axera/axera_route_identity.h"
#include "util/PathUtil.h"

namespace cosmo::nn::axera::benchmark {
bool VerifiedPersonModelBytes(const std::vector<std::uint8_t>& bytes);
}

namespace cosmo::axera_graph_admission {
// Same fixed root, ownership, no-follow, length and byte identity checks as
// the execution path. AX model loading and metadata checks still occur there.
inline bool VerifyTrustedBenchmarkPerson() noexcept {
    try {
        using namespace axera_trusted_directories;
        Fd root{OpenTrustedAbsoluteDirectory(path::GetAppBaseDir())};
        struct stat root_stat {};
        if (root.value < 0 || ::fstat(root.value, &root_stat) || !S_ISDIR(root_stat.st_mode) ||
            (root_stat.st_mode & 0022) || (root_stat.st_uid != ::geteuid() && root_stat.st_uid != 0))
            return false;
        const auto publisher = root_stat.st_uid;
        Fd resource{OpenDirectory(root.value, "resource")};
        if (!TrustedDirectory(resource.value, publisher))
            return false;
        Fd native{OpenDirectory(resource.value, "axera-native")};
        if (!TrustedDirectory(native.value, publisher))
            return false;
        Fd contracts{OpenDirectory(native.value, "contracts")};
        if (!TrustedDirectory(contracts.value, publisher))
            return false;
        Fd models{OpenDirectory(resource.value, "models")};
        if (!TrustedDirectory(models.value, publisher))
            return false;
        const auto& directory = nn::axera::route_identity::Lookup("benchmark.person_model_directory");
        if (directory.empty())
            return false;
        Fd model_dir{OpenDirectory(models.value, directory.c_str())};
        if (!TrustedDirectory(model_dir.value, publisher))
            return false;
        Fd fd{::openat(model_dir.value, "model.axmodel", O_RDONLY | O_CLOEXEC | O_NOFOLLOW)};
        struct stat st {};
        if (fd.value < 0 || ::fstat(fd.value, &st) || !S_ISREG(st.st_mode) || st.st_uid != publisher ||
            (st.st_mode & 0022) || st.st_size <= 0 || st.st_size > 32 * 1024 * 1024)
            return false;
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(st.st_size));
        std::size_t used = 0;
        while (used < bytes.size()) {
            const auto n = ::read(fd.value, bytes.data() + used, bytes.size() - used);
            if (n <= 0)
                return false;
            used += static_cast<std::size_t>(n);
        }
        return nn::axera::benchmark::VerifiedPersonModelBytes(bytes);
    } catch (...) {
        return false;
    }
}
}  // namespace cosmo::axera_graph_admission
#endif
