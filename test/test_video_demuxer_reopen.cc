#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "catch_amalgamated.hpp"
#include "media/VideoDemuxer.h"

namespace {
class DemuxInputs {
public:
    DemuxInputs() {
        auto pattern = (std::filesystem::temp_directory_path() / "cosmo-demux-reopen-XXXXXX").string();
        std::vector<char> buffer(pattern.begin(), pattern.end());
        buffer.push_back('\0');
        auto* created = mkdtemp(buffer.data());
        if (!created) {
            throw std::runtime_error("Cannot create demux test directory");
        }
        root   = created;
        first  = root / "first.wav";
        second = root / "second.wav";
        WriteWave(first);
        WriteWave(second);
    }
    ~DemuxInputs() {
        std::error_code error;
        std::filesystem::remove_all(root, error);
    }
    static void WriteWave(const std::filesystem::path& path) {
        // Mono 8 kHz / 16-bit PCM. Only format probing is needed; no decoder or model.
        const std::array<unsigned char, 44> header = {
            'R', 'I', 'F', 'F', 0xa4, 0x3e, 0,   0,   'W', 'A',  'V',  'E',  'f', 'm',  't',
            ' ', 16,  0,   0,   0,    1,    0,   1,   0,   0x40, 0x1f, 0,    0,   0x80, 0x3e,
            0,   0,   2,   0,   16,   0,    'd', 'a', 't', 'a',  0x80, 0x3e, 0,   0};
        const std::array<char, 16000> samples{};
        std::ofstream file(path, std::ios::binary);
        file.write(reinterpret_cast<const char*>(header.data()), header.size());
        file.write(samples.data(), samples.size());
        if (!file)
            throw std::runtime_error("Cannot write demux test input");
    }
    std::filesystem::path root, first, second;
};

size_t OpenDescriptorsFor(const std::filesystem::path& path) {
    size_t result = 0;
    for (const auto& entry : std::filesystem::directory_iterator("/proc/self/fd")) {
        std::error_code error;
        const auto target = std::filesystem::read_symlink(entry.path(), error);
        if (!error && target == path)
            ++result;
    }
    return result;
}
}  // namespace

TEST_CASE("Opening another stream releases the previous input", "[demux][reopen]") {
    DemuxInputs inputs;
    cosmo::media::VideoDemuxer demux;
    demux.SetFile(inputs.first.string());
    REQUIRE(demux.OpenStream() == cosmo::util::ErrorEnum::Success);
    REQUIRE(OpenDescriptorsFor(inputs.first) == 1);
    demux.SetFile(inputs.second.string());
    REQUIRE(demux.OpenStream() == cosmo::util::ErrorEnum::Success);
    CHECK(OpenDescriptorsFor(inputs.first) == 0);
    CHECK(OpenDescriptorsFor(inputs.second) == 1);
    demux.CloseStream();
    CHECK(OpenDescriptorsFor(inputs.second) == 0);
}

TEST_CASE("Failed replacement releases the previous input and can recover", "[demux][reopen]") {
    DemuxInputs inputs;
    cosmo::media::VideoDemuxer demux;
    demux.SetFile(inputs.first.string());
    REQUIRE(demux.OpenStream() == cosmo::util::ErrorEnum::Success);
    demux.SetFile((inputs.root / "missing.wav").string());
    CHECK(demux.OpenStream() == cosmo::util::ErrorEnum::DemuxOpenStreamFail);
    CHECK(OpenDescriptorsFor(inputs.first) == 0);
    demux.SetFile(inputs.second.string());
    REQUIRE(demux.OpenStream() == cosmo::util::ErrorEnum::Success);
    CHECK(OpenDescriptorsFor(inputs.second) == 1);
}

TEST_CASE("Local repeated playback keeps its current input", "[demux][reopen]") {
    DemuxInputs inputs;
    cosmo::media::VideoDemuxer demux;
    demux.SetFile(inputs.first.string());
    REQUIRE(demux.OpenStream() == cosmo::util::ErrorEnum::Success);
    REQUIRE(demux.OpenStream(true) == cosmo::util::ErrorEnum::Success);
    CHECK(OpenDescriptorsFor(inputs.first) == 1);
}
