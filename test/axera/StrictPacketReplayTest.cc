// Offline host mock of every indexed packet. FNV scan is diagnostic CPU work,
// not part of the eventual strict data path or proof of VDEC compatibility.
#include <fcntl.h>
#include <unistd.h>

#include <cassert>
#include <cstdint>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "media/AxeraCompressedPacketLease.h"
using namespace cosmo::media;
int main(int argc, char** argv) {
    assert(argc == 3);
    const int fd = open(argv[1], O_RDONLY | O_CLOEXEC);
    assert(fd >= 0);
    std::ifstream manifest(argv[2]);
    assert(manifest);
    auto bytes = std::make_shared<std::vector<uint8_t>>(400790);
    AxPacketPool pool({{0x100000, bytes->data(), bytes->size(), bytes}});
    assert(pool.Valid());
    std::string line;
    uint64_t checksum = 14695981039346656037ULL, count = 0;
    int64_t previous_pts = INT64_MIN;
    unsigned reordered   = 0;
    while (std::getline(manifest, line)) {
        std::stringstream ss(line);
        std::string pts, dts, size, pos, flags;
        assert(std::getline(ss, pts, ',') && std::getline(ss, dts, ',') && std::getline(ss, size, ',') &&
               std::getline(ss, pos, ',') && std::getline(ss, flags, ','));
        AxPacketLocation location{std::stoll(pos),
                                  static_cast<size_t>(std::stoull(size)),
                                  std::stoll(pts),
                                  std::stoll(dts),
                                  count,
                                  AxPacketEncoding::Avcc,
                                  1,
                                  12288,
                                  512,
                                  flags.find('K') != std::string::npos};
        if (previous_pts != INT64_MIN && location.pts < previous_pts)
            ++reordered;
        previous_pts = location.pts;
        auto lease   = pool.Acquire();
        assert(lease && lease->ReadFromFile(fd, location));
        assert(lease->Location().decode_sequence == count && lease->Location().pts == location.pts);
        const auto* data = lease->MappedAddress();
        assert(data);
        for (size_t i = 0; i < location.bytes; ++i)
            checksum = (checksum ^ data[i]) * 1099511628211ULL;
        assert(lease->MockSubmit(true) && lease->MockComplete());
        ++count;
    }
    assert(count == 360 && reordered > 0 && checksum == 0x90e3efe8e074ac91ULL);
    assert(pool.MockEndOfStream() && !pool.Acquire());
    close(fd);
}
