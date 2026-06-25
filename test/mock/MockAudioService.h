#pragma once
// clang-format off
#include "catch_amalgamated.hpp"
#include "catch2/trompeloeil.hpp"
// clang-format on
#include "flow/common/AlgDataUnit.h"
#include "service/media/IAudioService.h"
#include "trompeloeil.hpp"
#include "util/PathUtil.h"

namespace cosmo::test {

class MockAudioService : public cosmo::service::IAudioService {
public:
    MAKE_MOCK5(QueryAudioFiles,
               std::vector<cosmo::AlarmAudioInfo>(int&, const std::string&, const std::vector<std::string>&,
                                                  int, int),
               override);
    MAKE_MOCK1(GetAudioFileWebPath, std::string(cosmo::AlarmAudioInfo&), override);
    MAKE_MOCK1(GetAudioFileWebPath, std::string(const std::string&), override);
    MAKE_MOCK2(RemoveAudioFile, bool(const std::string&, std::string&), override);
    MAKE_MOCK1(AddAudioFile, bool(const std::string&), override);
    MAKE_CONST_MOCK0(AudioFileCount, size_t(), override);
    MAKE_CONST_MOCK0(AudioFileMaxCount, size_t(), override);
    MAKE_MOCK4(AddAudioDevice, bool(const std::string&, const std::string&, const std::string&, std::string&),
               override);
    MAKE_MOCK4(ModifyAudioDevice,
               bool(const std::string&, const std::string&, const std::string&, const std::string&),
               override);
    MAKE_MOCK2(RemoveAudioDevice, bool(const std::string&, std::string&), override);
    MAKE_MOCK4(QueryAudioDevices, std::vector<cosmo::AudioDeviceInfo>(int&, const std::string&, int, int),
               override);
    MAKE_MOCK1(CheckAudioDeviceAlive, bool(const std::string&), override);
    MAKE_MOCK1(PlayAudioDevice, bool(cosmo::AudioDevicePlay&), override);
};

}  // namespace cosmo::test
