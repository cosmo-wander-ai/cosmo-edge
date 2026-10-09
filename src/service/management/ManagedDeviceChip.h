#pragma once

#include <array>
#include <cctype>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <string>

namespace cosmo::service::detail {

// Sophon firmware can expose the generic "cvitek,cv181x" compatible while the
// model property carries the actual accelerator. Neither the BMRT backend nor
// the legacy, hard-coded engine label identifies a chip.
inline std::string ReadSophonManagementChip(
    const std::filesystem::path& proc_tree = "/proc/device-tree",
    const std::filesystem::path& sys_tree  = "/sys/firmware/devicetree/base") {
    bool bm1688                                      = false;
    bool cv186x                                      = false;
    const std::array<std::filesystem::path, 4> paths = {proc_tree / "compatible", proc_tree / "model",
                                                        sys_tree / "compatible", sys_tree / "model"};
    for (const auto& path : paths) {
        std::ifstream input(path, std::ios::binary);
        if (!input)
            continue;
        std::array<char, 4097> bytes{};
        input.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        // Device-tree identity properties are short. Do not classify truncated
        // evidence or let a malformed field trigger unbounded allocation.
        if (input.bad() || input.gcount() == static_cast<std::streamsize>(bytes.size()))
            return "unknown";
        std::string token;
        auto accept = [&] {
            bm1688 = bm1688 || token == "bm1688";
            cv186x = cv186x || token == "cv186x" || token == "cv186ah" || token == "cv186";
            token.clear();
        };
        for (std::streamsize i = 0; i < input.gcount(); ++i) {
            const auto c = static_cast<unsigned char>(bytes[static_cast<std::size_t>(i)]);
            if (std::isalnum(c))
                token.push_back(static_cast<char>(std::tolower(c)));
            else
                accept();  // Also separates embedded NULs in compatible lists.
        }
        accept();
    }
    // Conflicting sources must not silently select a potentially incompatible model.
    if (bm1688 == cv186x)
        return "unknown";
    return bm1688 ? "bm1688" : "cv186x";
}

}  // namespace cosmo::service::detail
